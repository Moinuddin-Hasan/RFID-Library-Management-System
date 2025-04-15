# RFID Library Management System

## Overview
An IoT-based Library Management System built using ESP32 and RFID technology. This project creates a self-service library kiosk that allows users to borrow and return books using RFID cards, keep track of borrowed items, and manage the library inventory through a web interface.

## Features

### Hardware Features
- **RFID Authentication**: Students and staff can login using RFID cards
- **Book Scanning**: Books are tagged with unique RFID cards for easy tracking
- **Motion Detection**: IR sensor detects nearby motion to activate the RFID scanner
- **LCD Display**: Shows system status and information to users
- **WiFi Hotspot**: ESP32 creates its own WiFi access point for connectivity

### Software Features
- **User Management**:
  - Multiple account types (Student/Staff)
  - RFID card registration
  - Account creation and management
  
- **Book Management**:
  - Add, remove, and track books in the library
  - Lending history tracking
  - Book location tracking (shelf and floor)
  
- **Student Portal**:
  - View borrowed books
  - Check return dates
  - View borrowing history
  - Borrow new books
  
- **Admin Portal**:
  - User account management
  - Book catalog management
  - System monitoring
  
- **Automated Features**:
  - Due date calculation
  - Overdue penalty calculation
  - Auto-timeout for RFID scanning

## Hardware Requirements

- ESP32 Development Board
- MFRC522 RFID Reader/Writer
- RFID Cards/Tags
- 16x2 I2C LCD Display
- IR Motion Sensor
- Connecting wires
- Power supply
- Optional: enclosure for the system

## Pin Connections

| Component    | ESP32 Pin   |
|--------------|-------------|
| RFID RST     | GPIO 16     |
| RFID SS (SDA)| GPIO 5      |
| RFID MOSI    | GPIO 23     |
| RFID MISO    | GPIO 19     |
| RFID SCK     | GPIO 18     |
| IR Sensor    | GPIO 4      |
| LCD SDA      | GPIO 21     |
| LCD SCL      | GPIO 22     |

## Software Requirements

- PlatformIO or Arduino IDE
- Libraries:
  - MFRC522 (for RFID functionality)
  - ArduinoJson (for JSON parsing)
  - LiquidCrystal_I2C (for LCD control)
  - ESP32 WiFi library
  - WebServer library
  - SPIFFS (for file storage)

## Installation

### 1. Hardware Setup
1. Connect all components according to the pin mapping table above.
2. Power the ESP32 using a stable 5V power supply.

### 2. Software Setup

#### Using PlatformIO (Recommended)
1. Clone this repository:
   ```
   git clone https://github.com/yourusername/library-management-system.git
   ```
2. Open the project in PlatformIO IDE.
3. Upload the filesystem image (SPIFFS data):
   ```
   pio run --target uploadfs
   ```
4. Build and upload the project:
   ```
   pio run --target upload
   ```

#### Using Arduino IDE
1. Download the project as a ZIP file and extract it.
2. Install the required libraries using the Library Manager.
3. Install the ESP32 board using the Board Manager.
4. Install the ESP32 Filesystem Uploader plugin.
5. Select the ESP32 Dev Module board.
6. Upload the SPIFFS data using the "ESP32 Sketch Data Upload" tool.
7. Compile and upload the sketch.

## Usage

### 1. Initial Setup
1. Power on the system.
2. Connect to the WiFi access point named "Library Kiosk 1" (no password required).
3. Open a web browser and navigate to the IP address shown on the LCD display (typically 192.168.4.1).
4. Default admin credentials:
   - Username: Admin
   - Password: admin123

### 2. User Login
- **Staff Login**: Enter username and password on the login page.
- **Student Login**: Enter student ID and password on the login page.
- **RFID Login**: Place your registered RFID card on the reader when prompted by the motion sensor.

### 3. Borrowing a Book
1. Log in as a student.
2. Navigate to the "Borrow a Book" section.
3. Scan the book's RFID card.
4. The system will register the book as borrowed for 14 days.

### 4. Returning a Book
1. From the main page, click on "Return Book".
2. Scan the book's RFID card.
3. The system will register the book as returned.

### 5. Adding New Books (Admin Only)
1. Log in as an admin.
2. Navigate to the "Add Book" tab.
3. Fill in the book details.
4. Click "Scan Book Card" and scan an RFID card to associate with the book.
5. Click "Add Book" to save the book to the system.

### 6. Adding New Users (Admin Only)
1. Log in as an admin.
2. Navigate to the "Create Account" tab.
3. Select the account type (Student or Staff).
4. Fill in the user details.
5. Click "Scan Card" and scan an RFID card to associate with the user.
6. Click "Create Account" to save the user to the system.

## File Structure

```
├── data/                  # Web interface files
│   ├── index.html         # Login page
│   ├── admin.html         # Admin dashboard
│   ├── student.html       # Student dashboard
│   ├── books.html         # Book details page
│   ├── styles.css         # CSS styles
│   ├── scripts.js         # JavaScript code
│   ├── users.json         # User database
│   └── books.json         # Book database
├── src/                   # Source code
│   └── main.cpp           # Main Arduino code
├── platformio.ini         # PlatformIO configuration
└── README.md              # This file
```

## Security Considerations

- This system uses a simple username/password authentication and local storage.
- For increased security in production environments, consider:
  - Adding encryption to the stored JSON files
  - Implementing secure hash algorithms for passwords
  - Adding SSL/TLS for web communications
  - Using a more robust database solution

## Limitations

- The system currently supports only a local database stored in the ESP32 SPIFFS memory.
- WiFi range is limited to the ESP32's built-in antenna.
- The system can handle a limited number of books and users due to ESP32 memory constraints.

## Future Enhancements

- Cloud database integration
- Multiple RFID readers for larger libraries
- Barcode scanner support for ISBN lookup
- Email notifications for due dates
- Image uploads for book covers
- Mobile app integration

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgements

- Thanks to all the library administrators who provided input on the system requirements.
- Special thanks to the open-source community for the excellent libraries that made this project possible.
