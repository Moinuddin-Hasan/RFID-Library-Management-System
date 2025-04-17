#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>     // Library for RFID reader functionality
#include <WiFi.h>        // Enables WiFi connectivity for our ESP32
#include <WebServer.h>   // Handles HTTP server operations
#include <SPIFFS.h>      // File system for storing web files and data
#include <Wire.h>        // Required for I2C communication with LCD
#include <LiquidCrystal_I2C.h> // Controls our I2C LCD display
#include <ArduinoJson.h> // Makes working with JSON data much easier

// Pin definitions - hardware connections for our system
#define RST_PIN 16  // Reset pin for RFID module
#define SS_PIN 5    // SDA/SS pin for RFID module (chip select)
#define IR_PIN 4    // IR motion sensor input pin
#define SDA_PIN 21  // I2C data line for LCD display
#define SCL_PIN 22  // I2C clock line for LCD display

// WiFi credentials - we're creating an access point for users to connect to
const char* ssid = "Library Kiosk 1";  // Our access point name
const char* password = "";  // Empty password = open network for easy access

// Global objects initialization
MFRC522 rfid(SS_PIN, RST_PIN);  // RFID reader instance
LiquidCrystal_I2C lcd(0x27, 16, 2); // LCD screen (I2C address may need adjustment for your specific LCD)
WebServer server(80);  // Web server on standard HTTP port

// Motion detection variables to manage user presence
volatile bool motionDetected = false;  // Flag set by interrupt
unsigned long motionTimestamp = 0;     // When motion was last detected
const unsigned long SCAN_TIMEOUT = 5000; // 5 seconds window to scan card after motion
bool scanningActive = false;           // Whether we're currently in scan mode

// Card tracking variables
String currentCardUID = "";           // Stores the most recently scanned card
unsigned long lastCardTime = 0;       // When the card was last scanned
const unsigned long CARD_RESET_TIME = 10000; // Clear card data after 10 seconds of inactivity

// LCD display management for scrolling text
String scrollText = "";               // Text to scroll on LCD
int scrollPosition = 0;               // Current position in scrolling text
unsigned long lastScrollTime = 0;     // Time tracking for smooth scrolling
const int scrollSpeed = 400;          // Milliseconds between scroll updates

// RFID reading modes to determine how to handle scanned cards
enum RFIDMode {
  NORMAL,   // Regular card scanning (checkout/return)
  NEW_USER, // Registering a card for a new user
  NEW_BOOK  // Registering a card for a new book
};

RFIDMode currentMode = NORMAL;  // Start in normal scanning mode

// Interrupt handler for IR sensor - runs when motion is detected
// IRAM_ATTR ensures this runs from RAM for faster response time
void IRAM_ATTR motionInterrupt() {
  motionDetected = true;  // Just set the flag, keep ISR short and simple
}

// Helper function to convert RFID's raw UID bytes to readable hex string
String getUIDString(MFRC522::Uid *uid) {
  String uidStr = "";
  for (byte i = 0; i < uid->size; i++) {
    if (uid->uidByte[i] < 0x10) uidStr += "0";  // Pad with leading zero if needed
    uidStr += String(uid->uidByte[i], HEX);     // Convert byte to hex string
  }
  uidStr.toUpperCase();  // Make it all uppercase for consistency
  return uidStr;
}

