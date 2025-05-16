#include <Arduino.h>
// Retrieve the unix timestamp from https://www.unixtimestamp.com/

// Date and time functions using a DS3231 RTC connected via I2C and Wire lib
// based on ds3231.ino example from the RTClib library
#include "RTClib.h"

RTC_DS3231 rtc;

String inputString = "";      // String to store incoming serial data
boolean stringComplete = false;  // Flag for completed string

void printTime(); // Function prototype

void setup () {
  Serial.begin(57600);

#ifndef ESP8266
  while (!Serial); // wait for serial port to connect. Needed for native USB
#endif

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }
  
  Serial.println("Current time information:");
  printTime();
  Serial.println("Send Unix timestamp to set RTC time (seconds since 1970-01-01 00:00:00 UTC)");
  Serial.println("Find the unix timestamp at https://www.unixtimestamp.com/");
  Serial.println("Example: 1694563200 (2023-09-13 00:00:00 UTC)");
}

void loop () {
  // Check if there's serial data available and process it
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n' || inChar == '\r') {
      stringComplete = true;
    } else {
      inputString += inChar;
    }
  }
  
  // Process completed input
  if (stringComplete) {
    if (inputString.length() > 0) {
      // Try to convert input to a Unix timestamp
      uint32_t newTime = inputString.toInt();
      if (newTime > 1600000000) { // Basic sanity check (time after 2020-09-13)
        // Set the RTC using the Unix timestamp
        rtc.adjust(DateTime(newTime));
        Serial.print("RTC time set to: ");
        printTime();
      } else {
        Serial.println("Invalid timestamp. Please enter Unix time (seconds since 1970-01-01)");
      }
    }
    
    // Clear input for next command
    inputString = "";
    stringComplete = false;
  }

  // Display current time every second
  static unsigned long lastPrintTime = 0;
  if (millis() - lastPrintTime >= 1000) {
    printTime();
    lastPrintTime = millis();
  }
}

void printTime() {
  DateTime now = rtc.now();

  Serial.print("Unix timestamp: ");
  Serial.println(now.unixtime());
  
  Serial.println();
}