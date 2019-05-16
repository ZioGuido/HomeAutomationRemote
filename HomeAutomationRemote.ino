//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Remote controller for "GSi Domotica", a personal Home Automation System by Guido Scognamiglio
// Last update: May 2019
// Version with 24H weather forecast + 5 Day forecast
//
// Based on Arduino MKR 1010 WiFi and Zihatec Arduitouch MKR -> https://www.hwhardsoft.de/english/projects/arduitouch-mkr/
//
// Uses:
// - Adafruit GFX library for ILI9341 touch screen display
// - WiFiNina for MKR1010 WiFi
// - FlashStorage for storing data into the flash memory (ATSAM doesn't have its own EEPROM)
// - ArduinoJson for parsing data downloaded from OpenWeatherMap
// - JsonStreamingParser for parsing the 5-day forecast which would require much more RAM than the MKR board has
// - SimpleDHT for reading the DHT11 Temperature & Humidity sensor
// - Font DSEG14_Classic_Bold_Italic_64 from Online font converter: http://oleddisplay.squix.ch/#/home
//
// Requires:
// - Access to a WiFi network
// - An account on OpenWeatherMap
// - Custom functions to operate remotely on your Home Automation System
//

#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "TouchEvent.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "Fonts/DSEG14_Classic_Bold_Italic_64.h"
#include "Fonts/DSEG14_Classic_Italic_36.h"
#include "Fonts/Meteocons_Regular_48.h"
#include "Fonts/Meteocons_Regular_24.h"
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <SimpleDHT.h>
#include <ArduinoJson.h>
#include "JsonStreamingParser.h"
#include "JsonListener.h"
#include <FlashStorage.h>

// PIN definitions
#define TFT_CS   A3
#define TFT_DC   0
#define TFT_MOSI 8
#define TFT_RST  22
#define TFT_CLK  9
#define TFT_MISO 10
#define TFT_LED  A2
#define TOUCH_CS A4
#define TOUCH_IRQ 1
#define PIN_BEEPER  2
#define PIN_DHT11 A1
#define PIN_HEATER_RELAY 3
#define PIN_THERMOSTAT_REMOTE 4
#define PIN_DISPLAY_TYPE A0 // read comments in setup()

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configuration
char ssid[] = "{your SSID here}";
char pass[] = "{your password here}";
IPAddress HomeAutomationServer(24,0,0,254); // Specify the IP to your Home Automation Server
#define NTP_SERVER "3.it.pool.ntp.org" // Change to your preference
#define IDLE_TIME 6 // Seconds of inactivity before returning to the home page
#define BEEP_DURATION 50 // Milliseconds
#define THERMOSTAT_HYSTERESIS 20 // Seconds
// Considering the DHT sensor is located inside the enclosure, it will also capture the circuit's heat, while the actual envinronmental temperature might be lower.
// Therefore a compensation is needed in order to have a reading closer to what's perceived 2 meters away from the circuit.
// This value is a percentage of the actual reading from the sensor, to be added or subtracted, according to the sign.
// In order to be set with accuracy, some comparison is needed possibly with an infrared thermometer, varying this value until the reading becomes stable and accurate.
#define THERMOSTAT_OFFSET -14.f
// These are days and monts in local language (Italian, in my case)
const char *days[] = { "Domenica", "Lunedi", "Martedi", "Mercoledi", "Giovedi", "Venerdi", "Sabato" };
const char *months[] = { "Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno", "Luglio", "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre" };
// OpenWeatherMap account ID and City ID
String OWM_APIKEY = "{your API key here}";
String OWM_CITYID = "{your City ID here}";
#define OWM_UDPATE_FREQ 3600 // Seconds
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Display and touch screen
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
TouchEvent tevent(touch);

// Wi-Fi
WiFiClient client;
int WiFistatus = WL_IDLE_STATUS;
String PIN = "", pinEntry;
String CRLF = "\015\012"; // "\x0D\x0A" // "\r\n" // "\13\10" //

// NTP Client for internet time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER);

// DHT11 Sensor
SimpleDHT11 dht11(PIN_DHT11);
float Temperature, prev_Temperature;

// Weather forecast service
//char WeatherServer[] = "api.openweathermap.org";
IPAddress WeatherServer(37,139,20,5);
struct WeatherData
{
  char description[32];
  char iconID[4];
  float temp;
  float humi;
  float temp_min;
  float temp_max;
  float wind_speed;
  float clouds;
  unsigned long LastTimeUpdated;
} WeatherForecast;

struct Weather24HData
{
  float temp;
  float tmin;
  float tmax;
  unsigned long timestamp;
  char  icon[4];
  unsigned long LastTimeUpdated;
} Forecast24H[8];

struct Weather5DData
{
  float temp[8];
  float humi[8];
  float tmin;
  float tmax;
  float avgh;
  char  icon[4];
  int   nday;
  unsigned long LastTimeUpdated;
} Forecast5D[6];


// Other global variables and definitions
#define SWBTN_TOP_Y   20
#define SWBTN_WIDTH   160
#define SWBTN_HEIGHT  55
unsigned int SecondTimer, IdleTimer;
unsigned int Page;
String nowTime;
bool blinking_dots;

// Relay Switch definitions
struct SwitchDef
{
  String Name;      // Switch name
  int Hidden;       // This switch is hidden?
  int AskConfirm;   // This switch requires a confirmation?
  int Type;         // 0 = Toggle, 1 = Pushbutton
  int Status;       // Stores current state. If it's a pushbutton, this is always 0
} Switch[8];

struct ThermostatData
{
  bool Status;          // Thermostat on/off status
  bool RelayStatus;     // Heater status
  float Temp;           // Threshold temperature
  bool RemoteControl;   // Status of the remote control
  bool HeaterStatus;    // Stores the old status of heater (actually the relay)
  int Hysteresis;       // Counts the hysteresis
} Thermostat;
FlashStorage(ThermostatStore, ThermostatData);

