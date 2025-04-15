#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// Pin definitions
#define RST_PIN 16  // RST pin connected to GPIO 4
#define SS_PIN 5    // SDA pin connected to GPIO 5
#define IR_PIN 4   // IR sensor connected to GPIO 13
#define SDA_PIN 21  // I2C LCD SDA pin
#define SCL_PIN 22  // I2C LCD SCL pin

// WiFi credentials
const char* ssid = "Library Kiosk 1";  // WiFi network name
const char* password = "";  // Empty password for open network

// Global variables
MFRC522 rfid(SS_PIN, RST_PIN);  // RFID reader
LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C LCD 16x2 (address might need adjustment)
WebServer server(80);  // Web server on port 80

// Motion detection variables
volatile bool motionDetected = false;
unsigned long motionTimestamp = 0;
const unsigned long SCAN_TIMEOUT = 5000; // 5 seconds scan window
bool scanningActive = false;

// Current card UID - only stored temporarily
String currentCardUID = "";
unsigned long lastCardTime = 0;
const unsigned long CARD_RESET_TIME = 10000; // Reset card after 10 seconds

// LCD scrolling text variables
String scrollText = "";
int scrollPosition = 0;
unsigned long lastScrollTime = 0;
const int scrollSpeed = 400; // milliseconds between scrolls

// RFID reading mode
enum RFIDMode {
  NORMAL,
  NEW_USER,
  NEW_BOOK
};

RFIDMode currentMode = NORMAL;

// Interrupt handler for IR sensor - simplified for reliability
void IRAM_ATTR motionInterrupt() {
  motionDetected = true;
}

// Convert UID to String
String getUIDString(MFRC522::Uid *uid) {
  String uidStr = "";
  for (byte i = 0; i < uid->size; i++) {
    if (uid->uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(uid->uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  return uidStr;
}

// Scroll text on LCD
void scrollLcdText() {
  if (scrollText.length() > 16) {
    unsigned long currentTime = millis();
    if (currentTime - lastScrollTime > scrollSpeed) {
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
          displayText += "   " + scrollText.substring(0, remainingChars);
        }
        lcd.print(displayText);
      } else {
        lcd.print(scrollText.substring(scrollPosition, endPos));
      }
      
      // Increment scroll position
      scrollPosition++;
      if (scrollPosition > scrollText.length() + 3) { // +3 for the spaces
        scrollPosition = 0;
      }
      
      // Always show IP on second line
      lcd.setCursor(0, 1);
      lcd.print("IP: " + WiFi.softAPIP().toString());
    }
  }
}

// Function to load JSON file from SPIFFS
String loadFile(const char* path) {
  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading: " + String(path));
    return "{}";
  }
  
  String content = file.readString();
  file.close();
  return content;
}

// Function to save JSON file to SPIFFS
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

// Serve static files
void serveFile(const String& path, const String& contentType) {
  // First try the exact path
  if (SPIFFS.exists(path)) {
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    Serial.println("Served file: " + path);
    return;
  }
  
  // Then try with /data prefix
  if (SPIFFS.exists("/data" + path)) {
    File file = SPIFFS.open("/data" + path, "r");
    server.streamFile(file, contentType);
    file.close();
    Serial.println("Served file: /data" + path);
    return;
  }
  
  // Finally try without leading slash
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
  
  // If not found, try with /data prefix
  if (SPIFFS.exists("/data/" + noLeadingSlash)) {
    File file = SPIFFS.open("/data/" + noLeadingSlash, "r");
    server.streamFile(file, contentType);
    file.close();
    Serial.println("Served file: /data/" + noLeadingSlash);
    return;
  }
  
  // File not found
  server.send(404, "text/plain", "File not found: " + path);
  Serial.println("File not found: " + path);

  // Debug: List all files in SPIFFS
  Serial.println("Files in SPIFFS:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.print("- ");
    Serial.println(file.name());
    file = root.openNextFile();
  }
}

// Request handlers
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

// API endpoint to get the last scanned card UID
void handleScan() {
  // Check if card UID has expired
  if (millis() - lastCardTime > CARD_RESET_TIME) {
    currentCardUID = "";
  }
  
  // Only return the current card UID if it's not empty
  if (currentCardUID != "") {
    server.send(200, "application/json", "{\"uid\":\"" + currentCardUID + "\", \"timestamp\":" + String(lastCardTime) + "}");
    
    // If we're in registration mode, reset the mode but keep the UID for this request
    if (currentMode != NORMAL) {
      currentMode = NORMAL;
    }
  } else {
    server.send(200, "application/json", "{\"uid\":\"\", \"timestamp\":0}");
  }
}

