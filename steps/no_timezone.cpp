#include <qrcode.h>
#include <Arduino.h>
#include "RTClib.h"
#include "sha1.h"
#include "TOTP.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <OnePinKeypad.h>

// Define ST7789 display pin connection
#define TFT_CS     10   
#define TFT_RST     8    
#define TFT_DC      9
#define ST77XX_GREY 0x7BEF

// Define Analog Pin for keypad
#define KEYPAD_PIN A0
#define NO_KEY '\0'

#define SOLENOID_PIN 3 // Pin for the solenoid lock

// Initialize the ST7789 display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Create a keypad object
OnePinKeypad keypad(KEYPAD_PIN);

RTC_DS3231 rtc;

// The shared secret is MyLegoDoor
uint8_t hmacKey[] = {0x4d, 0x79, 0x4c, 0x65, 0x67, 0x6f, 0x44, 0x6f, 0x6f, 0x72};

// Calibrated thresholds for the keypad
int myThresholds[16] = {6, 84, 152, 207, 252, 297, 337, 373, 400, 430, 457, 482, 501, 522, 542, 560};

TOTP totp = TOTP(hmacKey, 10);

int lastHourDisplayed = -1; // Last hour displayed
int lastMinuteDisplayed = -1; // Last minute displayed

// Variables for user code entry
char enteredCode[7] = ""; // Buffer for entered code (6 digits + null terminator)
int codeIndex = 0; // Current position in the entered code
bool codeVerified = false; // Whether the code has been verified
unsigned long codeEntryStartTime = 0; // When the user started entering a code
const unsigned long CODE_ENTRY_TIMEOUT = 10000; // 10 seconds to enter code
unsigned long lastTimeUpdate = 0; // For tracking time updates

// Function prototypes
void displayDefaultScreen();
void displayTOTPQRCode();
void base32_encode(const uint8_t* data, int dataLength, char* result, int bufSize);
void handleKeypadInput();
void verifyCode();
void displayCodeEntry();
void displayVerificationResult(bool success);
void displayTime();
void printTextCentered(char* text, int y, uint8_t textSize, uint16_t color);
void printTextCentered(const __FlashStringHelper* text, int y, uint8_t textSize, uint16_t color);

void setup() {
  Serial.begin(115200);
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW); // Ensure solenoid is off at startup

  if (!rtc.begin()) {
    Serial.println(F("Couldn't find RTC"));
    Serial.flush();
    while (1) delay(10);
  }
  
  // Initialize the ST7789 TFT display
  tft.init(240, 240, SPI_MODE3);
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);
  
  Serial.println(F("Display initialized"));
  keypad.useCalibratedThresholds(myThresholds);
  
  // Display QR code for 5 seconds
  displayTOTPQRCode();
  delay(5000);
  
  tft.fillScreen(ST77XX_BLACK);
  displayDefaultScreen();
}

void loop() {
  if (!codeVerified) {
    displayTime();
    handleKeypadInput();
  }
  
  unsigned long currentMillis = millis();
  // Check for timeout during code entry
  if (codeIndex > 0 && currentMillis - codeEntryStartTime > CODE_ENTRY_TIMEOUT) {
    // Reset code entry due to timeout
    codeIndex = 0;
    enteredCode[0] = '\0';
    tft.fillScreen(ST77XX_BLACK);
    displayDefaultScreen();
  }
  
  // Reset verification status after 3 seconds
  if (codeVerified && currentMillis - codeEntryStartTime > 3000) {
    Serial.println(F("Resetting verification status..."));
    codeVerified = false;
    digitalWrite(SOLENOID_PIN, LOW); // Deactivate solenoid lock
    tft.fillScreen(ST77XX_BLACK);
    displayDefaultScreen();
  }
}

void displayTime() {
  DateTime now = rtc.now();
  
  // Format time as 00:00PM
  int hour12 = now.hour() % 12;
  if (hour12 == 0) hour12 = 12;  // Adjust for 12 AM/PM

  if (now.hour() != lastHourDisplayed || now.minute() != lastMinuteDisplayed) {
    lastHourDisplayed = now.hour();
    lastMinuteDisplayed = now.minute();
    
    // Update time display
    char timeStr[9]; // Buffer for time string (format: 00:00PM\0)
    sprintf(timeStr, "%d:%02d%s", 
            hour12, 
            now.minute(), 
            now.hour() >= 12 ? "PM" : "AM");
    
    // Update just the time portion without redrawing the entire screen
    tft.fillRect(80, 10, 140, 20, ST77XX_BLACK); // Clear time area
    printTextCentered(timeStr, 10, 2, ST77XX_CYAN);
  }
}

void displayDefaultScreen() {
  tft.fillScreen(ST77XX_BLACK);
  
  lastHourDisplayed = -1; // Reset last hour
  lastMinuteDisplayed = -1; // Reset last minute
  displayTime();
  
  displayCodeEntry();
}

void handleKeypadInput() {
  char keyValue = keypad.readKeypadWithTimeout(50);
  
  // If no key pressed, return
  if (keyValue == NO_KEY) {
    return;
  }
  
  // Key pressed - handle it
  Serial.print(F("Key pressed: "));
  Serial.println(keyValue);
  
  // Start tracking time for the first key press
  if (codeIndex == 0) {
    codeEntryStartTime = millis();
  }
  
  // Handle numeric input
  if (keyValue >= '0' && keyValue <= '9' && codeIndex < 6) {
    enteredCode[codeIndex++] = keyValue;
    enteredCode[codeIndex] = '\0';
  } 
  else if (keyValue == '*') {
    // Clear entry
    codeIndex = 0;
    enteredCode[0] = '\0';
  }
  displayCodeEntry();

  // Verify code when all 6 digits are entered
  if (codeIndex == 6) {
      verifyCode();
    }
}