struct WaitFor
{
  bool Wait;
  int Button;
} Confirm;

enum ePages
{
  kPage_Home = 0,
  kPage_Config,
  kPage_Forecast24H,
  kPage_Forecast5D,
  kPage_Switches,
  kPage_Thermostat,
  kPage_Auth,
};

// Declare some prototypes with default values
void UpdateKeyPad(int single = -1);
void UpdateThermostat(int redraw = 3); // 1 = redraw only status, 2 = redraw only temp; 3 = redraw all
String getFromHTTP(String URL, IPAddress server = HomeAutomationServer);

// Code from: http://forum.arduino.cc/index.php?topic=329079.0
// Zone is CET (GMT+1), DST starts last sunday of march at 2:00 and ends last sunday of october at 2:00 (3:00)
bool CheckDST()
{
  bool dst = false;
  int thisMonth = timeClient.getMonth();
  int thisDay = timeClient.getDate();
  int thisWeekday = timeClient.getDay();
  int thisHour = timeClient.getHours();

  if (thisMonth == 10 && thisDay < (thisWeekday + 24)) dst = true;
  if (thisMonth == 10 && thisDay > 24 && thisWeekday == 1 && thisHour < 2) dst = true;
  if (thisMonth < 10 && thisMonth > 3) dst = true;
  if (thisMonth == 3 && thisDay > 24 && thisDay >= (thisWeekday + 24))
    if (!(thisWeekday == 1 && thisHour < 2))
      dst = true;

  return dst;
}

// Class to compute the continuous average of values in a cycling array
class CalcAverage
{
#define AVERAGE_MAX 20
public:
  float readings[AVERAGE_MAX], sum;
  int cnt;

  void Init()
  {
    sum = 0;
    for (cnt=0; cnt<AVERAGE_MAX; cnt++) readings[cnt] = 0;
    cnt = 0;
  }
  void AddValue(float in)
  {
    sum -= readings[cnt];
    readings[cnt] = in;
    sum += readings[cnt];
    if (++cnt >= AVERAGE_MAX) cnt = 0;
  }
  float GetAverage()
  {
    return sum / AVERAGE_MAX;
  }
} TempAverage, HumiAverage;

// Used to catch the touches on buttons
bool isIntersect( int Ax, int Ay, int Aw, int Ah,  int Bx, int By, int Bw, int Bh )
{
  return
    Bx + Bw > Ax &&
    By + Bh > Ay &&
    Ax + Aw > Bx &&
    Ay + Ah > By;
}

void ReadDHT11()
{
  byte temp = 0;
  byte humi = 0;
  int err = SimpleDHTErrSuccess;
  if ((err = dht11.read(&temp, &humi, NULL)) != SimpleDHTErrSuccess)
    return;

  float t = (float)temp / 100.f * (100.f + THERMOSTAT_OFFSET); // Apply offset
  TempAverage.AddValue(t);
  HumiAverage.AddValue((float)humi);
}

String getFromHTTP(String URL, IPAddress server)
{
  client.stop();
  if (client.connect(server, 80))
  {
    // Make a HTTP request:
    String Query = String(
      "GET " + URL + " HTTP/1.1" + CRLF +
      "Host: " + String(server) + ":80" + CRLF +
      "Connection: close" + CRLF +
      CRLF
    );
    client.print(Query);

    // Wait for client, apply a timeout
    unsigned long timeout = millis();
    while (client.available() == 0)
    {
        if (millis() - timeout > 2000)
        {
            client.stop();
            return "TIMEOUT";
        }
    }

    // Get response
    String Response = "";
    while (client.available())
      Response += (char)client.read();

    // Return just the HTML part, discard the HTTP header
    return Response.substring(Response.indexOf(CRLF + CRLF) + 4, Response.length());
  }

  return "OFFLINE";
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Function that defines the HTTP call to check the authorization
void CheckLogin()
{
  // This should be the URL to your automation server to check for the correct pin
  // Or you should implement your own login system...
  String URL = "";
  String HTML = getFromHTTP(URL);

  if (HTML == "TIMEOUT" || HTML == "OFFLINE")
  {
    tft.fillScreen(0);
    tft.setCursor(10, 120);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ILI9341_WHITE);
    tft.print("Server non raggiungibile!"); // "Server unreachable"
    tone(PIN_BEEPER, 880, 500);
    delay(1000);
    draw_screen(kPage_Home);
    return;
  }

  // Login accepted...
  if (HTML == "1")
  {
    PIN = pinEntry;
    draw_screen(kPage_Switches);

  // Login refused...
  } else {
    tft.fillScreen(0);
    tft.setCursor(0, 140);
    tft.setFont(&FreeSansBold24pt7b);
    tft.setTextColor(ILI9341_WHITE);
    tft.print("PIN ERRATO!"); // "WRONG PIN!"
    tone(PIN_BEEPER, 440, 200);
    delay(1000);
    draw_screen(kPage_Home);
  }
}

// Function that defines the HTTP calls to operate the switches
void DoSwitch(int button)
{
  // To do...
  // Implement your own function to send commands to your automation server
}

