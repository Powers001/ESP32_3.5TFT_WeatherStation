/*
  ESP32 Weather Station with OpenUV Integration
  Display: 3.5" (320x480)
  
  FIXES APPLIED:
  ✓ Removed duplicate drawUVIndex() function
  ✓ Added separate UV update timer (every 30 minutes)
  ✓ UV updates independently from weather data
*/

#include <FS.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "GfxUi.h"
#include <WiFi.h>
#include "All_Settings.h"
#include <JSON_Decoder.h>
#include <OpenWeather.h>
#include "NTP_Time.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// OpenUV API configuration and UV_UPDATE_INTERVAL_SECS moved to All_Settings.h

// Display constants
static const uint16_t SMALL_FONT_HEIGHT = 18;
static const uint16_t LARGE_FONT_HEIGHT = 40;
static const uint16_t TEXT_LINE_HEIGHT = 20;
static const uint16_t ICON_SIZE_LARGE = 100;
static const uint16_t ICON_SIZE_SMALL = 64;
static const uint16_t ICON_SIZE_TINY = 48;

// UV Index display constants
static const uint16_t UV_DISPLAY_HEIGHT = 30;  // Total height for UV display
static const uint16_t UV_MARGIN_BOTTOM = 8;    // 2mm ≈ 8 pixels at 96 DPI

// Global objects
TFT_eSPI tft = TFT_eSPI();
OW_Weather ow;
OW_forecast* forecast = nullptr;
GfxUi ui = GfxUi(&tft);

// UV Data structure
struct UVData {
  float current_uv;   // Real-time UV index
  float max_uv;       // Maximum UV for the day
  time_t max_uv_time; // When max occurs
  bool valid;
  unsigned long lastUpdate;
  char max_time_str[6]; // localized HH:MM for display
} uvData = {0.0, 0.0, 0, false, 0, ""};

// State variables
boolean booted = true;
boolean firstRun = true;
long lastDownloadUpdate = millis();
long lastUVUpdate = 0;  // trigger UV update on first loop
uint8_t rotationAngle = 0;
boolean showUpdateIcon = false;
unsigned long lastIconRotation = 0;

// Display dimensions
uint16_t screenWidth, screenHeight;

// Cached dimension calculations
struct ScreenDims {
  uint16_t halfW, halfH;
  uint16_t w1_50, w3_50, w21_50, w24_50, w26_50, w47_100, w51_100;
  uint16_t h1_50, h2_25, h3_50, h9_50, h1_5, h23_100, h13_20;
  uint16_t statusTextY;
  uint16_t uvDisplayY;
} dims;

// Previous values for change detection
struct PreviousValues {
  float temp;
  uint16_t weatherId;
  float windSpeed;
  uint16_t windDeg;
  uint8_t clouds;
  uint8_t humidity;
  time_t sunrise;
  time_t sunset;
  uint8_t moonPhaseIcon;
  float forecastTemps[4][2];
  uint16_t forecastIds[4];
  float pop[8];
  float hourlyTemps[9];
  char lastTimeStr[8];
  char lastDateStr[32];
  float lastUV;
  char lastUVTime[6];
} prevVals;

// Layout verification structure
struct LayoutBounds {
  uint16_t timeX, timeY, timeW, timeH;
  uint16_t weatherX, weatherY, weatherW, weatherH;
  uint16_t uvX, uvY, uvW, uvH;
  uint16_t astronomyX, astronomyY, astronomyW, astronomyH;
  uint16_t forecastX, forecastY, forecastW, forecastH;
  uint16_t popGraphX, popGraphY, popGraphW, popGraphH;
  uint16_t tempGraphX, tempGraphY, tempGraphW, tempGraphH;
} layout;

// Function prototypes
void updateData();
void updateUVData();
void drawUVIndex();
uint16_t getUVColor(float uvIndex);
const char* getUVCategory(float uvIndex);
void drawProgress(uint8_t percentage, const char* text);
void drawTime();
void drawCurrentWeather();
void drawForecast();
void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex);
const char* getMeteoconIcon(uint16_t id, bool today);
void drawAstronomy();
void drawSeparator(uint16_t x, uint16_t y, uint16_t length, bool isHorizontal);
void drawHourlyTemperatureGraph(uint16_t yStart);
void drawHourlyPopGraph(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
int splitIndex(const String& text);
int getNextDayIndex();
void initScreenDimensions();
void initPreviousValues();
void initLayoutBounds();
void drawStaticElements();
void clearArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
bool hasWeatherChanged();
bool hasForecastChanged();
bool hasAstronomyChanged();
bool hasPopChanged();
bool hasHourlyTempChanged();
bool hasUVChanged();
bool isForecastDataValid();
void drawUpdateIcon(bool show);
void rotateUpdateIcon();
bool verifyNoOverlap();
void printLayoutInfo();

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= screenHeight) return false;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

void initScreenDimensions() {
  dims.halfW = screenWidth >> 1;
  dims.halfH = screenHeight >> 1;
  dims.w1_50 = screenWidth / 50;
  dims.w3_50 = screenWidth * 3 / 50;
  dims.w21_50 = screenWidth * 21 / 50;
  dims.w24_50 = screenWidth * 24 / 50;
  dims.w26_50 = screenWidth * 26 / 50;
  dims.w47_100 = screenWidth * 47 / 100;
  dims.w51_100 = screenWidth * 51 / 100;
  dims.h1_50 = screenHeight / 50;
  dims.h2_25 = screenHeight * 2 / 25;
  dims.h3_50 = screenHeight * 3 / 50;
  dims.h9_50 = screenHeight * 9 / 50;
  dims.h1_5 = screenHeight / 5;
  dims.h23_100 = screenHeight * 23 / 100;
  dims.h13_20 = screenHeight * 13 / 20;
  dims.statusTextY = screenHeight * 2 / 3;
  
  dims.uvDisplayY = dims.h13_20 - UV_DISPLAY_HEIGHT - UV_MARGIN_BOTTOM;
}

