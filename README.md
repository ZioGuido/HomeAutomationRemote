# HomeAutomationRemote
Remote control for an Home Automation System


Remote controller project for a personal Home Automation System
Code by Guido Scognamiglio

Based on Arduino MKR 1010 WiFi and Zihatec Arduitouch MKR
https://www.hwhardsoft.de/english/projects/arduitouch-mkr/

Uses libraries:
- Adafruit GFX library for ILI9341 touch screen display
- WiFiNina for MKR1010 WiFi
- FlashStorage for storing data into the flash memory (ATSAM doesn't have its own EEPROM)
- ArduinoJson for parsing data downloaded from OpenWeatherMap
- SimpleDHT for reading the DHT11 Temperature & Humidity sensor
- Extra Fonts from Online font converter: http://oleddisplay.squix.ch/#/home

Requires:
- Access to a WiFi network
- An account on OpenWeatherMap
- Custom functions to operate remotely on your Home Automation System