// Called to get the configuration of all relay switches from the server
bool GetSwitchConfig()
{
  // To do...
  // Implement your own function to retrieve the relay configuration from your server
  // and assign values to these variables...

  for (int r=0; r<8; ++r)
  {
    // Switch[r].Name
    // Switch[r].Hidden
    // Switch[r].AskConfirm
    // Switch[r].Type
    // Switch[r].Status
  }

  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Process clicks in the Switches page
void TouchSwiches(TS_Point p)
{
  // Process YES or NO if the switch is waiting for a confirmation
  if (Confirm.Wait)
  {
    if (isIntersect(p.x, p.y, 1, 1,   0, 120, 120, 120)) // NO
    {
      draw_screen(kPage_Switches);
      Confirm.Button = -1;
      Confirm.Wait = false;
    }

    if (isIntersect(p.x, p.y, 1, 1, 200, 120, 120, 120)) // YES
    {
      DoSwitch(Confirm.Button);
      draw_screen(kPage_Switches);
      Confirm.Button = -1;
      Confirm.Wait = false;
    }

    return;
  }

  // Catch click on a switch button
  int s = 0, button = -1;
  for (int l=0; l<4; ++l)
  {
    int y = SWBTN_TOP_Y + l * SWBTN_HEIGHT;
    for (int c=0; c<2; ++c)
    {
      int x = c * SWBTN_WIDTH;
      if (isIntersect(p.x, p.y, 1, 1, x, y, SWBTN_WIDTH, SWBTN_HEIGHT))
      {
        button = s;
        break;
      }
      s++;
    }
  }

  // Ignore "hidden" switches
  if (Switch[button].Hidden)
    return;

  // Ask confirmation if required
  if (Switch[button].AskConfirm)
  {
    tft.fillScreen(0);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 50);
    tft.print(Switch[button].Name);

    tft.setFont(&FreeSansBold24pt7b);
    tft.setCursor(10, 100);
    tft.print("Confermare?"); // "Confirm?"

    tft.setTextColor(ILI9341_BLACK);
    tft.fillRoundRect(  0, 120, 120, 120, 16, ILI9341_RED);   tft.setCursor(    25, 120 + 72); tft.print("NO");
    tft.fillRoundRect(200, 120, 120, 120, 16, ILI9341_GREEN); tft.setCursor(200+35, 120 + 72); tft.print("SI");

    Confirm.Wait = true;
    Confirm.Button = button;
    return;
  }

  // Proceed
  DoSwitch(button);
  UpdateKeyPad(button);
}

// Process clicks on the Login page
void TouchLogin(TS_Point p)
{
  tft.setFont(&FreeSansBold24pt7b);
  tft.setTextColor(ILI9341_WHITE);

  // Clicks on the digit buttons
  int n = 0, x, y;
  for (int r=0; r<2; ++r)
  {
    y = 110 + r * 64;
    for (int c=0; c<5; ++c)
    {
      x = c * 64;
      if (isIntersect(p.x, p.y, 1, 1, x, y, 64, 64))
      {
        if (pinEntry.length() < 4)
        {
          pinEntry += n;
          tft.setCursor(0+16 + pinEntry.length()*24, 30 + 48);
          tft.print("*");
          break;
        }
      }
      n++;
    }
  }

  // Click the OK button
  if (isIntersect(p.x, p.y, 1, 1, 320-64*2, 30, 64*2, 64))
  {
    CheckLogin();
  }
}

void TouchThermostat(TS_Point p)
{
  // On/Off Status
  if (isIntersect(p.x, p.y, 1, 1, 0, 30, 320, 60))
  {
    Thermostat.Status = !Thermostat.Status;
    // Make sure that the heater relay is off when the thermostat is turned off
    if (!Thermostat.Status)
    {
      Thermostat.RelayStatus = false;
      digitalWrite(PIN_HEATER_RELAY, LOW);
    }
    UpdateThermostat(1);
    return;
  }

  // Minus, decrease desired temperature
  if (isIntersect(p.x, p.y, 1, 1, 0, 180, 120, 60))
  {
    if (Thermostat.Temp > 15.f)
    {
      Thermostat.Temp -= 0.5f;
      ThermostatStore.write(Thermostat);
      UpdateThermostat(2);
    }
    return;
  }

  // Plus, increase desired temperature
  if (isIntersect(p.x, p.y, 1, 1, 200, 180, 120, 60))
  {
    if (Thermostat.Temp < 30.f)
    {
      Thermostat.Temp += 0.5f;
      ThermostatStore.write(Thermostat);
      UpdateThermostat(2);
    }
    return;
  }
}

// Single click event
void onClick(TS_Point p)
{
  tone(PIN_BEEPER, 1760, BEEP_DURATION);

  // Reboot as soon as the display is touched in case the WiFi connection is lost
  if (WiFi.status() != WL_CONNECTED)
    NVIC_SystemReset();

  switch(Page)
  {
    case kPage_Home:
      // Touch the temperature area, go to thermostat page
      if (isIntersect(p.x, p.y, 1, 1, 200, 170, 120, 70))
        draw_screen(kPage_Thermostat);

      // Touch the weather area
      else
      if (isIntersect(p.x, p.y, 1, 1, 0, 140, 200, 200))
        draw_screen(kPage_Forecast24H);

      // Touch elsewhere
      else
        // If PIN is not set, go to login page, else go to switches page
        draw_screen(PIN != "" ? kPage_Switches : kPage_Auth);

      IdleTimer = IDLE_TIME;
      break;

    case kPage_Auth:
      IdleTimer = IDLE_TIME;
      TouchLogin(p);
      break;

    case kPage_Switches:
      TouchSwiches(p);
      IdleTimer = IDLE_TIME;
      break;

    case kPage_Thermostat:
      TouchThermostat(p);
      IdleTimer = IDLE_TIME;
      break;

    case kPage_Config:
      break;

    case kPage_Forecast24H:
      draw_screen(kPage_Forecast5D);
      break;

    case kPage_Forecast5D:
      draw_screen(kPage_Home);
      break;
  }
}

void onDblClick(TS_Point p)
{
}

void onLongClick(TS_Point p)
{
  // Reset after a long click in the home screen
  if (Page <= kPage_Home) NVIC_SystemReset();
}

/*
void onSwipe(uint8_t dir) // dir = 0 = right to left, 1 = left to right
{
  switch(dir) {
    case 0:
      break;
    case 1:
      break;
  }
}
*/