void initPreviousValues() {
  prevVals.temp = -9999;
  prevVals.weatherId = 0;
  prevVals.windSpeed = -9999;
  prevVals.windDeg = 9999;
  prevVals.clouds = 255;
  prevVals.humidity = 255;
  prevVals.sunrise = 0;
  prevVals.sunset = 0;
  prevVals.moonPhaseIcon = 255;
  prevVals.lastUV = -1;
  prevVals.lastUVTime[0] = '\0';
  memset(prevVals.forecastTemps, 0, sizeof(prevVals.forecastTemps));
  memset(prevVals.forecastIds, 0, sizeof(prevVals.forecastIds));
  memset(prevVals.pop, 0, sizeof(prevVals.pop));
  memset(prevVals.hourlyTemps, 0, sizeof(prevVals.hourlyTemps));
  memset(prevVals.lastTimeStr, 0, sizeof(prevVals.lastTimeStr));
  memset(prevVals.lastDateStr, 0, sizeof(prevVals.lastDateStr));
}

void initLayoutBounds() {
  layout.timeX = dims.w1_50;
  layout.timeY = dims.h1_50;
  layout.timeW = dims.halfW - dims.w1_50;
  layout.timeH = dims.h2_25 + LARGE_FONT_HEIGHT - dims.h1_50;
  
  layout.weatherX = 0;
  layout.weatherY = dims.h1_5;
  layout.weatherW = dims.halfW;
  layout.weatherH = dims.uvDisplayY - dims.h1_5;
  
  layout.uvX = 0;
  layout.uvY = dims.uvDisplayY;
  layout.uvW = dims.halfW;
  layout.uvH = UV_DISPLAY_HEIGHT;
  
  layout.astronomyX = ICON_SIZE_LARGE;
  layout.astronomyY = dims.h1_5;
  layout.astronomyW = dims.halfW - ICON_SIZE_LARGE;
  layout.astronomyH = dims.h13_20 - dims.h1_5;
  
  layout.forecastX = dims.w26_50;
  layout.forecastY = 0;
  layout.forecastW = dims.w24_50;
  layout.forecastH = dims.h9_50 + screenHeight * 3 / 20 + 26;
  
  layout.popGraphX = dims.w51_100;
  layout.popGraphY = layout.forecastH + 6;
  layout.popGraphW = dims.w24_50;
  layout.popGraphH = dims.h13_20 - layout.popGraphY - 6;
  
  layout.tempGraphX = 0;
  layout.tempGraphY = dims.h13_20;
  layout.tempGraphW = screenWidth;
  layout.tempGraphH = screenHeight - dims.h13_20;
}

void clearArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (x >= screenWidth || y >= screenHeight) return;
  if (x + w > screenWidth) w = screenWidth - x;
  if (y + h > screenHeight) h = screenHeight - y;
  tft.fillRect(x, y, w, h, TFT_BLACK);
}

bool isForecastDataValid() {
  return (forecast != nullptr && 
          forecast->temp[0] > -100 && forecast->temp[0] < 100);
}

void drawStaticElements() {
  drawSeparator(dims.w1_50, dims.h9_50, dims.w24_50, true);
  drawSeparator(dims.halfW, 0, screenHeight * 11 / 20, false);
  drawSeparator(0, dims.h13_20, screenWidth, true);
}

bool verifyNoOverlap() {
  bool timeWeatherOk = (layout.timeY + layout.timeH <= layout.weatherY);
  bool weatherUVOk = (layout.weatherY + layout.weatherH <= layout.uvY);
  bool uvTempGraphOk = (layout.uvY + layout.uvH <= layout.tempGraphY);
  
  bool forecastPopOk = (layout.forecastY + layout.forecastH <= layout.popGraphY);
  bool popTempOk = (layout.popGraphY + layout.popGraphH <= layout.tempGraphY);
  
  bool leftRightOk = (layout.weatherX + layout.weatherW <= dims.halfW) &&
                     (layout.forecastX >= dims.halfW);
  
  bool allOk = timeWeatherOk && weatherUVOk && uvTempGraphOk && 
               forecastPopOk && popTempOk && leftRightOk;
  
  if (!allOk) {
    Serial.println(F("LAYOUT OVERLAP DETECTED!"));
    printLayoutInfo();
  }
  
  return allOk;
}

void printLayoutInfo() {
  Serial.println(F("\n=== LAYOUT BOUNDS ==="));
  Serial.printf("Screen: %dx%d\n", screenWidth, screenHeight);
  Serial.printf("Time: (%d,%d) %dx%d\n", layout.timeX, layout.timeY, layout.timeW, layout.timeH);
  Serial.printf("Weather: (%d,%d) %dx%d\n", layout.weatherX, layout.weatherY, layout.weatherW, layout.weatherH);
  Serial.printf("UV Index: (%d,%d) %dx%d\n", layout.uvX, layout.uvY, layout.uvW, layout.uvH);
  Serial.printf("Astronomy: (%d,%d) %dx%d\n", layout.astronomyX, layout.astronomyY, layout.astronomyW, layout.astronomyH);
  Serial.printf("Forecast: (%d,%d) %dx%d\n", layout.forecastX, layout.forecastY, layout.forecastW, layout.forecastH);
  Serial.printf("PoP Graph: (%d,%d) %dx%d\n", layout.popGraphX, layout.popGraphY, layout.popGraphW, layout.popGraphH);
  Serial.printf("Temp Graph: (%d,%d) %dx%d\n", layout.tempGraphX, layout.tempGraphY, layout.tempGraphW, layout.tempGraphH);
  Serial.println();
}

uint16_t getUVColor(float uvIndex) {
  if (uvIndex < 0) return TFT_WHITE;
  if (uvIndex <= 2) return TFT_GREEN;
  if (uvIndex <= 5) return TFT_YELLOW;
  if (uvIndex <= 7) return TFT_ORANGE;
  if (uvIndex <= 10) return TFT_RED;
  return TFT_VIOLET;
}

const char* getUVCategory(float uvIndex) {
  if (uvIndex < 0) return "N/A";
  if (uvIndex <= 2) return "LOW";
  if (uvIndex <= 5) return "MODERATE";
  if (uvIndex <= 7) return "HIGH";
  if (uvIndex <= 10) return "VERY HIGH";
  return "EXTREME";
}