// Handles the scrolling text effect on our LCD display
void scrollLcdText() {
  if (scrollText.length() > 16) {  // Only scroll if text is longer than display width
    unsigned long currentTime = millis();
    if (currentTime - lastScrollTime > scrollSpeed) {  // Time to scroll one position
      lastScrollTime = currentTime;
      lcd.clear();
      lcd.setCursor(0, 0);
      
      // Calculate the substring to display (16 characters)
      int endPos = scrollPosition + 16;
      if (endPos > scrollText.length()) {
        // If we reach the end, add spaces then continue from the beginning
        String displayText = scrollText.substring(scrollPosition);
        int remainingChars = 16 - displayText.length();
        if (remainingChars > 0) {
          displayText += "   " + scrollText.substring(0, remainingChars);  // Add gap before repeating
        }
        lcd.print(displayText);
      } else {
        lcd.print(scrollText.substring(scrollPosition, endPos));
      }
      
      // Increment scroll position and reset if we're past the end
      scrollPosition++;
      if (scrollPosition > scrollText.length() + 3) {  // +3 for the spaces
        scrollPosition = 0;  // Start over from the beginning
      }
      
      // Always show IP address on second line for user connection info
      lcd.setCursor(0, 1);
      lcd.print("IP: " + WiFi.softAPIP().toString());
    }
  }
}

// Load JSON data from a file in our SPIFFS file system
String loadFile(const char* path) {
  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading: " + String(path));
    return "{}";  // Return empty JSON object on failure
  }
  
  String content = file.readString();
  file.close();
  return content;
}

// Save JSON data to a file in our SPIFFS file system
bool saveFile(const char* path, const String& content) {
  File file = SPIFFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing: " + String(path));
    return false;
  }
  
  bool success = file.print(content);
  file.close();
  return success;
}

// Robustly serve files from SPIFFS with multiple path fallbacks
void serveFile(const String& path, const String& contentType) {
  // Try several possible file locations - exact path first
  if (SPIFFS.exists(path)) {
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    Serial.println("Served file: " + path);
    return;
  }
  
  // Then try with /data prefix - our alternate storage location
  if (SPIFFS.exists("/data" + path)) {
    File file = SPIFFS.open("/data" + path, "r");
    server.streamFile(file, contentType);
    file.close();
    Serial.println("Served file: /data" + path);
    return;
  }
  
  // Try without leading slash - common path variation
  String noLeadingSlash = path;
  if (path.startsWith("/")) {
    noLeadingSlash = path.substring(1);
  }
  
  if (SPIFFS.exists(noLeadingSlash)) {
    File file = SPIFFS.open(noLeadingSlash, "r");
    server.streamFile(file, contentType);
    file.close();
    Serial.println("Served file: " + noLeadingSlash);
    return;
  }
  
  // Last attempt with /data prefix without leading slash
  if (SPIFFS.exists("/data/" + noLeadingSlash)) {
    File file = SPIFFS.open("/data/" + noLeadingSlash, "r");
    server.streamFile(file, contentType);
    file.close();
    Serial.println("Served file: /data/" + noLeadingSlash);
    return;
  }
  
  // If we got here, file wasn't found in any location
  server.send(404, "text/plain", "File not found: " + path);
  Serial.println("File not found: " + path);

  // Debug: List all files to help diagnose the issue
  Serial.println("Files in SPIFFS:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.print("- ");
    Serial.println(file.name());
    file = root.openNextFile();
  }
}

// Web server request handlers for different pages of our interface
void handleRoot() {
  serveFile("/index.html", "text/html");
}

void handleCSS() {
  serveFile("/styles.css", "text/css");
}

void handleJS() {
  serveFile("/scripts.js", "application/javascript");
}

void handleAdmin() {
  serveFile("/admin.html", "text/html");
}

void handleStudent() {
  serveFile("/student.html", "text/html");
}

void handleBooks() {
  serveFile("/books.html", "text/html");
}

// API endpoint to get the last scanned card UID for the web interface
void handleScan() {
  // Check if card UID has expired - security feature
  if (millis() - lastCardTime > CARD_RESET_TIME) {
    currentCardUID = "";  // Clear old card data after timeout
  }
  
  // Only return card data if we have a recent scan
  if (currentCardUID != "") {
    server.send(200, "application/json", "{\"uid\":\"" + currentCardUID + "\", \"timestamp\":" + String(lastCardTime) + "}");
    
    // If we're in registration mode, reset back to normal after sending the data
    if (currentMode != NORMAL) {
      currentMode = NORMAL;
    }
  } else {
    server.send(200, "application/json", "{\"uid\":\"\", \"timestamp\":0}");
  }
}