void draw_screen(int pg)
{
  Page = pg;
  nowTime = "";

  switch(Page)
  {
    case kPage_Home:
      prev_Temperature = 0;
      tft.fillScreen(0);
      UpdateDateString();
      UpdateHomePage();
      drawWeatherInfo();
      break;

    case kPage_Auth:
      LoginPage();
      break;

    case kPage_Switches:
      Confirm.Wait = false;
      Confirm.Button = -1;

      tft.fillScreen(0);
      tft.setFont(&FreeSans9pt7b);
      tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
      tft.setCursor(10, 120);
      tft.print("Ricezione della configurazione..."); // "Downloading configuration"
      if (GetSwitchConfig())
      {
        tft.fillScreen(0);
        UpdateStatusBar();
        UpdateKeyPad();
      }
      break;

    case kPage_Thermostat:
      tft.fillScreen(0);
      UpdateStatusBar();
      UpdateThermostat();
      break;

    case kPage_Forecast24H:
      Show24HForecast();
      break;

    case kPage_Forecast5D:
      Show5DForecast();
      break;

    // To do...
    case kPage_Config:

      break;
  }
}

// Draws the 8 switch buttons
void UpdateKeyPad(int single)// = -1)
{
  tft.setFont(&FreeSans9pt7b);

  int s = -1;
  for (int l=0; l<4; ++l)
  {
    int y = SWBTN_TOP_Y + l * SWBTN_HEIGHT;
    for (int c=0; c<2; ++c)
    {
      s++;
      // Don't draw "hidden" switches
      if (Switch[s].Hidden) continue;

      int x = c * SWBTN_WIDTH;
      uint16_t bgcolor = Switch[s].Status ? ILI9341_RED : ILI9341_PURPLE;
      uint16_t txcolor = Switch[s].Status ? ILI9341_BLACK : ILI9341_WHITE;
      String Label = Switch[s].Name;

      if (single > -1 && s != single)
        continue;

      tft.fillRoundRect(x, y, SWBTN_WIDTH-1, SWBTN_HEIGHT-1, 5, bgcolor);
      tft.drawRoundRect(x, y, SWBTN_WIDTH-1, SWBTN_HEIGHT-1, 5, ILI9341_WHITE);
      tft.setCursor(x + 5, y + 30);
      tft.setTextColor(txcolor, bgcolor);
      tft.print(Label);
    }
  }
}

// See: https://openweathermap.org/weather-conditions
String GetWeatherIcon(String s)
{
  if (s.length() > 3) return "";

  // clear sky
  if (s == "01d") return("1");
  if (s == "01n") return("2");

  // few clouds
  if (s == "02d") return("3");
  if (s == "02n") return("4");

  // scattered clouds
  if (s.toInt() == 3) return("5");

  // broken clouds
  if (s.toInt() == 4) return("Y");

  // shower rain
  if (s.toInt() == 9) return("R");

  // rain
  if (s.toInt() == 10) return("8");

  // thunderstorm
  if (s.toInt() == 11) return("6");

  // snow
  if (s.toInt() == 13) return("W");

  // mist
  if (s.toInt() == 50) return("M");
}

void drawWeatherInfo()
{
  if (WeatherForecast.LastTimeUpdated <= 0) return;

  tft.fillRect(0, 140, 200, 200, 0);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(ILI9341_WHITE);

  char line[64];
  sprintf(line, "%s", WeatherForecast.description);
  tft.setCursor(4, 158); tft.print(line);

  sprintf(line, "T: %2.1f, H: %2.f%%", WeatherForecast.temp, WeatherForecast.humi);
  tft.setCursor(4, 178); tft.print(line);

  sprintf(line, "%2.1f ~ %2.1f", WeatherForecast.temp_min, WeatherForecast.temp_max);
  tft.setCursor(4, 198); tft.print(line);

  sprintf(line, "Nuvole: %2.1f%%", WeatherForecast.clouds);
  tft.setCursor(4, 218); tft.print(line);

  sprintf(line, "Vento: %3.1f km/h", WeatherForecast.wind_speed * 1.852f); // Knots to Km/h
  tft.setCursor(4, 238); tft.print(line);

  // Draw icon...
  tft.fillRect(240, 20, 80, 80, 0);
  tft.setFont(&Meteocons_Regular_48);
  tft.setCursor(260, 90);
  tft.print(GetWeatherIcon(WeatherForecast.iconID));
}

void getOpenWeatherMap()
{
  // Query string parameter cnt=1 downloads only one array item out of 40, which is the weather situation in the next 3 hours
  String WeatherURL = "/data/2.5/forecast?id=" + OWM_CITYID + "&units=metric&cnt=1&lang=it&APPID=" + OWM_APIKEY;
  String result = getFromHTTP(WeatherURL, WeatherServer);

  if (result == "TIMEOUT" || result == "OFFLINE")
    return;

  // Computed from: https://arduinojson.org/v6/assistant/
  const size_t capacity = 2*JSON_ARRAY_SIZE(1) + 3*JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(2) + 2*JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 2*JSON_OBJECT_SIZE(8) + 330;
  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, result);
  JsonArray list = doc["list"];

  sprintf(WeatherForecast.description, "%s", (const char*)list[0]["weather"][0]["description"]);
  sprintf(WeatherForecast.iconID, "%s", (const char*)list[0]["weather"][0]["icon"]);
  WeatherForecast.temp = (float)list[0]["main"]["temp"];
  WeatherForecast.humi = (float)list[0]["main"]["humidity"];
  WeatherForecast.temp_min = (float)list[0]["main"]["temp_min"];
  WeatherForecast.temp_max = (float)list[0]["main"]["temp_max"];
  WeatherForecast.wind_speed = (float)list[0]["wind"]["speed"];
  WeatherForecast.clouds = (float)list[0]["clouds"]["all"];
  WeatherForecast.LastTimeUpdated = timeClient.getEpochTime();
}