bool hasUVChanged() {
  if (!uvData.valid) return false;
  return (abs(prevVals.lastUV - uvData.max_uv) > 0.1 || strcmp(prevVals.lastUVTime, uvData.max_time_str) != 0);
}

void updateUVData() {
  if (!WiFi.isConnected()) {
    Serial.println(F("WiFi not connected for UV update"));
    return;
  }
  
  HTTPClient http;
  
  char url[160];
  snprintf(url, sizeof(url), "%s?lat=%.6f&lng=%.6f",
           OPENUV_API_URL.c_str(), latitude.toFloat(), longitude.toFloat());
  
  Serial.printf("Fetching UV data from: %s\n", url);
  
  http.begin(url);
  http.addHeader("x-access-token", OPENUV_API_KEY.c_str());
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      uvData.current_uv = doc["result"]["uv"];
      uvData.max_uv = doc["result"]["uv_max"];
      
      const char* timeStr = doc["result"]["uv_max_time"];
      uvData.max_time_str[0] = '\0';
      if (timeStr && strlen(timeStr) >= 16) {
        // Parse HH:MM from ISO8601 UTC (YYYY-MM-DDTHH:MM:SSZ)
        int uh = (timeStr[11]-'0')*10 + (timeStr[12]-'0');
        int um = (timeStr[14]-'0')*10 + (timeStr[15]-'0');
        long offsetSec = (long)TIMEZONE.toLocal(now(), &tz1_Code) - (long)now();
        int localMin = uh * 60 + um + (int)(offsetSec / 60);
        while (localMin < 0) localMin += 1440;
        while (localMin >= 1440) localMin -= 1440;
        int lh = localMin / 60;
        int lm = localMin % 60;
        snprintf(uvData.max_time_str, sizeof(uvData.max_time_str), "%02d:%02d", lh, lm);
      }
      
      uvData.valid = true;
      uvData.lastUpdate = millis();
      
      Serial.printf("UV Data: Current=%.1f, Max=%.1f (at %s local)\n",
                    uvData.current_uv, uvData.max_uv,
                    (uvData.max_time_str[0] ? uvData.max_time_str : "--:--"));
    } else {
      Serial.printf("JSON parse error: %s\n", error.c_str());
      uvData.valid = false;
    }
  } else {
    Serial.printf("HTTP error: %d\n", httpCode);
    uvData.valid = false;
  }
  
  http.end();
}

void drawUVIndex() {
  // Draw only the right-justified Max UV on the temperature graph title line
  const uint16_t topPadding = 22;
  const uint16_t yStart = dims.h13_20; // same start as temperature graph
  const uint16_t y0 = yStart + topPadding;
  const uint16_t x1 = screenWidth - 18;

  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(TR_DATUM);

  char uvText[48];
  uint16_t color = TFT_DARKGREY;
  if (uvData.valid) {
    const char* t = (uvData.max_time_str[0] ? uvData.max_time_str : "--:--");
    snprintf(uvText, sizeof(uvText), "Max UV: %.1f at %s", uvData.max_uv, t);
    color = getUVColor(uvData.max_uv);
  } else {
    snprintf(uvText, sizeof(uvText), "Max UV: -- at --:--");
  }

  // Clear just the right side area where UV text appears
  uint16_t w = tft.textWidth(uvText) + 6;
  if (w > (screenWidth/2)) w = screenWidth/2; // clamp
  clearArea(x1 - w, y0 - 22, w, 20);

  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(uvText, x1, y0 - 18);
  tft.unloadFont();

  if (uvData.valid) {
    prevVals.lastUV = uvData.max_uv;
    strcpy(prevVals.lastUVTime, uvData.max_time_str);
  }
}

void setup() {
  Serial.begin(250000);
  delay(500);

  if (psramFound()) {
    Serial.println(F("✓ PSRAM initialized."));
  } else {
    Serial.println(F("⚠ WARNING: PSRAM not found!"));
  }
  
  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, BACKLIGHT);
  delay(10);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  screenWidth = tft.width();
  screenHeight = tft.height();
  
  Serial.printf("Display: %dx%d\n", screenWidth, screenHeight);
  
  initScreenDimensions();
  initPreviousValues();
  initLayoutBounds();
  
  if (!verifyNoOverlap()) {
    Serial.println(F("✗ ERROR: Layout has overlapping sections!"));
  } else {
    Serial.println(F("✓ Layout verification passed"));
  }

  Serial.print(F("Initializing LittleFS... "));
  if (!LittleFS.begin(false)) {
    Serial.println(F("FAILED!"));
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("FS Mount FAILED!", screenWidth / 2, screenHeight / 2);
    tft.drawString("Re-upload Sketch Data!", screenWidth / 2, screenHeight / 2 + 30);
    while (true) delay(1000);
  }
  Serial.println(F("SUCCESS."));

#ifdef FORMAT_LittleFS
  tft.setTextDatum(BC_DATUM);
  tft.drawString("Formatting LittleFS, so wait!", dims.halfW, screenHeight * 4 / 5);
  LittleFS.format();
#endif

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tft_output);
  TJpgDec.setSwapBytes(true);

  if (LittleFS.exists("/splash/OpenWeather.jpg")) {
    TJpgDec.drawFsJpg((screenWidth - 240) >> 1, screenHeight / 8, 
              "/splash/OpenWeather.jpg", LittleFS);
  } else {
    Serial.println(F("Splash image NOT FOUND!"));
  }

  delay(2000);
  tft.fillRect(0, screenHeight * 43 / 50, screenWidth, screenHeight * 7 / 50, TFT_BLACK);

  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("Original by: blog.squix.org", dims.halfW, screenHeight * 23 / 25);
  tft.drawString("Adapted by: Bodmer", dims.halfW, screenHeight * 24 / 25);
  tft.unloadFont();

  delay(2000);
  tft.fillRect(0, screenHeight * 43 / 50, screenWidth, screenHeight * 7 / 50, TFT_BLACK);
 
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(BC_DATUM);
  tft.setTextPadding(screenWidth);
  tft.drawString("Connecting to WiFi", dims.halfW, dims.statusTextY);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  Serial.println();

  tft.fillRect(0, dims.statusTextY - 30, screenWidth, screenHeight - (dims.statusTextY - 30), TFT_BLACK); 
  tft.drawString("Fetching weather data...", dims.halfW, dims.statusTextY);
  tft.setTextPadding(0);
  tft.unloadFont();

  udp.begin(localPort);
  syncTime();
  
  Serial.println(F("✓ Setup complete. Starting loop..."));
}

