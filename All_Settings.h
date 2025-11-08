//  Use the OpenWeather library: https://github.com/Bodmer/OpenWeather

//  The weather icons and fonts are in the sketch data folder, press Ctrl+K
//  to view.

// The ESP32 board support package 2.0.0 or later must be loaded in the
// Arduino boards manager to provide LittleFS support.

//            >>>       IMPORTANT TO PREVENT CRASHES      <<<
//>>>>>>  Set LittleFS to at least 1.5Mbytes before uploading files  <<<<<<

//            >>>           DON'T FORGET THIS             <<<
//  Upload the fonts and icons to LittleFS using the "Tools" menu option.

// You can change the "User_Setup.h" file inside the OpenWeather
// to shows the data stream from the server.

//////////////////////////////
// Settings defined below

#define WIFI_SSID      "ORBI62"
#define WIFI_PASSWORD  "myleetkey"

//#define TIMEZONE UK // See NTP_Time.h tab for other "Zone references", UK, usMT etc
#define TIMEZONE NZ

// Update every 15 minutes, up to 1000 request per day are free (viz average of ~40 per hour)
const int UPDATE_INTERVAL_SECS = 15UL * 60UL;  // 15 minutes

// Pins for the TFT interface are defined in the User_Config.h file inside the TFT_eSPI library

// For units use "metric" or "imperial"
const String units = "metric";

// Sign up for a key and read API configuration info here:
// https://openweathermap.org/, change x's to your API key
const String api_key = "ccd54e436bd392c4e6f664a77f68a462";

// Set the forecast longitude and latitude to at least 4 decimal places

// New York, Empire State Building
// 40.749778527083656, -73.98629815117553
const String latitude =  "-43.5321"; // 90.0000 to -90.0000 negative for Southern hemisphere
const String longitude = "172.6362"; // 180.000 to -180.000 negative for West

// For language codes see https://openweathermap.org/current#multi
const String language = "en";  // Default language = en = English

// OpenUV API configuration
const String OPENUV_API_URL = "https://api.openuv.io/api/v1/uv";
const String OPENUV_API_KEY = "openuv-2bngarmh8l2aho-io"; // replace with your key if needed

// Display and UI
#define AA_FONT_SMALL "fonts/NSBold15"
#define AA_FONT_LARGE "fonts/NSBold36"
const uint8_t BACKLIGHT = 100;     // 0-255
const bool USE_24H = true;         // 24h time display preference (if used)

// NTP (optional; used by NTP_Time.h if wired up)
const char NTP_PRIMARY[] = "pool.ntp.org";
const char NTP_FALLBACK[] = "time.google.com";

// Update cadences
const uint32_t UV_UPDATE_INTERVAL_SECS = 30UL * 60UL;   // 30 minutes

// HTTP behavior
const uint16_t HTTP_TIMEOUT_MS = 6000; // request timeout
const uint8_t HTTP_RETRIES = 2;        // retry count for transient failures

// Short day of week abbreviations used in 4 day forecast (change to your language)
const String shortDOW[8] = { "???", "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// Change the labels to your language here:
const char sunStr[] = "Sun";
const char cloudStr[] = "Cloud";
const char humidityStr[] = "Humidity";
const String moonPhase[8] = { "New", "Waxing", "1st qtr", "Waxing", "Full", "Waning", "Last qtr", "Waning" };

// End of user settings
//////////////////////////////
