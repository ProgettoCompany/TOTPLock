#include <qrcode.h>
#include <Arduino.h>
#include "RTClib.h"
#include "sha1.h"
#include "TOTP.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <OnePinKeypad.h>
#include <EEPROM.h>

// Define ST7789 display pin connection
#define TFT_CS     10   
#define TFT_RST     8    
#define TFT_DC      9
#define ST77XX_GREY 0x7BEF

// Define Analog Pin for keypad
#define KEYPAD_PIN A0
#define NO_KEY '\0'

#define SOLENOID_PIN 3 // Pin for the solenoid lock

// EEPROM Storage definitions for storing timezone offset
#define EEPROM_MAGIC_MARKER "TOTP"  // 4-byte marker to verify EEPROM has been initialized
#define EEPROM_MAGIC_ADDR 0         // Starting address for magic marker
#define EEPROM_TZ_ADDR 4  

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Create a keypad object
OnePinKeypad keypad(KEYPAD_PIN);

// Calibrated thresholds for the keypad
int myThresholds[16] = {6, 84, 152, 207, 252, 297, 337, 373, 400, 430, 457, 482, 501, 522, 542, 560};

RTC_DS3231 rtc;

// The shared secret is shTGPxibDo (feel free to change it using https://www.lucadentella.it/OTP/)
uint8_t hmacKey[] = {0x73, 0x68, 0x54, 0x47, 0x50, 0x78, 0x69, 0x62, 0x44, 0x6f, 0x63, 0x33, 0x51, 0x39, 0x54, 0x36};

TOTP totp = TOTP(hmacKey, 10);

// Variables to keep track of the last time displayed
// This is used to avoid redrawing the the time if it hasn't changed 
int lastHourDisplayed = -1;
int lastMinuteDisplayed = -1;

// Variables for user code entry
char enteredCode[7] = ""; // Buffer for entered code (6 digits + null terminator)
int codeIndex = 0; // Current position in the entered code
bool codeVerified = false; // Whether the code has been verified
unsigned long codeEntryStartTime = 0; // When the user started entering a code
const unsigned long CODE_ENTRY_TIMEOUT = 10000; // 10 seconds to enter code
unsigned long lastTimeUpdate = 0; // For tracking time updates

// Timezone configuration
int8_t timezoneOffset = 0; // Timezone offset in half-hours
bool inTimezoneSetup = false; // Whether we're currently in timezone setup mode

// Function prototypes
void displayDefaultScreen();
void displayTOTPQRCode();
void base32Encode(const uint8_t* data, int dataLength, char* result, int bufSize);
void handleKeypadInput();
void verifyCode();
void displayCodeEntry();
void displayVerificationResult(bool success);
void displayTime();
void printTextCentered(char* text, int y, uint8_t textSize, uint16_t color);
void printTextCentered(const __FlashStringHelper* text, int y, uint8_t textSize, uint16_t color);
void enterTimezoneSetup();
void handleTimezoneInput(char keyValue);
void saveTimezoneToEEPROM();
void loadTimezoneFromEEPROM();
void initializeEEPROM();
bool isEEPROMInitialized();
void displayTimezoneSetup();

void setup() {
  Serial.begin(115200);
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW); // Ensure solenoid is off at startup

  // Initialize EEPROM and load timezone if available
  if (!isEEPROMInitialized()) {
    Serial.println(F("Initializing EEPROM"));
    initializeEEPROM();
  } else {
    loadTimezoneFromEEPROM();
  }

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
  if (inTimezoneSetup) {
    // In timezone setup mode
    char keyValue = keypad.readKeypadWithTimeout(50);
    if (keyValue != NO_KEY) {
      handleTimezoneInput(keyValue);
    }
  } else {
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
    
    // Reset verification status after 3 seconds (go back to locked state)
    if (codeVerified && currentMillis - codeEntryStartTime > 3000) {
      Serial.println(F("Resetting verification status..."));
      codeVerified = false;
      digitalWrite(SOLENOID_PIN, LOW); // Deactivate solenoid lock
      tft.fillScreen(ST77XX_BLACK);
      displayDefaultScreen();
    }
  }
  delay(10);
}

/**
 * Display the current time
 * This function retrieves the current time from the RTC and formats it for display.
 * It also applies the timezone offset to adjust the time accordingly.
 */
