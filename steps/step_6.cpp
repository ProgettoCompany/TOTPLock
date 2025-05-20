#include <qrcode.h>
#include <Arduino.h>
#include "RTClib.h"
#include "sha1.h"
#include "TOTP.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// Define ST7789 display pin connection
#define TFT_CS     10   
#define TFT_RST     8    
#define TFT_DC      9
#define ST77XX_GREY 0x7BEF

// Initialize the ST7789 display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

RTC_DS3231 rtc;

// The shared secret is shTGPxibDo (feel free to change it using https://www.lucadentella.it/OTP/)
uint8_t hmacKey[] = {0x73, 0x68, 0x54, 0x47, 0x50, 0x78, 0x69, 0x62, 0x44, 0x6f};

// Calibrated thresholds for the keypad
int myThresholds[16] = {6, 84, 152, 207, 252, 297, 337, 373, 400, 430, 457, 482, 501, 522, 542, 560};

TOTP totp = TOTP(hmacKey, 10);

// Function prototypes
void displayTOTPQRCode();
void base32Encode(const uint8_t* data, int dataLength, char* result, int bufSize);
void printTextCentered(char* text, int y, uint8_t textSize, uint16_t color);
void printTextCentered(const __FlashStringHelper* text, int y, uint8_t textSize, uint16_t color);

void setup() {
  Serial.begin(115200);

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
  
  // Display QR code
  displayTOTPQRCode();
}

void loop() {
  // Print the current time as a unix timestamp and the TOTP code
  DateTime now = rtc.now();
  long GMT = now.unixtime();
  char* currentCode = totp.getCode(GMT);
  Serial.print(F("Current TOTP: "));
  Serial.println(currentCode);
  Serial.print(F("Current Unix Time: "));
  Serial.println(GMT);
  delay(1000); // Update every second 
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