# ESP32 & 3.5 Inch TFT WeatherStation

Hardware:
ESP32-S3 Wroom1
Display - 3.5 TFT with S7796S driver 480x320 (Landscape) - Approx $7 USD from Aliexpress

![Image 1](./images/WeatherStationv2.jpg)

Key Features/Enhancements:

- Sketch will update the current time every 1 minute while the weather information, forecasts and graphs refresh on a 10-minute interval (or whatever UPDATE_INTERVAL_SECS is set to in your All_Settings.h file).

- A circular rotating animation that appears to show when the weather info is being updated (about 100 pixels right of the time) and vertically centered with the time/temperature line, ensuring it doesn't interfere with any of your existing display elements or clearing functions.

- Added a 24 hour rainfall % forcast graph (JASON variable "pop"), due to space limitations AM/AP are depicted in different colours

- Added a 24 hour temperature forecast line graph

Configured BodmersTFT_eSPI Library:
1) User_Setup_Selech.h:
  - #include <User_Setups/Setup27_RPi_ST7796_ESP32.h>

2) User_Setup.h:
-  #define ST7796_DRIVER
-  #define TFT_BL   7            // LED back-light control pin
-  #define TFT_MISO 19
-  #define TFT_MOSI 11
-  #define TFT_SCLK 12
-  #define TFT_CS   4  // Chip select control pin
-  #define TFT_DC    6  // Data Command control pin
-  #define TFT_RST   5  // Reset pin (could connect to RST pin)
-  #define SPI_FREQUENCY  40000000
-  #define USE_FSPI_PORT // or USE_HSPI_PORT // This one is important! I could not get the display working until I read about this setting

General Call-outs:
- MISO, MOSI & SCLK need to be defined for any picture to display, even if MISO is not connected!
- Ensure this is enabled "#define USE_FSPI_PORT // or USE_HSPI_PORT"  !!!!!
- Backlight Control:
#define TFT_BL   7            // LED back-light control pin
//#define TFT_BACKLIGHT_ON HIGH  // Level to turn ON back-light (HIGH or LOW) // If uncommented backlight will stay bright (100%), 

//If above line is uncommented, Control backlight in sketch with:
  //pinMode(TFT_BL, OUTPUT);
  //analogWrite(TFT_BL, 10);  // Set backlight to full brightness (0-255)
  //delay(10);

This is a heavily modified version of the CYD Internet Weather Station with 3 days Forecast on an ESP32 Cheap Yellow Display located here - https://medium.com/@androidcrypto/create-an-internet-weather-station-with-3-days-forecast-on-an-esp32-cheap-yellow-display-cyd-15eb5c353b1d

====================================================================================================================================================================================================================================================================================
![Image 1](./images/esp32_cyd_weather_station_01_600h.png)

## Set up the TFT_eSPI library

Please don't forget to copy the files "*Setup801_ESP32_CYD_ILI9341_240x320.h*" and "*Setup805_ESP32_CYD_ST7789_240x320.h*" in the "User_Setups" folder of the TFT_eSPI library and edit the 
"*User_Setup_Select.h*" to include the set up, depending on your CYD board type (ILI9341 or ST7789).

## Required Libraries
````plaintext
TFT_eSPI Version: 2.4.3 *1) (https://github.com/Bodmer/TFT_eSPI)
OpenWeather Version: Feb 16, 2023 (https://github.com/Bodmer/OpenWeather)
JSON_Decoder Version: n.a. (https://github.com/Bodmer/JSON_Decoder)
TJpg_Decoder Version: 1.1.0 (https://github.com/Bodmer/TJpg_Decoder)
Timezone Version: 1.2.4 (https://github.com/JChristensen/Timezone)

*1) In case you encounter any problems with the TFT_eSPI library you should consider to use my forked TFT_eSPI library that solved some problems, see link below
````

Forked TFT_eSPI library by AndroidCrypto: https://github.com/AndroidCrypto/TFT_eSPI

## This sketch uses the LittleFS file system
The weather icons and font files are stored in the LittleFS filesystem. Before running the sketch you need to upload the files in the sketch subfolder 'data' to the ESP32, 
for e.g. with the arduino-littlefs-upload plugin (https://github.com/earlephilhower/arduino-littlefs-upload).

A Description can be found here: https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/

## Development Environment
````plaintext
Arduino IDE Version 2.3.6 (Windows)
arduino-esp32 boards Version 3.2.0 (https://github.com/espressif/arduino-esp32)
````