void displayTime() {
  // Apply timezone offset (stored in half-hours) converted to seconds
  long secondsOffset = timezoneOffset * 30 * 60; // half-hours to seconds
  
  // Apply the offset to the timestamp and create a new DateTime
  TimeSpan offset(secondsOffset);
  DateTime adjusted = rtc.now() + offset;
  
  // Extract the adjusted time components
  int adjustedHour = adjusted.hour();
  int adjustedMinute = adjusted.minute();
  
  // Format time as 00:00PM
  int hour12 = adjustedHour % 12;
  if (hour12 == 0) hour12 = 12;  // Adjust for 12 AM/PM

  if (adjustedHour != lastHourDisplayed || adjustedMinute != lastMinuteDisplayed) {
    lastHourDisplayed = adjustedHour;
    lastMinuteDisplayed = adjustedMinute;
    
    // Update time display
    char timeStr[9]; // Buffer for time string (format: 00:00PM\0)
    sprintf(timeStr, "%d:%02d%s", 
            hour12, 
            adjustedMinute, 
            adjustedHour >= 12 ? "PM" : "AM");
    
    // Update just the time portion without redrawing the entire screen
    tft.fillRect(80, 10, 140, 20, ST77XX_BLACK); // Clear time area
    printTextCentered(timeStr, 10, 2, ST77XX_CYAN);
  }
}

/**
 * Display Default Screen
 * This function clears the screen and and shows the current time.
 * It also displays the code entry prompt. The last displayed hour
 * and minute are reset to ensure the time is updated correctly. This
 * function is called at startup and after code verification.
 */
void displayDefaultScreen() {
  tft.fillScreen(ST77XX_BLACK);
  
  lastHourDisplayed = -1; // Reset last hour
  lastMinuteDisplayed = -1; // Reset last minute
  displayTime();
  
  displayCodeEntry();
}

/**
 * Handle keypad input
 * This function reads the keypad and processes the input.
 */
void handleKeypadInput() {
  char keyValue = keypad.readKeypadWithTimeout(50);
  
  // If no key pressed, return
  if (keyValue == NO_KEY) {
    return;
  }
  
  // Key pressed - handle it
  Serial.print(F("Key pressed: "));
  Serial.println(keyValue);
  
  // Check for 'A' key for timezone setup
  if (keyValue == 'A') {
    enterTimezoneSetup();
    return;
  }
  
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

/**
 * Verify the entered code
 * This function checks if the entered code matches the current TOTP code.
 * If it matches, access is granted and the solenoid lock is activated.
 */
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

/**
 * Display the code entry screen
 * This function shows the user the code they are entering.
 * It also provides instructions for clearing the entry
 * and setting the timezone.
 */
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
  
  printTextCentered(F("Press * to clear"), 180, 2, ST77XX_GREEN);
  printTextCentered(F("A = Set Timezone"), 200, 2, ST77XX_YELLOW);
}

/**
 * Display the verification result
 * This function shows whether the access was granted or denied.
 * It also activates the solenoid lock if access is granted.
 * @param success True if access is granted, false if denied
 */
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

/**
 * Enter timezone setup mode
 * This function is called when the user presses the 'A' key.
 */
void enterTimezoneSetup() {
  inTimezoneSetup = true;
  displayTimezoneSetup();
}

/**
 * Display the timezone setup screen
 * This function shows the current timezone and allows the user to adjust it.
 * The user can increase or decrease the timezone offset by 30 minutes using
 * the keypad. The user can also save the changes and exit the setup.
 */
void displayTimezoneSetup() {
  tft.fillScreen(ST77XX_BLACK);
  
  printTextCentered(F("TIMEZONE SETUP"), 20, 2, ST77XX_CYAN);
  
  // Show current timezone
  char currentTZ[9];
  
  float tzFloat = timezoneOffset / 2.0;
  if (tzFloat == 0.0) {
    sprintf(currentTZ, "UTC");
  } else {
    char tempTZ[6]; // Buffer to store the offset value
    dtostrf(tzFloat, 2, (timezoneOffset % 2 == 0) ? 0 : 1, tempTZ);
    if (tzFloat > 0) {
      sprintf(currentTZ, "UTC+%s", tempTZ);
    } else {
      sprintf(currentTZ, "UTC%s", tempTZ);
    }
  }
  
  // Display current timezone in large font
  printTextCentered(currentTZ, 100, 3, ST77XX_WHITE);
  
  // Display instructions
  printTextCentered(F("B: +30min"), 160, 2, ST77XX_GREEN);
  printTextCentered(F("C: -30min"), 180, 2, ST77XX_RED);
  printTextCentered(F("D: Save & Exit"), 200, 2, ST77XX_YELLOW);
}