// API endpoint to clear the card UID - useful after processing a transaction
void handleClearCard() {
  currentCardUID = "";
  server.send(200, "text/plain", "Card cleared");
}

// API endpoint to set the RFID scan mode from the web interface
void handleMode() {
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    if (mode == "user") {
      currentMode = NEW_USER;
      // Trigger motion detection to immediately start scan process
      motionDetected = true;
      server.send(200, "text/plain", "Mode set to new user");
    } else if (mode == "book") {
      currentMode = NEW_BOOK;
      // Trigger motion detection to immediately start scan process
      motionDetected = true;
      server.send(200, "text/plain", "Mode set to new book");
    } else {
      currentMode = NORMAL;
      server.send(200, "text/plain", "Mode set to normal");
    }
  } else {
    server.send(400, "text/plain", "Missing mode parameter");
  }
}

// API endpoint to get the list of all registered users
void handleGetUsers() {
  String usersData = loadFile("/users.json");
  server.send(200, "application/json", usersData);
}

// API endpoint to get the list of all library books
void handleGetBooks() {
  String booksData = loadFile("/books.json");
  server.send(200, "application/json", booksData);
}

// API endpoint to update the users database file
void handleUpdateUsers() {
  if (server.hasArg("data")) {
    String data = server.arg("data");
    if (saveFile("/users.json", data)) {
      server.send(200, "text/plain", "Users data updated successfully");
    } else {
      server.send(500, "text/plain", "Failed to update users data");
    }
  } else {
    server.send(400, "text/plain", "Missing data parameter");
  }
}

// API endpoint to update the books database file
void handleUpdateBooks() {
  if (server.hasArg("data")) {
    String data = server.arg("data");
    if (saveFile("/books.json", data)) {
      server.send(200, "text/plain", "Books data updated successfully");
    } else {
      server.send(500, "text/plain", "Failed to update books data");
    }
  } else {
    server.send(400, "text/plain", "Missing data parameter");
  }
}

// API endpoint to check if a specific book is currently borrowed
void handleCheckBorrowed() {
  if (server.hasArg("id")) {
    String bookId = server.arg("id");
    String booksData = loadFile("/books.json");
    
    // Parse the JSON data to search through books
    DynamicJsonDocument doc(8192);  // Larger size for full library catalog
    deserializeJson(doc, booksData);
    
    bool borrowed = false;
    JsonArray books = doc["books"].as<JsonArray>();
    
    // Search for the book by ID and check its borrowed status
    for (JsonObject book : books) {
      if (book["id"] == bookId && book["borrowed"] == true) {
        borrowed = true;
        break;
      }
    }
    
    server.send(200, "application/json", "{\"borrowed\":" + String(borrowed ? "true" : "false") + "}");
  } else {
    server.send(400, "text/plain", "Missing id parameter");
  }
}