void Get24HForecast()
{
  // Query string parameter cnt=8 downloads only 8 out of 40 possible array items, which is the next 24 hours (one update every 3 hours)
  String WeatherURL = "/data/2.5/forecast?id=" + OWM_CITYID + "&units=metric&cnt=8&lang=it&APPID=" + OWM_APIKEY;
  String result = getFromHTTP(WeatherURL, WeatherServer);

  if (result == "TIMEOUT" || result == "OFFLINE")
    return;

  // Computed from: https://arduinojson.org/v6/assistant/
  const size_t capacity = 8*JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(8) + 23*JSON_OBJECT_SIZE(1) + 9*JSON_OBJECT_SIZE(2) + 9*JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(7) + 15*JSON_OBJECT_SIZE(8) + 1930;
  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, result);
  JsonArray list = doc["list"];

  for (int h=0; h<8; ++h)
  {
    Forecast24H[h].temp = (float)list[h]["main"]["temp"];
    Forecast24H[h].tmin = (float)list[h]["main"]["temp_min"];
    Forecast24H[h].tmax = (float)list[h]["main"]["temp_max"];
    Forecast24H[h].timestamp = (unsigned long)list[h]["dt"];
    sprintf(Forecast24H[h].icon, "%s", (const char*)list[h]["weather"][0]["icon"]);
  }
  Forecast24H[0].LastTimeUpdated = timeClient.getEpochTime();
}

void Show24HForecast()
{
  // Check if it's time to update the weather forecast
  if (timeClient.getEpochTime() - Forecast24H[0].LastTimeUpdated >= OWM_UDPATE_FREQ)
  {
    tft.fillScreen(0);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(0, 120);
    tft.print("Ricezione dati meteo..."); // "Download weather informations..."
    Get24HForecast();
  }

  tft.fillScreen(0);
  UpdateStatusBar();
  char txt[8];

  // Draw scale
  tft.setTextColor(tft.color565(128, 128, 128));
  tft.setFont();
  for (int h=0; h<8; ++h)
  {
    int y = 80 + h * 20;
    int g = 50 - h * 10;
    tft.setCursor(10, y-3);
    sprintf(txt, "%+02d", g);
    tft.print(txt);
    tft.drawLine(30, y, 320, y, tft.color565(80, 80, 80));
  }

  // Draw histograms
  for (int h=0; h<8; ++h)
  {
    int x = 40 + h * 35;
    int y1 = 180 - (int)Forecast24H[h].temp * 2;
    int y2 = 180 - (int)Forecast24H[h].tmin * 2;
    int y3 = 180 - (int)Forecast24H[h].tmax * 2;

    // Draw icon...
    tft.setTextColor(ILI9341_WHITE);
    tft.setFont(&Meteocons_Regular_24);
    tft.setCursor(x, 70);
    tft.print(GetWeatherIcon(Forecast24H[h].icon));

    // Print max temperature
    tft.setTextColor(ILI9341_WHITE);
    tft.setFont();
    tft.setCursor(x+1, y1 - 10);
    sprintf(txt, "%02.1f", Forecast24H[h].tmax);
    tft.print(txt);

    // Draw temperature bar
    tft.fillRect(x, y1, 25, 230-y1, tft.color565(128, 128, 128));

    // Print min temperature
    tft.setTextColor(ILI9341_YELLOW);
    tft.setCursor(x+1, y2 + 10);
    sprintf(txt, "%02.1f", Forecast24H[h].tmin);
    tft.print(txt);

    // Print time
    sprintf(txt, "%02d:%02d", (Forecast24H[h].timestamp % 86400L) / 3600, (Forecast24H[h].timestamp % 3600) / 60);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(x-2, 231);
    tft.print(txt);

    // Draw min & max lines
    if (h < 7)
    {
      int y2b = 180 - (int)Forecast24H[h+1].tmin * 2;
      int y3b = 180 - (int)Forecast24H[h+1].tmax * 2;
      tft.drawLine(x, y2+1, x + 35, y2b+1, ILI9341_BLUE);
      tft.drawLine(x, y3, x + 35, y3b, ILI9341_RED);
    }
  }
}

// Custom parser for the JSON Steam coming from OpenWeatherMap
class OWMListener : public JsonListener
{
public:
  void Init()
  {
    d = -1;
    h = 0;
    prev_day = -1;
    _count = 0;
  }

  // Get the key name
  virtual void key(String k)
  {
    last_key = k;
  }

  // Get the key value
  virtual void value(String v)
  {
    if (last_key == "")
    {
      delay(5);
      return;
    }

    // Funny fancy animation of random colored dots that appear while the values are parsed from the json stream
    tft.setCursor(10 + random(310), 10 + random(230));
    tft.setTextColor(tft.color565(32 + random(223), 32 + random(223), 32 + random(223)));
    tft.print(".");

    if (last_key == "dt")
    {
      _count++;

      // Get the timestamp, calc the day and hour
      unsigned long timestamp = (unsigned long)v.toInt();
      int _day = ((timestamp / 86400L) + 4 ) % 7; // 0 is Sunday
      _hour = (timestamp % 86400L) / 3600;

      // If it's a new day, shift to next day and reset daily forecast
      if (prev_day != _day)
      {
        d++;
        h = 0;
        Forecast5D[d].nday = _day;
        prev_day = _day;
      }
      // In the same day, just shift to next forecast
      else
      {
        h++;
      }
    }

    // Get the temperature and store it into the daily array
    if (last_key == "temp")
    {
      Forecast5D[d].temp[h] = v.toFloat();
    }

    // Get the relative humidity and store it into the daily array
    if (last_key == "humidity")
    {
      Forecast5D[d].humi[h] = v.toFloat();
    }

    // Get the icon for the forecast of the midday
    if (last_key == "icon" && _hour == 12)
    {
      strcpy(Forecast5D[d].icon, v.c_str());
    }
  }

  // Apparently, parsing too fast causes corrupted values, so let's add some slight delay
  virtual void whitespace(char c)   { delay(5); }
  virtual void startDocument()      { delay(5); last_key = ""; }
  virtual void startObject()        { delay(5); last_key = ""; }
  virtual void endObject()          { delay(5); last_key = ""; }
  virtual void startArray()         { delay(5); last_key = ""; }
  virtual void endArray()           { delay(5); last_key = ""; }
  virtual void endDocument()        { delay(5); last_key = ""; }