/**
 * Handle timezone input from keypad
 * @param keyValue The key pressed
 */
void handleTimezoneInput(char keyValue) {
  if (keyValue == 'D') {
    // Save and exit timezone setup
    saveTimezoneToEEPROM();
    inTimezoneSetup = false;
    tft.fillScreen(ST77XX_BLACK);
    displayDefaultScreen();
    return;
  }
  
  if (keyValue == 'B') {
    Serial.println(F("Increasing timezone offset"));
    // Increase timezone offset by 30 minutes (1 half-hour)
    timezoneOffset++;
    // Limit to reasonable range (UTC+14)
    if (timezoneOffset > 28) timezoneOffset = 28;
    displayTimezoneSetup();
  }
  else if (keyValue == 'C') {
    Serial.println(F("Decreasing timezone offset"));
    // Decrease timezone offset by 30 minutes (1 half-hour)
    timezoneOffset--;
    // Limit to reasonable range (UTC-12)
    if (timezoneOffset < -24) timezoneOffset = -24;
    displayTimezoneSetup();
  }
}

/**
 * Check if EEPROM is initialized with the magic marker
 * @return true if EEPROM is initialized, false otherwise
 */
bool isEEPROMInitialized() {
  for (int i = 0; i < 4; i++) {
    if (EEPROM.read(EEPROM_MAGIC_ADDR + i) != EEPROM_MAGIC_MARKER[i]) {
      return false;
    }
  }
  return true;
}

/**
 * Initialize EEPROM with a magic marker and default timezone
 */
void initializeEEPROM() {
  // Write the magic marker
  for (int i = 0; i < 4; i++) {
    EEPROM.write(EEPROM_MAGIC_ADDR + i, EEPROM_MAGIC_MARKER[i]);
  }
  
  // Set default timezone to UTC+0
  EEPROM.write(EEPROM_TZ_ADDR, 0);
  timezoneOffset = 0;
}

/**
 * Load the timezone offset from EEPROM
 */
void loadTimezoneFromEEPROM() {
  // Read timezone value (as signed byte)
  timezoneOffset = (int8_t)EEPROM.read(EEPROM_TZ_ADDR);
  
  Serial.print(F("Loaded timezone offset: "));
  Serial.print(timezoneOffset / 2.0);
  Serial.println(F(" hours"));
}

/**
 * Save the timezone offset to EEPROM
 */
void saveTimezoneToEEPROM() {
  EEPROM.write(EEPROM_TZ_ADDR, (uint8_t)timezoneOffset);
  
  Serial.print(F("Saved timezone offset: "));
  Serial.print(timezoneOffset / 2.0);
  Serial.println(F(" hours"));
}

/**
 * Display the TOTP QR code on the TFT screen
 */
void displayTOTPQRCode() {
  QRCode qrcode;
  
  // Define the size of the QR code (1-40, higher means bigger size)
  uint8_t version = 4;  // Using a larger version for TOTP URI
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  
  // Create TOTP URI for Google Authenticator
  // Format: otpauth://totp/Label:User?secret=SECRET&issuer=Issuer
  char secret[20];
  base32Encode(hmacKey, 10, secret, 20); // Convert HMAC to Base32 for the URI
  
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

/**
 * Base32 encoding function for TOTP secret
 * @param data The data to encode
 * @param dataLength The length of the data
 * @param result The buffer to store the encoded result
 * @param bufSize The size of the result buffer
 */
void base32Encode(const uint8_t* data, int dataLength, char* result, int bufSize) {

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

/**
 * Print text centered on the TFT screen
 * @param text The text to print
 * @param y The y-coordinate for the text
 * @param textSize The size of the text
 * @param color The color of the text
 */
void printTextCentered(char* text, int y, uint8_t textSize, uint16_t color) {
  // Calculate text width (each character in default font is 6 pixels wide at size 1)
  int textWidth = strlen(text) * 6 * textSize;
  int centerX = (tft.width() - textWidth) / 2;
  
  tft.setTextSize(textSize);
  tft.setTextColor(color);
  tft.setCursor(centerX, y);
  tft.println(text);
}

/**
 * Print text centered on the TFT screen
 * @param text The text to print (can be a flash string)
 * @param y The y-coordinate for the text
 * @param textSize The size of the text
 * @param color The color of the text
 */
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