void setup() {
  Serial.begin(115200);  // Start serial communication for debugging
  Serial.println("Starting Library Management System");
  
  // Initialize the file system for our web interface and data
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  Serial.println("SPIFFS mounted successfully");
  
  // List all files to help with debugging
  Serial.println("Files in SPIFFS:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.print("- ");
    Serial.println(file.name());
    file = root.openNextFile();
  }
  
  // Initialize LCD display and show startup message
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Library System");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  // Initialize SPI communication and RFID reader
  SPI.begin();
  rfid.PCD_Init();
  delay(4);  // Brief delay for RFID module to stabilize
  Serial.println("RFID reader initialized");
  
  // Set up IR motion sensor with interrupt for user detection
  pinMode(IR_PIN, INPUT);
  Serial.println("Setting up IR sensor interrupt on pin " + String(IR_PIN));
  attachInterrupt(digitalPinToInterrupt(IR_PIN), motionInterrupt, RISING);
  Serial.println("IR sensor interrupt set up");
  
  // Test interrupt by triggering it once
  motionDetected = true;
  Serial.println("Motion detection flag manually set for testing");
  
  // Create WiFi access point for users to connect to our system
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  // Set up scrolling text for the LCD welcome message
  scrollText = "WiFi: " + String(ssid);
  lcd.clear();
  lcd.setCursor(0, 0);
  
  // Only scroll if the text is too long for the display
  if (scrollText.length() <= 16) {
    lcd.print(scrollText);
  } else {
    // Otherwise, display first portion and begin scrolling
    lcd.print(scrollText.substring(0, 16));
  }
  
  lcd.setCursor(0, 1);
  lcd.print("IP: " + IP.toString());
  
  // Create default data files if this is first boot
  if (!SPIFFS.exists("/users.json")) {
    Serial.println("Creating default users.json");
    saveFile("/users.json", "{\"users\":[{\"type\":\"staff\",\"username\":\"admin\",\"password\":\"admin123\",\"cardUid\":\"A286FF03\"},{\"type\":\"staff\",\"username\":\"staff1\",\"password\":\"staff123\",\"cardUid\":\"530D2349029380\"},{\"type\":\"student\",\"studentId\":\"S001\",\"password\":\"student123\",\"name\":\"John Smith\",\"email\":\"john.smith@example.com\",\"cardUid\":\"538426E2023F80\"},{\"type\":\"student\",\"studentId\":\"S002\",\"password\":\"student456\",\"name\":\"Emily Davis\",\"email\":\"emily.davis@example.com\",\"cardUid\":\"53ADFDCE028D80\"},{\"type\":\"student\",\"studentId\":\"S003\",\"password\":\"student789\",\"name\":\"David Johnson\",\"email\":\"david.johnson@example.com\",\"cardUid\":\"5395276302DC80\"}]}");
  }
  
  if (!SPIFFS.exists("/books.json")) {
    Serial.println("Creating default books.json");
    // Sample books with borrowing history and status
    saveFile("/books.json", "{\"books\":[{\"id\":\"B001\",\"isbn\":\"B001\",\"title\":\"Introduction to Programming\",\"author\":\"Jane Smith\",\"shelf\":\"R1C1\",\"floor\":\"1\",\"borrowed\":false,\"cardUid\":\"53C4734302A380\",\"history\":[]},{\"id\":\"B002\",\"isbn\":\"B002\",\"title\":\"Data Structures and Algorithms\",\"author\":\"Robert Johnson\",\"shelf\":\"R2C3\",\"floor\":\"1\",\"borrowed\":true,\"borrowedBy\":\"S001\",\"borrowDate\":\"2025-04-02T10:15:00Z\",\"returnDate\":\"2025-04-16T10:15:00Z\",\"cardUid\":\"53FC884B020880\",\"history\":[{\"username\":\"S001\",\"borrowDate\":\"2025-04-02T10:15:00Z\",\"returnDate\":null},{\"username\":\"S002\",\"borrowDate\":\"2025-03-01T14:30:00Z\",\"returnDate\":\"2025-03-14T11:45:00Z\"}]},{\"id\":\"B003\",\"isbn\":\"B003\",\"title\":\"Database Management Systems\",\"author\":\"Michael Chen\",\"shelf\":\"R3C2\",\"floor\":\"1\",\"borrowed\":false,\"cardUid\":\"53E5BCC6021280\",\"history\":[{\"username\":\"S003\",\"borrowDate\":\"2025-02-10T09:20:00Z\",\"returnDate\":\"2025-02-20T16:30:00Z\"}]},{\"id\":\"B004\",\"isbn\":\"B004\",\"title\":\"Computer Networks\",\"author\":\"Sarah Williams\",\"shelf\":\"R1C4\",\"floor\":\"2\",\"borrowed\":true,\"borrowedBy\":\"S002\",\"borrowDate\":\"2025-04-05T13:40:00Z\",\"returnDate\":\"2025-04-19T13:40:00Z\",\"cardUid\":\"53940740028780\",\"history\":[{\"username\":\"S002\",\"borrowDate\":\"2025-04-05T13:40:00Z\",\"returnDate\":null}]},{\"id\":\"B005\",\"isbn\":\"B005\",\"title\":\"Artificial Intelligence\",\"author\":\"David Brown\",\"shelf\":\"R2C1\",\"floor\":\"2\",\"borrowed\":false,\"cardUid\":\"53DD0760026780\",\"history\":[]},{\"id\":\"B006\",\"isbn\":\"B006\",\"title\":\"Operating Systems\",\"author\":\"Patricia Garcia\",\"shelf\":\"R3C3\",\"floor\":\"2\",\"borrowed\":false,\"cardUid\":\"53FCD8C402C580\",\"history\":[{\"username\":\"S001\",\"borrowDate\":\"2025-01-15T11:10:00Z\",\"returnDate\":\"2025-01-29T15:25:00Z\"}]},{\"id\":\"B007\",\"isbn\":\"B007\",\"title\":\"Software Engineering\",\"author\":\"Thomas Lee\",\"shelf\":\"R1C2\",\"floor\":\"3\",\"borrowed\":true,\"borrowedBy\":\"S003\",\"borrowDate\":\"2025-03-28T10:30:00Z\",\"returnDate\":\"2025-04-11T10:30:00Z\",\"cardUid\":\"530C524C02F680\",\"history\":[{\"username\":\"S003\",\"borrowDate\":\"2025-03-28T10:30:00Z\",\"returnDate\":null}]},{\"id\":\"B008\",\"isbn\":\"B008\",\"title\":\"Web Development\",\"author\":\"Lisa Johnson\",\"shelf\":\"R2C4\",\"floor\":\"3\",\"borrowed\":false,\"cardUid\":\"53BDECCB02E080\",\"history\":[]},{\"id\":\"B009\",\"isbn\":\"B009\",\"title\":\"Machine Learning Basics\",\"author\":\"Alan Turner\",\"shelf\":\"R4C1\",\"floor\":\"1\",\"borrowed\":true,\"borrowedBy\":\"S001\",\"borrowDate\":\"2025-03-15T10:30:00Z\",\"returnDate\":\"2025-03-29T10:30:00Z\",\"cardUid\":\"53ABCDEF02E080\",\"history\":[{\"username\":\"S001\",\"borrowDate\":\"2025-03-15T10:30:00Z\",\"returnDate\":null}]}]}");
  }

  // Create data directory for web files
  if (!SPIFFS.exists("/data")) {
    SPIFFS.mkdir("/data");
  }
  
  // Copy web files to data directory for consistent access
  if (SPIFFS.exists("/index.html") && !SPIFFS.exists("/data/index.html")) {
    File sourceFile = SPIFFS.open("/index.html", "r");
    File destFile = SPIFFS.open("/data/index.html", "w");
    if (sourceFile && destFile) {
      destFile.print(sourceFile.readString());
    }
    sourceFile.close();
    destFile.close();
  }
  
  // Set up server routes for web interface pages
  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/admin.html", HTTP_GET, handleAdmin);
  server.on("/student.html", HTTP_GET, handleStudent);
  server.on("/books.html", HTTP_GET, handleBooks);
  server.on("/styles.css", HTTP_GET, handleCSS);
  server.on("/scripts.js", HTTP_GET, handleJS);
  
  // Configure API endpoints for web interface to interact with hardware
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/clear-card", HTTP_GET, handleClearCard);
  server.on("/api/mode", HTTP_GET, handleMode);
  server.on("/api/users", HTTP_GET, handleGetUsers);
  server.on("/api/books", HTTP_GET, handleGetBooks);
  server.on("/api/users", HTTP_POST, handleUpdateUsers);
  server.on("/api/books", HTTP_POST, handleUpdateBooks);
  server.on("/api/check-borrowed", HTTP_GET, handleCheckBorrowed);
  
  // Create custom 404 page to help diagnose missing files
  server.onNotFound([]() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
    Serial.println("404 Error: " + server.uri());
  });
  
  // Start web server to accept connections
  server.begin();
  Serial.println("Web server started");
  Serial.println("Setup complete");
}