void loop() {
  if (showUpdateIcon && (millis() - lastIconRotation > 50)) {
    rotateUpdateIcon();
    lastIconRotation = millis();
  }
  
  if (booted || (millis() - lastDownloadUpdate > 1000UL * UPDATE_INTERVAL_SECS)) {
    showUpdateIcon = true;
    drawUpdateIcon(true);
    updateData();
    lastDownloadUpdate = millis();
    showUpdateIcon = false;
    drawUpdateIcon(false);
  }
  
  if (booted || (millis() - lastUVUpdate > 1000UL * UV_UPDATE_INTERVAL_SECS)) {
    Serial.println(F("Updating UV data..."));
    bool prevValid = uvData.valid;
    updateUVData();
    
    if (firstRun || hasUVChanged() || (prevValid != uvData.valid)) {
      drawUVIndex();
      Serial.println(F("✓ UV index updated"));
    }
    
    lastUVUpdate = millis();
  }
  
  if (booted) booted = false;
  
  if (minute() != lastMinute) {
    drawTime();
    lastMinute = minute();
    syncTime();
  }
  
  delay(100);
}

bool hasWeatherChanged() {
  if (!isForecastDataValid()) return false;
  return (prevVals.temp != forecast->temp[0] || 
          prevVals.weatherId != forecast->id[0]);
}

bool hasForecastChanged() {
  if (!isForecastDataValid()) return false;
  int8_t dayIndex = getNextDayIndex();
  
  for (int i = 0; i < 4; i++) {
    int idx = dayIndex + (i * 8);
    if (idx >= MAX_DAYS * 8) continue;
    
    float tmax = -9999, tmin = 9999;
    for (int j = 0; j < 8; j++) {
      int checkIdx = idx + j;
      if (checkIdx >= MAX_DAYS * 8) break;
      
      float maxT = forecast->temp_max[checkIdx];
      float minT = forecast->temp_min[checkIdx];
      if (maxT > tmax) tmax = maxT;
      if (minT < tmin) tmin = minT;
    }
    
    if (abs(prevVals.forecastTemps[i][0] - tmin) > 0.5 || 
        abs(prevVals.forecastTemps[i][1] - tmax) > 0.5 ||
        prevVals.forecastIds[i] != forecast->id[idx + 4]) {
      return true;
    }
  }
  return false;
}

bool hasAstronomyChanged() {
  if (!isForecastDataValid()) return false;
  
  time_t local_time = TIMEZONE.toLocal(forecast->dt[0], &tz1_Code);
  int ip;
  uint8_t icon = moon_phase(year(local_time), month(local_time), day(local_time), 
                            hour(local_time), &ip);
  
  return (abs(prevVals.windSpeed - forecast->wind_speed[0]) > 0.5 ||
          prevVals.windDeg != forecast->wind_deg[0] ||
          prevVals.clouds != forecast->clouds_all[0] ||
          prevVals.humidity != forecast->humidity[0] ||
          prevVals.sunrise != forecast->sunrise ||
          prevVals.sunset != forecast->sunset ||
          prevVals.moonPhaseIcon != icon);
}

bool hasPopChanged() {
  if (!isForecastDataValid()) return false;
  for (int i = 0; i < 8; i++) {
    if (abs(prevVals.pop[i] - forecast->pop[i]) > 0.01) return true;
  }
  return false;
}

bool hasHourlyTempChanged() {
  if (!isForecastDataValid()) return false;
  for (int i = 0; i < 9; i++) {
    if (abs(prevVals.hourlyTemps[i] - forecast->temp[i]) > 0.5) return true;
  }
  return false;
}

void updateData() {
  if (booted) {
    drawProgress(50, "Updating conditions...");
  }

  if (forecast) {
    delete forecast;
    forecast = nullptr;
  }
  
  forecast = new OW_forecast;
  if (!forecast) {
    Serial.println(F("Failed to allocate forecast memory"));
    return;
  }

  bool parsed = ow.getForecast(forecast, api_key, latitude, longitude, units, language);
  Serial.println(parsed ? F("✓ Weather data received") : F("✗ Failed to get weather data"));

  #ifdef SERIAL_MESSAGES
  printWeather();
  #endif

  if (booted) {
    drawProgress(100, "Done...");
    delay(2000);
    tft.fillScreen(TFT_BLACK);
  }

  if (parsed && isForecastDataValid()) {
    if (firstRun) {
      drawStaticElements();
      Serial.println(F("✓ Static elements drawn"));
    }
    
    drawTime();
    
    tft.loadFont(AA_FONT_SMALL, LittleFS);
    
    if (firstRun || hasWeatherChanged()) {
      Serial.println(F("Updating current weather..."));
      drawCurrentWeather();
      
      char tempStr[8];
      snprintf(tempStr, sizeof(tempStr), "%d", (int)forecast->temp[0]);
      
      tft.unloadFont();
      tft.loadFont(AA_FONT_LARGE, LittleFS);
      
      uint16_t tempWidth = tft.textWidth("-88") + 5;
      clearArea(dims.w47_100 - tempWidth, dims.h2_25, tempWidth, LARGE_FONT_HEIGHT);
      
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString(tempStr, dims.w47_100, dims.h2_25);
      tft.unloadFont();
      tft.loadFont(AA_FONT_SMALL, LittleFS);
      
      prevVals.temp = forecast->temp[0];
      prevVals.weatherId = forecast->id[0];
      Serial.println(F("✓ Current weather updated"));
    }
    
    if (firstRun) {
      Serial.println(F("Drawing initial UV index..."));
      drawUVIndex();
      Serial.println(F("✓ UV index drawn"));
    }
    
    if (firstRun || hasForecastChanged() || hasPopChanged()) {
      Serial.println(F("Updating forecast..."));
      drawForecast();
      Serial.println(F("✓ Forecast updated"));
    }
    
    if (firstRun || hasAstronomyChanged()) {
      Serial.println(F("Updating astronomy..."));
      drawAstronomy();
      Serial.println(F("✓ Astronomy updated"));
    }
    
    tft.unloadFont();
    
    if (firstRun) {
      firstRun = false;
      Serial.println(F("✓ First run complete"));
    }
  } else {
    Serial.println(F("✗ Invalid weather data"));
  }
}