  int getCount() { return _count; }

private:
  String last_key;
  int prev_day, _hour;
  int d, h;
  int _count;
};

// This function downloads the full 5-day forecast from OWM. Since it's too big to be parsed with ArduinoJson, we use a streaming parser library
// that will do the parsing using the custom class here above while the data comes from the web server, with the lowest ram usage possible.
void Get5DForecast()
{
  // Reset forecast memory
  for (int d = 0; d<6; ++d)
  {
     // Fill with impossible temperature values in order to recognize empty fields
    Forecast5D[d].tmin = 1000.f;
    Forecast5D[d].tmax = 1000.f;
    Forecast5D[d].avgh = 1000.f;
    Forecast5D[d].nday = 0;
    strcpy(Forecast5D[d].icon, "");
    for (int h = 0; h<8; ++h)
    {
      Forecast5D[d].temp[h] = 1000.f;
      Forecast5D[d].humi[h] = 1000.f;
    }
  }

  // Initialize the JSON Streaming Parser
  JsonStreamingParser parser;
  OWMListener listener;
  listener.Init();
  parser.setListener(&listener);

  tft.setFont();
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(0, 10);

  // Download the whole forecast (40 items, 8 a day for 5 days)
  String WeatherURL = "/data/2.5/forecast?id=" + OWM_CITYID + "&units=metric&lang=it&APPID=" + OWM_APIKEY;

  // Start HTTP connection
  client.stop();
  if (client.connect(WeatherServer, 80))
  {
    // Make a HTTP request:
    String Query = String(
      "GET " + WeatherURL + " HTTP/1.1" + CRLF +
      "Host: " + String(WeatherServer) + ":80" + CRLF +
      "Connection: close" + CRLF +
      CRLF
    );
    client.print(Query);

    // Wait for client, apply a timeout
    unsigned long timeout = millis();
    while (client.available() == 0)
    {
        if (millis() - timeout > 2000)
        {
            client.stop();
            return;
        }
    }

    // Skip response headers
    client.find("\r\n\r\n");

    while (client.available())
      parser.parse((char)client.read());

    // If the parsing fails, wait 3 seconds then retry...
    if (listener.getCount() != 40)
    {
      tft.setFont();
      tft.setTextColor(ILI9341_WHITE);
      tft.setCursor(0, 140);
      for (int s=0; s<30; ++s) { tft.print("."); delay(100); }
      Get5DForecast();
      return;
    }

    // Calculate daily min & max temperatures
    // Also calculate daily average humidity
    for (int d = 0; d<6; ++d)
    {
      float _min = 100.f, _max = -100.f;
      float avg_humi = 0;
      for (int h = 0; h<8; ++h)
      {
        if (Forecast5D[d].temp[h] == 1000.f) continue; // Ignore unexistant values
        if (Forecast5D[d].temp[h] > _max) _max = Forecast5D[d].temp[h];
        if (Forecast5D[d].temp[h] < _min) _min = Forecast5D[d].temp[h];

        if (Forecast5D[d].humi[h] != 1000.f) avg_humi += Forecast5D[d].humi[h];
      }

      Forecast5D[d].tmin = _min;
      Forecast5D[d].tmax = _max;
      Forecast5D[d].avgh = avg_humi / 8.f;
    }

    Forecast5D[0].LastTimeUpdated = timeClient.getEpochTime();
  }
}

void Show5DForecast()
{
  // Check if it's time to update the weather forecast
  if (timeClient.getEpochTime() - Forecast5D[0].LastTimeUpdated >= OWM_UDPATE_FREQ)
  {
    tft.fillScreen(0);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(0, 120);
    tft.print("Ricezione dati meteo..."); // "Download weather informations..."
    Get5DForecast();
  }

  tft.fillScreen(0);
  UpdateStatusBar();
  char txt[8];

  // Draw scale
  tft.setTextColor(tft.color565(128, 128, 128));
  tft.setFont();
  for (int h=0; h<8; ++h)
  {
    int y = 80 + h * 20;
    int g = 50 - h * 10;
    tft.setCursor(10, y-3);
    sprintf(txt, "%+02d", g);
    tft.print(txt);
    tft.drawLine(30, y, 320, y, tft.color565(80, 80, 80));
  }

  // Draw histograms
  int ix = 0;
  for (int d=0; d<6; ++d)
  {
    if (String(Forecast5D[d].icon) == "" || Forecast5D[d].tmin == 1000.f || Forecast5D[d].tmax == 1000.f)
      continue;

    int x = 40 + ix++ * 55;
    int y1 = 180 - (int)Forecast5D[d].tmin * 2;
    int y2 = 180 - (int)Forecast5D[d].tmax * 2;

    // Draw icon...
    tft.setTextColor(ILI9341_WHITE);
    tft.setFont(&Meteocons_Regular_24);
    tft.setCursor(x + 10, 70);
    tft.print(GetWeatherIcon(Forecast5D[d].icon));

    // Draw bars
    tft.fillRect(x     , y2, 25, 230-y2, tft.color565(255,  64,  64)); // max
    tft.fillRect(x + 25, y1, 25, 230-y1, tft.color565( 64,  64, 255)); // min

    tft.setFont();

    // Print max temperature
    tft.setCursor(x, y2 + 2);
    sprintf(txt, "%02.1f", Forecast5D[d].tmax);
    tft.print(txt);

    // Print min temperature
    tft.setCursor(x+26, y1 + 3);
    sprintf(txt, "%02.1f", Forecast5D[d].tmin);
    tft.print(txt);

    // Print himidity
    tft.setTextColor(tft.color565(196, 196, 196));
    tft.setCursor(x+1, y2 - 12);
    sprintf(txt, "H:%02.1f%%", Forecast5D[d].avgh);
    tft.print(txt);

    // Print day
    tft.setTextColor(ILI9341_WHITE);
    sprintf(txt, "%s", days[Forecast5D[d].nday]);
    tft.setCursor(x, 231);
    tft.print(txt);
  }
}

