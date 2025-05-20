# TOTP Lock

A TOTP-based physical access control system using an Arduino-compatible microcontroller, TFT display, solenoid lock, RTC, and analog keypad. Users authenticate by entering a 6-digit TOTP generated from a shared secret, compatible with apps like Google Authenticator.

**Build one yourself by following the [Instructables](https://www.instructables.com/Arduino-Time-Based-One-Time-Door-Password-Lock-Use/)!**

## Features

* 6-digit time-based one-time password (TOTP) authentication
* Solenoid lock control
* 240x240 TFT screen with dynamic UI
* QR code display for easy TOTP setup
* EEPROM-based timezone storage and setup
* One Pin Keypad for user input, allows for 16 keys with one pin!
* Real-time clock (RTC) timekeeping

## Hardware Used

* Arduino-compatible microcontroller
* Adafruit ST7789 240x240 TFT display
* DS3231 RTC module
* Analog (resistor-ladder) keypad
* Solenoid lock (with driver circuit)
* EEPROM (onboard)
* Optional: enclosure and keypad overlay

## Libraries

* [Adafruit\_GFX](https://github.com/adafruit/Adafruit-GFX-Library)
* [Adafruit\_ST7789](https://github.com/adafruit/Adafruit-ST7735-Library)
* [RTClib](https://github.com/adafruit/RTClib)
* [TOTP](https://github.com/lucadentella/TOTP)
* [QRCode](https://github.com/ricmoo/QRCode)
* [OnePinKeypad](https://github.com/ProgettoCompany/Progetto_One_Pin_Keypad_Arduino_Library)
* [sha1](https://github.com/PaulStoffregen/sha1)

## Setup

1. Flash the code to the microcontroller.
2. On boot, scan the QR code with a TOTP app (e.g. Google Authenticator).
3. Use the app-generated 6-digit code on the keypad.
4. Press `A` to enter timezone setup (for displayed time only):

   * `B`: +30 min
   * `C`: -30 min
   * `D`: Save & Exit

## Security

* TOTP secret is hardcoded for demonstration, given hardware access to the device, one could extract the secret or change the RTC time for a replay attack or just power the solenoid lock directly.
* EEPROM is used to store timezone, not the secret.
* Consider using encrypted storage and dynamic key provisioning for production use in a tamper-proof enclosure that prevents hardware access and the use of a magnet to bypass the solenoid lock.