void drawProgress(uint8_t percentage, const char* text) {
  tft.loadFont(AA_FONT_SMALL, LittleFS);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(screenWidth);
  tft.drawString(text, dims.halfW, screenHeight * 87 / 100);
  tft.setTextPadding(0);
  
  ui.drawProgressBar(dims.w3_50, screenHeight * 47 / 50, 
                     screenWidth * 44 / 50, screenHeight / 20, 
                     percentage, TFT_WHITE, TFT_BLUE);
  tft.unloadFont();
}

void drawTime() {
  char buf[32];
  time_t n = now();
  time_t local = TIMEZONE.toLocal(n, &tz1_Code);
  
  char timeStr[8];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour(local), minute(local));
  
  snprintf(buf, sizeof(buf), "Updated: %s %d %02d:%02d", 
           monthShortStr(month(local)), day(local), 
           hour(local), minute(local));
  
  if (strcmp(timeStr, prevVals.lastTimeStr) != 0 || strcmp(buf, prevVals.lastDateStr) != 0) {
    tft.loadFont(AA_FONT_SMALL, LittleFS);
    
    uint16_t dateWidth = tft.textWidth(buf) + 5;
    clearArea(layout.timeX, layout.timeY, dateWidth, SMALL_FONT_HEIGHT);
    
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextPadding(0);
    tft.drawString(buf, layout.timeX, layout.timeY);
    tft.unloadFont();

    tft.loadFont(AA_FONT_LARGE, LittleFS);
    
    uint16_t timeWidth = tft.textWidth("88:88") + 5;
    clearArea(layout.timeX, dims.h2_25, timeWidth, LARGE_FONT_HEIGHT);
    
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextPadding(0);
    tft.drawString(timeStr, layout.timeX, dims.h2_25);
    tft.unloadFont();
    
    strncpy(prevVals.lastTimeStr, timeStr, sizeof(prevVals.lastTimeStr) - 1);
    strncpy(prevVals.lastDateStr, buf, sizeof(prevVals.lastDateStr) - 1);
  }
}

void drawCurrentWeather() {
  if (!isForecastDataValid()) return;
  
  // Only clear the left current-weather text area; do not extend into the Cloud/Humidity column (~w21_50)
  clearArea(0, dims.h1_5, dims.w21_50 - 30, TEXT_LINE_HEIGHT * 2 + 5);
  
  const String& weatherText = (language == "en") ? forecast->main[0] : forecast->description[0];
  
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(0);

  int splitPoint = splitIndex(weatherText);
  int xpos = dims.w1_50 >> 1;
  
  if (splitPoint > 0) {
    tft.drawString(weatherText.substring(0, splitPoint), xpos, dims.h1_5);
    tft.drawString(weatherText.substring(splitPoint), xpos, dims.h1_5 + TEXT_LINE_HEIGHT);
  } else {
    tft.drawString(weatherText, xpos, dims.h1_5);
  }

  const uint16_t iconStartY = dims.h1_5 + 30;
  const uint16_t ICON_CLEAR_HEIGHT = dims.uvDisplayY - iconStartY - 5;

  clearArea(0, iconStartY, ICON_SIZE_LARGE, ICON_CLEAR_HEIGHT);

  const char* weatherIcon = getMeteoconIcon(forecast->id[0], true);
  char iconPath[32];
  snprintf(iconPath, sizeof(iconPath), "/icon/%s.bmp", weatherIcon);
  ui.drawBmp(iconPath, 0, iconStartY);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(units[0] == 'm' ? "°C" : "°F", dims.halfW, dims.h3_50);

  tft.setTextDatum(TL_DATUM);
}

void drawForecast() {
  if (!isForecastDataValid()) return;
  
  int8_t dayIndex = getNextDayIndex();
  const int colW = dims.w24_50 / 4;
  const int y = screenHeight / 20;

  const uint16_t separatorY = y + screenHeight * 3 / 20 + 26;
 
  clearArea(layout.forecastX, layout.forecastY, layout.forecastW, separatorY);

  // Ensure small font for forecast labels
  tft.loadFont(AA_FONT_SMALL, LittleFS);

  for (int i = 0; i < 4; i++) {
    drawForecastDetail(dims.w26_50 + colW * i, y, dayIndex);
    
    int idx = dayIndex;
    if (idx + 4 < MAX_DAYS * 8) {
      float tmax = -9999, tmin = 9999;
      for (int j = 0; j < 8; j++) {
        int checkIdx = idx + j;
        if (checkIdx >= MAX_DAYS * 8) break;
        
        float maxT = forecast->temp_max[checkIdx];
        float minT = forecast->temp_min[checkIdx];
        if (maxT > tmax) tmax = maxT;
        if (minT < tmin) tmin = minT;
      }
      
      prevVals.forecastTemps[i][0] = tmin;
      prevVals.forecastTemps[i][1] = tmax;
      prevVals.forecastIds[i] = forecast->id[idx + 4];
    }
    
    dayIndex += 8;
  }
  
  drawSeparator(dims.w51_100, separatorY, dims.w24_50, true);

  const uint16_t graphY = separatorY + 6;
  const uint16_t graphH = dims.h13_20 - graphY - 6;
  drawHourlyPopGraph(dims.w51_100, graphY, dims.w24_50, graphH);
  
  for (int i = 0; i < 8; i++) {
    prevVals.pop[i] = forecast->pop[i];
  }

  tft.unloadFont();
}