void UpdateDateString()
{
  tft.fillRect(0, 0, 320, 20, ILI9341_BLACK);
  char date[32]; sprintf(date, "%s %d %s %d", days[timeClient.getDay()], timeClient.getDate(), months[timeClient.getMonth()-1], timeClient.getYear());
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 15);
  tft.print(date);
}

void UpdateHomePage()
{
  // Blink the two dots between hours and minutes every second
  blinking_dots = !blinking_dots;
  tft.setFont(&DSEG14_Classic_Bold_Italic_64);
  tft.setTextColor(blinking_dots ? ILI9341_BLACK : ILI9341_WHITE);
  tft.setCursor(116, 96);
  tft.print(":");

  // Get temperature from the DHT sensor and apply the offset
  Temperature = TempAverage.GetAverage();// / 100.f * (100.f + THERMOSTAT_OFFSET); // Apply offset

  // Update time every minute
  char now[16]; sprintf(now, "%02d:%02d", timeClient.getHours(), timeClient.getMinutes());
  if (nowTime != now)
  {
    // Update whole date string at midnight
    if ((timeClient.getHours() + timeClient.getMinutes() + timeClient.getSeconds()) == 0)
    {
      UpdateDateString();
      timeClient.update();
    }
    timeClient.setTimeOffset(CheckDST() ? 3600*2 : 3600);

    tft.setFont(&DSEG14_Classic_Bold_Italic_64);

    // Delete old text
    tft.setTextColor(ILI9341_BLACK);
    tft.setCursor(10, 96);
    tft.print(nowTime);

    // Print updated text
    nowTime = now;
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 96);
    tft.print(nowTime);

    // Check if it's time to update the weather forecast
    if (timeClient.getEpochTime() - WeatherForecast.LastTimeUpdated >= OWM_UDPATE_FREQ)
    {
      getOpenWeatherMap();
      drawWeatherInfo();
    }
  }

  // Update thermometer only if there's a temperature change
  if (Temperature != prev_Temperature)
  {
    prev_Temperature = Temperature;

    uint16_t bgcolor = tft.color565(32, 32, 32); // Thermostat is Off
    uint16_t fgcolor = Thermostat.Status ? ILI9341_BLACK : ILI9341_WHITE;
    if (Thermostat.Status) bgcolor = ILI9341_YELLOW;  // Thermostat on but relay off
    if (Thermostat.RelayStatus) bgcolor = ILI9341_RED;  // Thermostat on and relay on

    char th[8];
    tft.fillRect(200, 170, 120, 70, bgcolor); // also touch area to open the thermostat page
    tft.setTextColor(fgcolor);

    tft.setFont(&DSEG14_Classic_Italic_36);
    sprintf(th, "%02.1f", Temperature);
    tft.setCursor(205, 218); tft.print(th);
    tft.drawCircle(310, 185, 5, fgcolor); tft.drawCircle(310, 185, 4, fgcolor);

    tft.setFont(&FreeSans9pt7b);
    sprintf(th, "H: %02.1f%%", HumiAverage.GetAverage());
    tft.setCursor(240, 236); tft.print(th);
  }
}

void UpdateStatusBar()
{
  // Don't update in the confirmation page
  if (Confirm.Wait) return;

  char now[16]; sprintf(now, "%02d:%02d", timeClient.getHours(), timeClient.getMinutes());
  if (nowTime == now) return;

  tft.fillRect(0, 0, 320, 20, ILI9341_PINK);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(ILI9341_BLACK, ILI9341_PINK);

  // Print Time
  nowTime = now;
  tft.setCursor(5, 16);
  tft.print(nowTime + "      IP: ");
  tft.print(WiFi.localIP());
  tft.print("      " + String(WiFi.RSSI()) + "dBm");
}

void LoginPage()
{
  pinEntry = "";

  tft.fillScreen(0);
  UpdateStatusBar();

  tft.setFont(&FreeSansBold24pt7b);
  tft.setTextColor(ILI9341_BLACK);

  tft.drawRoundRect(0, 30, 64*3-1, 64-1, 16, ILI9341_YELLOW);
  tft.fillRoundRect(320-64*2, 30, 64*2-1, 64-1, 16, ILI9341_RED);
  tft.drawRoundRect(320-64*2, 30, 64*2-1, 64-1, 16, ILI9341_YELLOW);
  tft.setCursor(320-64*2+24, 30 + 48);
  tft.print("OK");

  int n = 0, x, y;
  for (int r=0; r<2; ++r)
  {
    y = 110 + r * 64;
    for (int c=0; c<5; ++c)
    {
      x = c * 64;
      tft.fillRoundRect(x, y, 64-1, 64-1, 16, ILI9341_WHITE);
      tft.setCursor(x+18, y + 46);
      tft.print(n++);
    }
  }
}

void UpdateThermostat(int redraw)
{
  // Status button
  if (redraw & 1) // 1||3
  {
    tft.setFont(&FreeSansBold24pt7b);
    tft.setTextColor(ILI9341_BLACK);
    tft.fillRoundRect(0, 30, 320, 60, 16, Thermostat.Status ? ILI9341_GREEN : tft.color565(96,96,96));
    tft.setCursor(65, 80); tft.print(Thermostat.Status ? "ACCESO" : "SPENTO"); // "ON" : "OFF"
  }

  // Desired temperature
  if (redraw >> 1) // 2||3
  {
    tft.setFont(&DSEG14_Classic_Bold_Italic_64);
    char t[8]; sprintf(t, "%2.1f", Thermostat.Temp);
    tft.fillRect(0, 100, 320, 70, ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(80, 168); tft.print(t);
  }

  // Minus & Plus buttons
  if (redraw == 3)
  {
    tft.setFont(&FreeSansBold24pt7b);
    tft.setTextColor(ILI9341_BLACK);
    tft.fillRoundRect(  0, 180, 120, 60, 16, ILI9341_BLUE); tft.setCursor(    50, 220); tft.print("-");
    tft.fillRoundRect(200, 180, 120, 60, 16, ILI9341_RED);  tft.setCursor(200+50, 220); tft.print("+");
  }
}

void WiFiConnect()
{
  tft.fillRect(0, 100, 320, 40, 0);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 120);
  tft.print("Connessione in corso..."); // "Connecting to server..."

  int attempts = 3;
  // Attempt to connect to Wifi network:
  while (WiFistatus != WL_CONNECTED)
  {
    WiFistatus = WiFi.begin(ssid, pass);
    delay(1000);
    if (--attempts <= 0)
    {
      tft.fillRect(0, 100, 320, 40, 0);
      tft.setCursor(10, 120);
      tft.print("Rete Wi-Fi non raggiungibile"); // "Unable to connect to Wi-Fi network"
      break;
    }
  }
}