void verifyCode() {
  // Get current TOTP code
  DateTime now = rtc.now();
  long GMT = now.unixtime();
  char* currentCode = totp.getCode(GMT);
  
  // Compare entered code with current TOTP code
  bool success = (strcmp(enteredCode, currentCode) == 0);
  
  Serial.print(F("Entered code: "));
  Serial.println(enteredCode);
  Serial.print(F("Current TOTP: "));
  Serial.println(currentCode);
  Serial.print(F("Verification: "));
  Serial.println(success ? F("SUCCESS") : F("FAILED"));
  
  // Display result
  displayVerificationResult(success);
  
  // Reset code entry
  codeIndex = 0;
  enteredCode[0] = '\0';
  codeVerified = true;
  codeEntryStartTime = millis();
}

void displayCodeEntry() {
  tft.fillRect(0, 120, 200, 160, ST77XX_BLACK);
  
  // Display prompt
  printTextCentered(F("Enter Code:"), 50, 2, ST77XX_WHITE);
  
  // Display entered code so far
  tft.setTextSize(4);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(45, 120);
  tft.println(enteredCode);
  
  // Add placeholder underscores for remaining digits
  tft.setTextSize(4);
  tft.setTextColor(ST77XX_GREY);
  for (int i = codeIndex; i < 6; i++) {
    tft.setCursor(45 + i * 24, 120);
    tft.print("_");
  }
  
  printTextCentered(F("Press * to clear"), 200, 2, ST77XX_GREEN);
}

void displayVerificationResult(bool success) {
  tft.fillScreen(ST77XX_BLACK);
  
  if (success) {
    printTextCentered(F("ACCESS"), 100, 3, ST77XX_GREEN);
    printTextCentered(F("GRANTED"), 130, 3, ST77XX_GREEN);
    digitalWrite(SOLENOID_PIN, HIGH); // Activate solenoid lock
  } else {
    printTextCentered(F("ACCESS"), 100, 3, ST77XX_RED);
    printTextCentered(F("DENIED"), 130, 3, ST77XX_RED);
  }
}

void displayTOTPQRCode() {
  QRCode qrcode;
  
  // Define the size of the QR code (1-40, higher means bigger size)
  uint8_t version = 4;  // Using a larger version for TOTP URI
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  
  // Create TOTP URI for Google Authenticator
  // Format: otpauth://totp/Label:User?secret=SECRET&issuer=Issuer
  char secret[20];
  base32_encode(hmacKey, 10, secret, 20); // Convert HMAC to Base32 for the URI
  
  char uri[100];
  sprintf(uri, "otpauth://totp/Door:Lock?secret=%s&issuer=TOTPLock", secret);
  
  qrcode_initText(&qrcode, qrcodeData, version, 0, uri);
  
  // Clear the screen
  tft.fillScreen(ST77XX_BLACK);
  
  // Calculate the scale factor and position for centering
  int scale = 4;  // Scale factor for the QR modules
  int qrSize = qrcode.size * scale;
  int xOffset = (240 - qrSize) / 2;
  int yOffset = (240 - qrSize) / 2;
  
  // Draw the QR code
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        tft.fillRect(xOffset + x * scale, yOffset + y * scale, scale, scale, ST77XX_WHITE);
      }
    }
  }
  
  printTextCentered(F("Scan with Auth App"), 20, 2, ST77XX_CYAN);
}

// Base32 encoding function for TOTP secret
void base32_encode(const uint8_t* data, int dataLength, char* result, int bufSize) {

  const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  int resultIndex = 0;
  int bits = 0;
  int value = 0;
  
  for (int i = 0; i < dataLength; i++) {
    value = (value << 8) | data[i];
    bits += 8;
    
    while (bits >= 5) {
      if (resultIndex < bufSize - 1) {
        result[resultIndex++] = chars[(value >> (bits - 5)) & 0x1F];
      }
      bits -= 5;
    }
  }
  
  // If we have remaining bits
  if (bits > 0 && resultIndex < bufSize - 1) {
    result[resultIndex++] = chars[(value << (5 - bits)) & 0x1F];
  }
  
  // Null terminate
  result[resultIndex] = 0;
}


void printTextCentered(char* text, int y, uint8_t textSize, uint16_t color) {
  // Calculate text width (each character in default font is 6 pixels wide at size 1)
  int textWidth = strlen(text) * 6 * textSize;
  int centerX = (tft.width() - textWidth) / 2;
  
  tft.setTextSize(textSize);
  tft.setTextColor(color);
  tft.setCursor(centerX, y);
  tft.println(text);
}

void printTextCentered(const __FlashStringHelper* text, int y, uint8_t textSize, uint16_t color) {
  // Flash strings need to be handled differently
  PGM_P p = reinterpret_cast<PGM_P>(text);
  size_t len = 0;
  while (pgm_read_byte(p+len) != 0) len++;
  
  // Calculate text width
  int textWidth = len * 6 * textSize;
  int centerX = (tft.width() - textWidth) / 2;
  
  tft.setTextSize(textSize);
  tft.setTextColor(color);
  tft.setCursor(centerX, y);
  tft.println(text);
}