void drawForecastDetail(uint16_t x, uint16_t y, uint8_t dayIndex) {
  if (!isForecastDataValid() || dayIndex >= MAX_DAYS * 8) return;
  if (dayIndex + 4 >= MAX_DAYS * 8) return;
  
  String day = shortDOW[weekday(TIMEZONE.toLocal(forecast->dt[dayIndex + 4], &tz1_Code))];
  day.toUpperCase();

  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextPadding(0);
  tft.drawString(day, x + 35, y + 6);

  float tmax = -9999, tmin = 9999;
  for (int i = 0; i < 8; i++) {
    int idx = dayIndex + i;
    if (idx >= MAX_DAYS * 8) break;
    
    float maxT = forecast->temp_max[idx];
    float minT = forecast->temp_min[idx];
    if (maxT > tmax) tmax = maxT;
    if (minT < tmin) tmin = minT;
  }

  char buf[16];
  snprintf(buf, sizeof(buf), "%d %d", (int)tmax, (int)tmin);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, x + 35, y + screenHeight * 7 / 200 + 13);

  const char* weatherIcon = getMeteoconIcon(forecast->id[dayIndex + 4], false);
  char iconPath[40];
  snprintf(iconPath, sizeof(iconPath), "/icon50/%s.bmp", weatherIcon);
  ui.drawBmp(iconPath, x + 10, y + screenHeight / 25 + 13);
}