void setup()
{
  SecondTimer = 0;
  IdleTimer = 0;
  TempAverage.Init();
  HumiAverage.Init();

  pinMode(PIN_BEEPER, OUTPUT);

  // Init display
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(0);

  // If the X coordinates of the touch sensor respond reversed, tie A0 to ground
  pinMode(PIN_DISPLAY_TYPE, INPUT_PULLUP);

  // Turn backlight on
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, LOW);

  // Init TouchEvent instance
  touch.begin();
  if (digitalRead(PIN_DISPLAY_TYPE) == HIGH)
    tevent.setResolution(tft.width(), tft.height());    // Normal display
  else
    tevent.setResolution(-tft.width(), tft.height());   // Touch sensor with X axis reversed
  tevent.setDblClick(300);
  tevent.registerOnTouchClick(onClick);
  tevent.registerOnTouchDblClick(onDblClick);
  tevent.registerOnTouchLong(onLongClick);
  //tevent.registerOnTouchSwipe(onSwipe);

  // Start WiFi connection
  WiFiConnect();
  if (WiFi.status() != WL_CONNECTED)
  {
    tone(PIN_BEEPER, 880, 1000);
    Page = -1;
    // Skip the rest of the initialization
    return;
  }

  // Start NTP Client
  timeClient.begin();
  timeClient.update();
  timeClient.setTimeOffset(CheckDST() ? 3600*2 : 3600); // 3600 is Timezone CET (GMT+1)

  // Setup Weather forecast service
  Forecast24H[0].LastTimeUpdated = WeatherForecast.LastTimeUpdated = 0;
  getOpenWeatherMap();

  // Setup thermostat
  Thermostat = ThermostatStore.read();
  if (Thermostat.Temp <= 0 || Thermostat.Temp >= 30.f) Thermostat.Temp = 20.f;
  Thermostat.Status = Thermostat.RelayStatus = Thermostat.HeaterStatus = false;
  Thermostat.RemoteControl = HIGH;
  Thermostat.Hysteresis = 0;
  pinMode(PIN_HEATER_RELAY, OUTPUT); digitalWrite(PIN_HEATER_RELAY, LOW);
  pinMode(PIN_THERMOSTAT_REMOTE, INPUT_PULLUP);
  prev_Temperature = 0;

  // Start with home page
  draw_screen(kPage_Home);
  tone(PIN_BEEPER, 1760, BEEP_DURATION);
}

void loop()
{
  tevent.pollTouchScreen();

  // Nothing to do here if there's no WiFi connection...
  if (WiFi.status() != WL_CONNECTED)
    return;

  // This is a timer that should update every second
  if ((millis() - SecondTimer) >= 1000)
  {
    SecondTimer = millis();

    // Read DHT11 Sensor
    ReadDHT11();

    // Check for Thermostat remote control, act only once when the remote control changes its status
    bool TRC = digitalRead(PIN_THERMOSTAT_REMOTE) == LOW ? true : false;
    if (Thermostat.RemoteControl != TRC)
    {
      Thermostat.RemoteControl = TRC;
      if (Thermostat.Status != Thermostat.RemoteControl)
      {
        Thermostat.Status = Thermostat.RemoteControl;

        // Make sure the relay is released when the thermostat is turned off
        if (!Thermostat.Status)
        {
          Thermostat.RelayStatus = false;
          digitalWrite(PIN_HEATER_RELAY, LOW);
        }

        // Refresh home page?
        if (Page == kPage_Home)
        {
          // Force update of temperature box
          prev_Temperature = 0;
          tone(PIN_BEEPER, 1760, BEEP_DURATION);
        }
      }
    }

    // If the thermostat is on, check the temperature, compare with threshold and control the relay accordingly
    if (Thermostat.Status)
    {
        bool HeaterNewStatus = Temperature < Thermostat.Temp ? true : false;

        // If there's a change to be done, don't do it immediately, wait some time.
        // This avoids unwanted switchoffs in case the temperature reading is unstable.
        if (Thermostat.HeaterStatus != HeaterNewStatus)
        {
          Thermostat.HeaterStatus = HeaterNewStatus;
          Thermostat.Hysteresis = THERMOSTAT_HYSTERESIS;
        }

        if (Thermostat.Hysteresis > 0) Thermostat.Hysteresis--;
        if (Thermostat.Hysteresis == 0)
        {
          if (Thermostat.RelayStatus != HeaterNewStatus)
          {
            Thermostat.RelayStatus = HeaterNewStatus;
            digitalWrite(PIN_HEATER_RELAY, Thermostat.RelayStatus ? HIGH : LOW);

            // Refresh home page?
            if (Page == kPage_Home)
            {
              // Force update of temperature box
              prev_Temperature = 0;
            }
          }
        }
    }

    // Update screen informations
    if (Page == kPage_Home)
      UpdateHomePage();
    else
      UpdateStatusBar();

    // Count seconds, return to home page after a given idle time
    if (Page >= kPage_Switches)
    {
      if (IdleTimer > 0) IdleTimer--;
      if (IdleTimer == 0) draw_screen(kPage_Home);
    }
  }
}