void loop() {
  // Handle any pending web client requests
  server.handleClient();
  
  // Get current time for timing operations
  unsigned long currentTime = millis();
  
  // If we're not actively waiting for a card scan, show scrolling info
  if (!scanningActive) {
    scrollLcdText();
  }
  
  // Periodically check the IR sensor directly and log its state
  static unsigned long lastPinCheck = 0;
  if (currentTime - lastPinCheck > 5000) {  // Every 5 seconds
    lastPinCheck = currentTime;
    int irValue = digitalRead(IR_PIN);
    Serial.print("IR Pin value: ");
    Serial.println(irValue);
  }
  
  // Handle new motion detection events
  if (motionDetected && !scanningActive) {
    // Start a new scanning session when motion is detected
    motionTimestamp = currentTime;
    scanningActive = true;
    
    // Clear any previous card data for security
    currentCardUID = "";
    
    // Update LCD to show scan instructions
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Motion detected");
    lcd.setCursor(0, 1);
    lcd.print("Scan in 5 sec...");
    Serial.println("Motion detected, ready to scan");
    
    // Reset the flag so we don't trigger again until next motion
    motionDetected = false;
  }
  
  // Handle active scanning sessions
  if (scanningActive) {
    // Calculate elapsed time since motion detected
    unsigned long elapsedTime = currentTime - motionTimestamp;
    
    // If we're still within the scanning window
    if (elapsedTime < SCAN_TIMEOUT) {
      // Calculate seconds remaining and update display
      int remainingSeconds = 5 - (elapsedTime / 1000);
      
      // Only update the display when the second changes (reduce flicker)
      static int lastSecond = -1;
      if (remainingSeconds != lastSecond) {
        lastSecond = remainingSeconds;
        lcd.setCursor(0, 1);
        lcd.print("Scan in ");
        lcd.print(remainingSeconds);
        lcd.print(" sec...  ");
      }
      
      // Check for RFID card presence during the scan window
      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        // Successfully read a card - save its details
        currentCardUID = getUIDString(&rfid.uid);
        lastCardTime = currentTime;
        
        // Update LCD with card info and mode
        lcd.clear();
        lcd.setCursor(0, 0);
        
        // Show different messages based on current mode
        if (currentMode == NEW_USER) {
          lcd.print("New User Card");
          Serial.println("New user card: " + currentCardUID);
        } else if (currentMode == NEW_BOOK) {
          lcd.print("New Book Card");
          Serial.println("New book card: " + currentCardUID);
        } else {
          lcd.print("Card Detected");
          Serial.println("Card scanned: " + currentCardUID);
        }
        
        // Show UID on second line of display
        lcd.setCursor(0, 1);
        lcd.print("UID: " + currentCardUID);
        
        // End scanning session
        scanningActive = false;
        
        // Stop RFID communication to release the card
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        
        // Keep success message visible briefly
        delay(2000);
        
        // Reset scrolling text position
        scrollPosition = 0;
      }
    } else {
      // Scan timeout reached - no card detected
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Scan timeout");
      lcd.setCursor(0, 1);
      lcd.print("Try again");
      
      // End scan session
      scanningActive = false;
      delay(2000); // Show timeout message for 2 seconds
      
      // Reset scroll position for info display
      scrollPosition = 0;
    }
  }
  
  // Brief delay to prevent CPU hogging
  delay(50);
}