void drawAstronomy() {
  if (!isForecastDataValid()) return;
  
  const uint16_t centerX = dims.w21_50 >> 1;
  const uint16_t humX = dims.w21_50;
  char buf[16];

  tft.loadFont(AA_FONT_SMALL, LittleFS);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  
  uint16_t windTextWidth = tft.textWidth("888 m/s") + 5;
  clearArea(centerX + 24 - (windTextWidth / 2), dims.h1_5, windTextWidth, SMALL_FONT_HEIGHT);
  
  snprintf(buf, sizeof(buf), "%d %s", (int)forecast->wind_speed[0], 
           units[0] == 'm' ? "m/s" : "mph");
  tft.drawString(buf, centerX + 24, dims.h1_5);

  clearArea(centerX + 4, dims.h1_5 + 15, ICON_SIZE_TINY, ICON_SIZE_TINY);
  
  int windAngle = (forecast->wind_deg[0] + 22) / 45;
  if (windAngle > 7) windAngle = 0;
  static const char* windDir[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  
  char windPath[32];
  snprintf(windPath, sizeof(windPath), "/wind/%s.bmp", windDir[windAngle]);
  ui.drawBmp(windPath, centerX + 4, dims.h1_5 + 15);

  const uint16_t moonY = dims.h1_5 + 50;
  clearArea(centerX + 2, moonY + 15, ICON_SIZE_SMALL, ICON_SIZE_SMALL);
  
  time_t local_time = TIMEZONE.toLocal(forecast->dt[0], &tz1_Code);
  int ip;
  uint8_t icon = moon_phase(year(local_time), month(local_time), day(local_time), 
                            hour(local_time), &ip);

  char moonPath[40];
  snprintf(moonPath, sizeof(moonPath), "/moon/moonphase_L%d.bmp", icon);
  ui.drawBmp(moonPath, centerX + 2, moonY + 15);
  
  uint16_t moonTextWidth = tft.textWidth("Waning Crescent") + 5;
  clearArea(centerX + 30 - (moonTextWidth / 2), moonY + 77, moonTextWidth, SMALL_FONT_HEIGHT);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(moonPhase[ip], centerX + 30, moonY + 77);

  tft.setTextDatum(TC_DATUM);
  
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  uint16_t cloudLabelWidth = tft.textWidth(cloudStr) + 5;
  clearArea(humX - (cloudLabelWidth / 2), dims.h1_5, cloudLabelWidth, SMALL_FONT_HEIGHT);
  tft.drawString(cloudStr, humX, dims.h1_5);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%d%%", forecast->clouds_all[0]);
  uint16_t cloudValWidth = tft.textWidth("100%") + 5;
  clearArea(humX - (cloudValWidth / 2), dims.h1_5 + 20, cloudValWidth, SMALL_FONT_HEIGHT);
  tft.drawString(buf, humX, dims.h1_5 + 20);
  
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  uint16_t humLabelWidth = tft.textWidth(humidityStr) + 5;
  clearArea(humX - (humLabelWidth / 2), dims.h1_5 + 40, humLabelWidth, SMALL_FONT_HEIGHT);
  tft.drawString(humidityStr, humX, dims.h1_5 + 40);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%d%%", forecast->humidity[0]);
  uint16_t humValWidth = tft.textWidth("100%") + 5;
  clearArea(humX - (humValWidth / 2), dims.h1_5 + 60, humValWidth, SMALL_FONT_HEIGHT);
  tft.drawString(buf, humX, dims.h1_5 + 60);

  const uint16_t sunYStart = dims.h1_5 + 85;
  
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  uint16_t sunLabelWidth = tft.textWidth(sunStr) + 5;
  clearArea(humX - (sunLabelWidth / 2), sunYStart, sunLabelWidth, SMALL_FONT_HEIGHT);
  tft.setTextPadding(0);
  tft.drawString(sunStr, humX, sunYStart);
  
  time_t sunrise_local = TIMEZONE.toLocal(forecast->sunrise, &tz1_Code);
  time_t sunset_local = TIMEZONE.toLocal(forecast->sunset, &tz1_Code);
  
  snprintf(buf, sizeof(buf), "%02d:%02d", hour(sunrise_local), minute(sunrise_local));
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  uint16_t timeWidth = tft.textWidth("88:88") + 5;
  clearArea(humX - (timeWidth / 2), sunYStart + 20, timeWidth, SMALL_FONT_HEIGHT);
  tft.drawString(buf, humX, sunYStart + 20);
  
  snprintf(buf, sizeof(buf), "%02d:%02d", hour(sunset_local), minute(sunset_local));
  clearArea(humX - (timeWidth / 2), sunYStart + 40, timeWidth, SMALL_FONT_HEIGHT);
  tft.drawString(buf, humX, sunYStart + 40);

  tft.unloadFont();

  if (hasHourlyTempChanged()) {
    drawHourlyTemperatureGraph(dims.h13_20);
    
    for (int i = 0; i < 9; i++) {
      prevVals.hourlyTemps[i] = forecast->temp[i];
    }
  }
  
  prevVals.windSpeed = forecast->wind_speed[0];
  prevVals.windDeg = forecast->wind_deg[0];
  prevVals.clouds = forecast->clouds_all[0];
  prevVals.humidity = forecast->humidity[0];
  prevVals.sunrise = forecast->sunrise;
  prevVals.sunset = forecast->sunset;
  prevVals.moonPhaseIcon = icon;
}

void drawHourlyPopGraph(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  static const int segmentsToPlot = 8;
  if (!isForecastDataValid()) return;

  clearArea(x, y, w, h);

  // Use small font for all rainfall graph text
  tft.loadFont(AA_FONT_SMALL, LittleFS);

  const uint16_t titleHeight = 18;
  const uint16_t bottomPadding = 18;
  const uint16_t graphHeight = h - titleHeight - bottomPadding;
  const uint16_t y0 = y + titleHeight;
  const uint16_t x0 = x + 25;
  const uint16_t graphWidth = w - 28;

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Chance of Rain (%)", x + w / 2.5, y);

  uint16_t pmWidth = tft.textWidth("PM ");
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("PM", x + w, y);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("AM", x + w - pmWidth, y);

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  char label[4];
  for (int i = 0; i <= 4; i++) {
    uint16_t lineY = y0 + graphHeight - (graphHeight * i / 4);
    if (i < 4) {
      tft.drawFastHLine(x0, lineY, graphWidth, TFT_DARKGREY);
    }
    snprintf(label, sizeof(label), "%d", i * 25);
    tft.drawString(label, x0 - 4, lineY);
  }

  const int barSlotWidth = graphWidth / segmentsToPlot;
  const int barWidth = barSlotWidth >> 1;
  
  for (int i = 0; i < segmentsToPlot; i++) {
    uint16_t barHeight = forecast->pop[i] * graphHeight;
    if (barHeight > 0) {
      if (barHeight < 1) barHeight = 1;
      uint16_t barX = x0 + (i * barSlotWidth) + (barSlotWidth >> 2);
      tft.fillRect(barX, y0 + graphHeight - barHeight, barWidth, barHeight, TFT_BLUE);
    }
  }

  tft.setTextDatum(TC_DATUM);
  for (int i = 0; i < segmentsToPlot; i++) {
    uint16_t labelX = x0 + (i * barSlotWidth) + (barSlotWidth >> 1);
    time_t local_time = TIMEZONE.toLocal(forecast->dt[i], &tz1_Code);
    int h = hour(local_time);
    
    tft.setTextColor((h < 12) ? TFT_YELLOW : TFT_GREEN, TFT_BLACK);
    
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(label, sizeof(label), "%d", h12);
    tft.drawString(label, labelX, y0 + graphHeight + 4);
  }

  tft.setTextDatum(TL_DATUM);
  tft.unloadFont();
}

void drawHourlyTemperatureGraph(uint16_t yStart) {
  clearArea(layout.tempGraphX, layout.tempGraphY, layout.tempGraphW, layout.tempGraphH);
  
  static const int hoursToPlot = 9;
  if (!isForecastDataValid()) return;

  float rawMinTemp = 9999, rawMaxTemp = -9999;
  for (int i = 0; i < hoursToPlot; i++) {
    float t = forecast->temp[i];
    if (t < rawMinTemp) rawMinTemp = t;
    if (t > rawMaxTemp) rawMaxTemp = t;
  }

  if ((rawMaxTemp - rawMinTemp) < 4) {
    float mid = (rawMaxTemp + rawMinTemp) / 2;
    rawMaxTemp = mid + 2;
    rawMinTemp = mid - 2;
  }
  
  float minTemp = floor(rawMinTemp) - 1;
  float maxTemp = ceil(rawMaxTemp) + 1;
  
  const uint16_t topPadding = 22;
  const uint16_t bottomPadding = 20;
  const uint16_t graphHeight = (screenHeight - yStart) - topPadding - bottomPadding;
  const uint16_t y0 = yStart + topPadding;

  float tempRange = maxTemp - minTemp;
  if (tempRange == 0) tempRange = 1;
  
  int yStep = (tempRange <= 8) ? 2 : ((tempRange <= 14) ? 3 : 5);

  const uint16_t x0 = dims.w3_50;
  const uint16_t x1 = screenWidth - 18;
  const uint16_t graphWidth = x1 - x0;

  tft.loadFont(AA_FONT_SMALL, LittleFS);
  // Left-aligned title
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("3-Hourly Temperature Forecast", x0, y0 - 18);

  char label[8];
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  
  int startT = ((int)ceil(minTemp / yStep)) * yStep;
  for (int t = startT; t <= maxTemp; t += yStep) {
    if (t < minTemp) continue;
    uint16_t y = y0 + graphHeight - (long)(graphHeight * (t - minTemp) / tempRange);
    tft.drawFastHLine(x0, y, graphWidth, TFT_DARKGREY);
    snprintf(label, sizeof(label), "%d°", t);
    tft.drawString(label, x0 - 5, y);
  }

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  for (int i = 0; i < hoursToPlot; i++) {
    uint16_t x = x0 + graphWidth * i / (hoursToPlot - 1);
    time_t local_time = TIMEZONE.toLocal(forecast->dt[i], &tz1_Code);
    int h = hour(local_time);
    
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(label, sizeof(label), "%d%s", h12, (h < 12) ? "am" : "pm");
    tft.drawString(label, x, y0 + graphHeight + 6);
  }

  tft.unloadFont();

  static const uint16_t LINE_THICKNESS = 3;
  static const uint16_t POINT_RADIUS = 5;
  
  uint16_t prevX = 0, prevY = 0;
  for (int i = 0; i < hoursToPlot; i++) {
    float temp = forecast->temp[i];
    uint16_t x = x0 + graphWidth * i / (hoursToPlot - 1);
    uint16_t y = y0 + graphHeight - (long)(graphHeight * (temp - minTemp) / tempRange);
    
    if (i > 0) {
      for (int offset = -(LINE_THICKNESS/2); offset <= (LINE_THICKNESS/2); offset++) {
        tft.drawLine(prevX, prevY + offset, x, y + offset, TFT_ORANGE);
      }
    }
    prevX = x;
    prevY = y;
    
    tft.fillCircle(x, y, POINT_RADIUS, TFT_ORANGE);
  }
  
  tft.setTextDatum(TL_DATUM);
  // Redraw the right-justified UV header after graph clears/updates
  drawUVIndex();
}

const char* getMeteoconIcon(uint16_t id, bool today) {
  if (!forecast) return "unknown";
  
  if (today && id / 100 == 8) {
    time_t currentTime = now();
    if (currentTime > 0 && (currentTime < forecast->sunrise || currentTime > forecast->sunset)) {
      id += 1000;
    }
  }
  
  if (id / 100 == 2) return "thunderstorm";
  if (id / 100 == 3) return "drizzle";
  if (id / 100 == 4) return "unknown";
  if (id == 500) return "lightRain";
  if (id == 511) return "sleet";
  if (id / 100 == 5) return "rain";
  if (id >= 611 && id <= 616) return "sleet";
  if (id / 100 == 6) return "snow";
  if (id / 100 == 7) return "fog";
  if (id == 800) return "clear-day";
  if (id == 801) return "partly-cloudy-day";
  if (id >= 802 && id <= 804) return "cloudy";
  if (id == 1800) return "clear-night";
  if (id == 1801) return "partly-cloudy-night";
  if (id >= 1802 && id <= 1804) return "cloudy";
  
  return "unknown";
}

void drawSeparator(uint16_t x, uint16_t y, uint16_t length, bool isHorizontal) {
  if (isHorizontal) {
    tft.drawFastHLine(x, y, length, 0x4228);
  } else {
    tft.drawFastVLine(x, y, length, 0x4228);
  }
}

int splitIndex(const String& text) {
  uint16_t index = 0;
  int halfLen = text.length() >> 1;
  
  while ((text.indexOf(' ', index) >= 0) && (index <= halfLen)) {
    index = text.indexOf(' ', index) + 1;
  }
  
  return (index > 0) ? index - 1 : 0;
}

int getNextDayIndex() {
  if (!forecast) return 0;
  
  const String& today = forecast->dt_txt[0];
  for (int index = 0; index < 9; index++) {
    if (forecast->dt_txt[index].substring(8, 10) != today.substring(8, 10)) {
      return index;
    }
  }
  return 0;
}

void printWeather() {
#ifdef SERIAL_MESSAGES
  if (!isForecastDataValid()) return;
  
  Serial.println(F("\n=== Weather Data ==="));
  Serial.printf("City: %s\n", forecast->city_name);
  
  char buf[8];
  time_t sr = TIMEZONE.toLocal(forecast->sunrise, &tz1_Code);
  snprintf(buf, sizeof(buf), "%02d:%02d", hour(sr), minute(sr));
  Serial.printf("Sunrise: %s\n", buf);
  
  time_t ss = TIMEZONE.toLocal(forecast->sunset, &tz1_Code);
  snprintf(buf, sizeof(buf), "%02d:%02d", hour(ss), minute(ss));
  Serial.printf("Sunset: %s\n", buf);
  
  Serial.printf("Location: %.2f, %.2f\n", ow.lat, ow.lon);
  Serial.printf("Timezone: %d\n", forecast->timezone);
  Serial.printf("Current temp: %.1f\n", forecast->temp[0]);
  Serial.printf("Weather: %s (%d)\n", forecast->main[0].c_str(), forecast->id[0]);
  Serial.println();
#endif
}

void drawUpdateIcon(bool show) {
  const uint16_t iconSize = 15;
  const uint16_t iconX = dims.w1_50 + 100;
  const uint16_t iconY = dims.h2_25 + 12;
  
  if (!show) {
    clearArea(iconX - iconSize/2 - 2, iconY - iconSize/2 - 2, iconSize + 4, iconSize + 4);
    rotationAngle = 0;
  }
}

void rotateUpdateIcon() {
  const uint16_t iconSize = 15;
  const uint16_t iconX = dims.w1_50 + 100;
  const uint16_t iconY = dims.h2_25 + 12;
  const uint16_t radius = iconSize / 2;
  
  clearArea(iconX - radius - 2, iconY - radius - 2, iconSize + 4, iconSize + 4);
  
  tft.drawCircle(iconX, iconY, radius, TFT_CYAN);
  
  for (int i = 0; i < 270; i += 10) {
    float a = (rotationAngle + i) * PI / 180.0;
    int x1 = iconX + (radius - 2) * cos(a);
    int y1 = iconY + (radius - 2) * sin(a);
    int x2 = iconX + radius * cos(a);
    int y2 = iconY + radius * sin(a);
    tft.drawLine(x1, y1, x2, y2, TFT_ORANGE);
  }
  
  float arrowAngle = (rotationAngle + 270) * PI / 180.0;
  int arrowX = iconX + radius * cos(arrowAngle);
  int arrowY = iconY + radius * sin(arrowAngle);
  
  int tipLen = 4;
  float tipAngle1 = arrowAngle - 2.8;
  float tipAngle2 = arrowAngle - 3.6;
  
  int tip1X = arrowX + tipLen * cos(tipAngle1);
  int tip1Y = arrowY + tipLen * sin(tipAngle1);
  int tip2X = arrowX + tipLen * cos(tipAngle2);
  int tip2Y = arrowY + tipLen * sin(tipAngle2);
  
  tft.drawLine(arrowX, arrowY, tip1X, tip1Y, TFT_ORANGE);
  tft.drawLine(arrowX, arrowY, tip2X, tip2Y, TFT_ORANGE);
  
  rotationAngle += 15;
  if (rotationAngle >= 360) rotationAngle = 0;
}