// API endpoint to clear the card UID
void handleClearCard() {
  currentCardUID = "";
  server.send(200, "text/plain", "Card cleared");
}

// API endpoint to set the RFID mode
void handleMode() {
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    if (mode == "user") {
      currentMode = NEW_USER;
      // Manually trigger the motion detection to start the scan
      motionDetected = true;
      server.send(200, "text/plain", "Mode set to new user");
    } else if (mode == "book") {
      currentMode = NEW_BOOK;
      // Manually trigger the motion detection to start the scan
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

// API endpoint to get users data
void handleGetUsers() {
  String usersData = loadFile("/users.json");
  server.send(200, "application/json", usersData);
}

// API endpoint to get books data
void handleGetBooks() {
  String booksData = loadFile("/books.json");
  server.send(200, "application/json", booksData);
}

// API endpoint to update users data
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

// API endpoint to update books data
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

// API endpoint to check if a book is borrowed
void handleCheckBorrowed() {
  if (server.hasArg("id")) {
    String bookId = server.arg("id");
    String booksData = loadFile("/books.json");
    
    // Parse the JSON data
    DynamicJsonDocument doc(8192);  // larger size to accommodate all books
    deserializeJson(doc, booksData);
    
    bool borrowed = false;
    JsonArray books = doc["books"].as<JsonArray>();
    
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
  Serial.begin(115200);
  Serial.println("Starting Library Management System");
  
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  Serial.println("SPIFFS mounted successfully");
  
  // List all files in SPIFFS
  Serial.println("Files in SPIFFS:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.print("- ");
    Serial.println(file.name());
    file = root.openNextFile();
  }
  
  // Initialize LCD
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Library System");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  // Initialize SPI and RFID
  SPI.begin();
  rfid.PCD_Init();
  delay(4);  // Let MFRC522 warm up
  Serial.println("RFID reader initialized");
  
  // Set up IR sensor - improved setup
  pinMode(IR_PIN, INPUT);
  // Make sure to properly set up the interrupt pin
  Serial.println("Setting up IR sensor interrupt on pin " + String(IR_PIN));
  attachInterrupt(digitalPinToInterrupt(IR_PIN), motionInterrupt, RISING);
  Serial.println("IR sensor interrupt set up");
  
  // Test interrupt by triggering it manually once
  motionDetected = true;
  Serial.println("Motion detection flag manually set for testing");
  
  // Create WiFi AP
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  // Set up scrolling text for LCD
  scrollText = "WiFi: " + String(ssid);
  lcd.clear();
  lcd.setCursor(0, 0);
  
  // If text fits on screen, just display it
  if (scrollText.length() <= 16) {
    lcd.print(scrollText);
  } else {
    // Otherwise, display the first part and start scrolling
    lcd.print(scrollText.substring(0, 16));
  }
  
  lcd.setCursor(0, 1);
  lcd.print("IP: " + IP.toString());
  
  // Initialize default data files if they don't exist
  if (!SPIFFS.exists("/users.json")) {
    Serial.println("Creating default users.json");
    saveFile("/users.json", "{\"users\":[{\"type\":\"staff\",\"username\":\"admin\",\"password\":\"admin123\",\"cardUid\":\"A286FF03\"},{\"type\":\"staff\",\"username\":\"staff1\",\"password\":\"staff123\",\"cardUid\":\"530D2349029380\"},{\"type\":\"student\",\"studentId\":\"S001\",\"password\":\"student123\",\"name\":\"John Smith\",\"email\":\"john.smith@example.com\",\"cardUid\":\"538426E2023F80\"},{\"type\":\"student\",\"studentId\":\"S002\",\"password\":\"student456\",\"name\":\"Emily Davis\",\"email\":\"emily.davis@example.com\",\"cardUid\":\"53ADFDCE028D80\"},{\"type\":\"student\",\"studentId\":\"S003\",\"password\":\"student789\",\"name\":\"David Johnson\",\"email\":\"david.johnson@example.com\",\"cardUid\":\"5395276302DC80\"}]}");
  }
  
  if (!SPIFFS.exists("/books.json")) {
    Serial.println("Creating default books.json");
    // Added an overdue book to demonstrate penalty, and separated location into shelf and floor
    saveFile("/books.json", "{\"books\":[{\"id\":\"B001\",\"isbn\":\"B001\",\"title\":\"Introduction to Programming\",\"author\":\"Jane Smith\",\"shelf\":\"R1C1\",\"floor\":\"1\",\"borrowed\":false,\"cardUid\":\"53C4734302A380\",\"history\":[]},{\"id\":\"B002\",\"isbn\":\"B002\",\"title\":\"Data Structures and Algorithms\",\"author\":\"Robert Johnson\",\"shelf\":\"R2C3\",\"floor\":\"1\",\"borrowed\":true,\"borrowedBy\":\"S001\",\"borrowDate\":\"2025-04-02T10:15:00Z\",\"returnDate\":\"2025-04-16T10:15:00Z\",\"cardUid\":\"53FC884B020880\",\"history\":[{\"username\":\"S001\",\"borrowDate\":\"2025-04-02T10:15:00Z\",\"returnDate\":null},{\"username\":\"S002\",\"borrowDate\":\"2025-03-01T14:30:00Z\",\"returnDate\":\"2025-03-14T11:45:00Z\"}]},{\"id\":\"B003\",\"isbn\":\"B003\",\"title\":\"Database Management Systems\",\"author\":\"Michael Chen\",\"shelf\":\"R3C2\",\"floor\":\"1\",\"borrowed\":false,\"cardUid\":\"53E5BCC6021280\",\"history\":[{\"username\":\"S003\",\"borrowDate\":\"2025-02-10T09:20:00Z\",\"returnDate\":\"2025-02-20T16:30:00Z\"}]},{\"id\":\"B004\",\"isbn\":\"B004\",\"title\":\"Computer Networks\",\"author\":\"Sarah Williams\",\"shelf\":\"R1C4\",\"floor\":\"2\",\"borrowed\":true,\"borrowedBy\":\"S002\",\"borrowDate\":\"2025-04-05T13:40:00Z\",\"returnDate\":\"2025-04-19T13:40:00Z\",\"cardUid\":\"53940740028780\",\"history\":[{\"username\":\"S002\",\"borrowDate\":\"2025-04-05T13:40:00Z\",\"returnDate\":null}]},{\"id\":\"B005\",\"isbn\":\"B005\",\"title\":\"Artificial Intelligence\",\"author\":\"David Brown\",\"shelf\":\"R2C1\",\"floor\":\"2\",\"borrowed\":false,\"cardUid\":\"53DD0760026780\",\"history\":[]},{\"id\":\"B006\",\"isbn\":\"B006\",\"title\":\"Operating Systems\",\"author\":\"Patricia Garcia\",\"shelf\":\"R3C3\",\"floor\":\"2\",\"borrowed\":false,\"cardUid\":\"53FCD8C402C580\",\"history\":[{\"username\":\"S001\",\"borrowDate\":\"2025-01-15T11:10:00Z\",\"returnDate\":\"2025-01-29T15:25:00Z\"}]},{\"id\":\"B007\",\"isbn\":\"B007\",\"title\":\"Software Engineering\",\"author\":\"Thomas Lee\",\"shelf\":\"R1C2\",\"floor\":\"3\",\"borrowed\":true,\"borrowedBy\":\"S003\",\"borrowDate\":\"2025-03-28T10:30:00Z\",\"returnDate\":\"2025-04-11T10:30:00Z\",\"cardUid\":\"530C524C02F680\",\"history\":[{\"username\":\"S003\",\"borrowDate\":\"2025-03-28T10:30:00Z\",\"returnDate\":null}]},{\"id\":\"B008\",\"isbn\":\"B008\",\"title\":\"Web Development\",\"author\":\"Lisa Johnson\",\"shelf\":\"R2C4\",\"floor\":\"3\",\"borrowed\":false,\"cardUid\":\"53BDECCB02E080\",\"history\":[]},{\"id\":\"B009\",\"isbn\":\"B009\",\"title\":\"Machine Learning Basics\",\"author\":\"Alan Turner\",\"shelf\":\"R4C1\",\"floor\":\"1\",\"borrowed\":true,\"borrowedBy\":\"S001\",\"borrowDate\":\"2025-03-15T10:30:00Z\",\"returnDate\":\"2025-03-29T10:30:00Z\",\"cardUid\":\"53ABCDEF02E080\",\"history\":[{\"username\":\"S001\",\"borrowDate\":\"2025-03-15T10:30:00Z\",\"returnDate\":null}]}]}");
  }

  // Create data directory if it doesn't exist
  if (!SPIFFS.exists("/data")) {
    SPIFFS.mkdir("/data");
  }
  
  // Copy HTML files to data directory if needed
  if (SPIFFS.exists("/index.html") && !SPIFFS.exists("/data/index.html")) {
    File sourceFile = SPIFFS.open("/index.html", "r");
    File destFile = SPIFFS.open("/data/index.html", "w");
    if (sourceFile && destFile) {
      destFile.print(sourceFile.readString());
    }
    sourceFile.close();
    destFile.close();
  }
  
  // Set up server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/admin.html", HTTP_GET, handleAdmin);
  server.on("/student.html", HTTP_GET, handleStudent);
  server.on("/books.html", HTTP_GET, handleBooks);
  server.on("/styles.css", HTTP_GET, handleCSS);
  server.on("/scripts.js", HTTP_GET, handleJS);
  
  // API endpoints
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/clear-card", HTTP_GET, handleClearCard);
  server.on("/api/mode", HTTP_GET, handleMode);
  server.on("/api/users", HTTP_GET, handleGetUsers);
  server.on("/api/books", HTTP_GET, handleGetBooks);
  server.on("/api/users", HTTP_POST, handleUpdateUsers);
  server.on("/api/books", HTTP_POST, handleUpdateBooks);
  server.on("/api/check-borrowed", HTTP_GET, handleCheckBorrowed);
  
  // Handle 404 (file not found) errors with a custom message
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
  
  // Start server
  server.begin();
  Serial.println("Web server started");
  Serial.println("Setup complete");
}

void loop() {
  server.handleClient();
  
  // Get current time
  unsigned long currentTime = millis();
  
  // If not in scanning mode, scroll the WiFi info on LCD
  if (!scanningActive) {
    scrollLcdText();
  }
  
  // Check motion sensor pin directly and print status occasionally
  static unsigned long lastPinCheck = 0;
  if (currentTime - lastPinCheck > 5000) {
    lastPinCheck = currentTime;
    int irValue = digitalRead(IR_PIN);
    Serial.print("IR Pin value: ");
    Serial.println(irValue);
  }
  
  // Handle motion detection - improved detection
  if (motionDetected && !scanningActive) {
    // Start a new scan window
    motionTimestamp = currentTime;
    scanningActive = true;
    
    // Clear card UID when starting new scan
    currentCardUID = "";
    
    // Notify on LCD and serial
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Motion detected");
    lcd.setCursor(0, 1);
    lcd.print("Scan in 5 sec...");
    Serial.println("Motion detected, ready to scan");
    
    // Reset motion flag for next detection
    motionDetected = false;
  }
  
  // Handle scanning countdown if active
  if (scanningActive) {
    // Calculate remaining time
    unsigned long elapsedTime = currentTime - motionTimestamp;
    
    // RFID scanning countdown
    if (elapsedTime < SCAN_TIMEOUT) {
      int remainingSeconds = 5 - (elapsedTime / 1000);
      
      // Update countdown once per second
      static int lastSecond = -1;
      if (remainingSeconds != lastSecond) {
        lastSecond = remainingSeconds;
        lcd.setCursor(0, 1);
        lcd.print("Scan in ");
        lcd.print(remainingSeconds);
        lcd.print(" sec...  ");
      }
      
      // Only scan for cards during this 5-second window
      if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        // Save the card UID and timestamp
        currentCardUID = getUIDString(&rfid.uid);
        lastCardTime = currentTime;
        
        // Display on LCD
        lcd.clear();
        lcd.setCursor(0, 0);
        
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
        
        lcd.setCursor(0, 1);
        lcd.print("UID: " + currentCardUID);
        
        // End scan session
        scanningActive = false;
        
        // Stop RFID
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        
        // Show success message for a moment
        delay(2000);
        
        // Return to scrolling display
        scrollPosition = 0;
      }
    } else {
      // Timeout reached
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Scan timeout");
      lcd.setCursor(0, 1);
      lcd.print("Try again");
      
      // End scan session
      scanningActive = false;
      delay(2000); // Show timeout message
      
      // Reset scroll position
      scrollPosition = 0;
    }
  }
  
  delay(50);
}