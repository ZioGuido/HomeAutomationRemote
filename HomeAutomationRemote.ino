//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Remote controller project for a personal Home Automation System
// Code by Guido Scognamiglio
// Last update: May 2019
//
// Based on Arduino MKR 1010 WiFi and Zihatec Arduitouch MKR -> https://www.hwhardsoft.de/english/projects/arduitouch-mkr/
// Uses libraries:
// - Adafruit GFX library for ILI9341 touch screen display
// - WiFiNina for MKR1010 WiFi
// - FlashStorage for storing data into the flash memory (ATSAM doesn't have its own EEPROM)
// - ArduinoJson for parsing data downloaded from OpenWeatherMap
// - SimpleDHT for reading the DHT11 Temperature & Humidity sensor
// - Extra Fonts from Online font converter: http://oleddisplay.squix.ch/#/home
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
#include <Fonts/DSEG14_Classic_Bold_Italic_64.h>
#include <Fonts/DSEG14_Classic_Italic_36.h>
#include <Fonts/Meteocons_Regular_48.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <SimpleDHT.h>
#include <ArduinoJson.h>
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

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configuration
char ssid[] = "{type your SSID here}";
char pass[] = "{type your password}";

// Specify the IP of your Home Automation Server
IPAddress HomeAutomationServer(24, 0, 0, 127);

// Define a different NTP server if you're not in Italy
#define NTP_SERVER "3.it.pool.ntp.org"

// Seconds of inactivity before returning to the home page
#define IDLE_TIME 6

// Beep duration in Milliseconds
#define BEEP_DURATION 50

// Thermostat Hysteresis in Seconds
#define THERMOSTAT_HYSTERESIS 20

// Considering the DHT sensor is located inside the enclosure, it will also capture the circuit's heat, while the actual envinronmental temperature might be lower.
// Therefore a compensation is needed in order to have a reading closer to what's perceived 2 meters away from the circuit.
// This value is a percentage of the actual reading from the sensor, to be added or subtracted, according to the sign.
// In order to be set with accuracy, some comparison is needed possibly with an infrared thermometer, varying this value until the reading becomes stable and accurate.
#define THERMOSTAT_OFFSET -14.f

// These are days and monts in local language (Italian, in my case)
const char *days[] = { "Domenica", "Lunedi", "Martedi", "Mercoledi", "Giovedi", "Venerdi", "Sabato" };
const char *months[] = { "Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno", "Luglio", "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre" };

// OpenWeatherMap account ID and City ID
String OWM_APIKEY = "{register with OpenWeatherMap to get your Key}";
String OWM_CITYID = "{see OWM API documentation to find your City ID}";

// Weather forecast update in Seconds
#define OWM_UDPATE_FREQ 3600 // 3600 seconds = 1 hour
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Display and touch screen
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
TouchEvent tevent(touch);

// Wi-Fi
WiFiClient client;
int status = WL_IDLE_STATUS;
String PIN = "", pinEntry;
String CRLF = "\015\012"; // "\x0D\x0A" // "\r\n" // "\13\10" // This is CARRIAGE RETURN + LINE FEED

// NTP Client for internet time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER);

// DHT11 Sensor
SimpleDHT11 dht11(PIN_DHT11);
float Temperature, prev_Temperature;

// Weather forecast service
//char WeatherServer[] = "api.openweathermap.org";
IPAddress WeatherServer(37,139,20,5);
// Define a different Country Code if you want weather information in a different language
String WeatherURL = "/data/2.5/forecast?id=" + OWM_CITYID + "&units=metric&cnt=1&lang=it&APPID=" + OWM_APIKEY;
struct WeatherData
{
  //char location[32];
  //char weather[32];
  char description[32];
  char iconID[4];
  float temp;
  float humi;
  float temp_min;
  float temp_max;
  float wind_speed;
  //float wind_deg;
  float clouds;
  unsigned long LastTimeUpdated;
} WeatherForecast;
FlashStorage(WeatherStore, WeatherData);


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
  kPage_Switches,
  kPage_Thermostat,
  kPage_Auth,
};

// Declare some prototypes with default values
void UpdateKeyPad(int single = -1); // Pass a number 0~7 to redraw only a single button
void UpdateThermostat(int redraw = 3); // 1 = redraw only status, 2 = redraw only temp; 3 = redraw all
String getFromHTTP(String URL, IPAddress server = HomeAutomationServer);

// Code from: http://forum.arduino.cc/index.php?topic=329079.0
// Zone is CET (GMT+1), DST starts last sunday of march at 2:00 and ends last sunday of october at 2:00 (3:00)
// You should implement this function in a different way if you're in a different time zone
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

// Compute an average float value
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

// Used to catch the touches on buttons (TouchEvent library also has a similar function)
bool isIntersect( int Ax, int Ay, int Aw, int Ah,  int Bx, int By, int Bw, int Bh )
{
  return
    Bx + Bw > Ax &&
    By + Bh > Ay &&
    Ax + Aw > Bx &&
    Ay + Ah > By;
}

// A DHT22 would be more precise, allows decimal readings and can be updated twice in a second...
void ReadDHT11()
{
  byte temp = 0;
  byte humi = 0;
  int err = SimpleDHTErrSuccess;
  if ((err = dht11.read(&temp, &humi, NULL)) != SimpleDHTErrSuccess)
    return;

  // Apply offset
  float t = (float)temp / 100.f * (100.f + THERMOSTAT_OFFSET);

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
    while (client.available() == 0) {
        if (millis() - timeout > 2000) {
            client.stop();
            return "TIMEOUT";
        }
    }

    // Get response
    String Response = "";
    while (client.available())
    	Response += (char)client.read();

    // Get just the HTML part, discard the HTTP header
    String HTML = Response.substring(Response.indexOf(CRLF + CRLF) + 4, Response.length());

    return HTML;
  }

  return "OFFLINE";
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Function that defines the HTTP call to check the authorization
void CheckLogin()
{
  String res; // Stores the response from the Server to check if the PIN is valid

  // TO DO...
  // Here you should define the call to your Server to check for the login...

  if (res == "1")
  {
    PIN = pinEntry;
    draw_screen(kPage_Switches);
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
   // TO DO...
   // Here you should write your own function that sends the controls to your Home Automation Server

}

// Called to get the configuration of all relay switches from the server
void GetSwitchConfig()
{
	// TO DO...
	// Here you should write your own function to gather the switch informations from your Home Automation Server
	// and assign values to these variables:
    // Switch[r].Name
    // Switch[r].Hidden
    // Switch[r].AskConfirm
    // Switch[r].Type
    // Switch[r].Status

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

  switch(Page)
  {
    case kPage_Home:
      // Touch the temperature area, go to thermostat page
      if (isIntersect(p.x, p.y, 1, 1, 200, 170, 120, 70))
        draw_screen(kPage_Thermostat);

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

    // The configuration page has not been implemented at all...
    case kPage_Config:
      break;
  }
}

// Not used...
void onDblClick(TS_Point p)
{
}

void onLongClick(TS_Point p)
{
  // Reset after a long click in the home screen
  if (Page == kPage_Home) NVIC_SystemReset();
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
      tft.setCursor(10,140);
      tft.print("Ricezione della configurazione..."); // "Downloading configuration"
      GetSwitchConfig();

      tft.fillScreen(0);
      UpdateStatusBar();
      UpdateKeyPad();
      break;

    case kPage_Thermostat:
      tft.fillScreen(0);
      UpdateStatusBar();
      UpdateThermostat();
      break;

    // To do...
    case kPage_Config:

      break;
  }
}

// Draws the 8 switch buttons
void UpdateKeyPad(int single) // default = -1
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

  // Draw icon... See: https://openweathermap.org/weather-conditions
  // Idea: since this is a font, each icon could have its own color... i.e.: the sun could be yellow, the moon could be blue...
  tft.fillRect(240, 20, 80, 80, 0);
  tft.setFont(&Meteocons_Regular_48);
  tft.setCursor(260, 90);

  // clear sky
  if (String(WeatherForecast.iconID) == "01d") tft.print("1");
  if (String(WeatherForecast.iconID) == "01n") tft.print("2");

  // few clouds
  if (String(WeatherForecast.iconID) == "02d") tft.print("3");
  if (String(WeatherForecast.iconID) == "02n") tft.print("4");

  // scattered clouds
  if (String(WeatherForecast.iconID).toInt() == 3) tft.print("5");

  // broken clouds
  if (String(WeatherForecast.iconID).toInt() == 4) tft.print("Y");

  // shower rain
  if (String(WeatherForecast.iconID).toInt() == 9) tft.print("R");

  // rain
  if (String(WeatherForecast.iconID).toInt() == 10) tft.print("8");

  // thunderstorm
  if (String(WeatherForecast.iconID).toInt() == 11) tft.print("6");

  // snow
  if (String(WeatherForecast.iconID).toInt() == 13) tft.print("W");

  // mist
  if (String(WeatherForecast.iconID).toInt() == 50) tft.print("M");
}

void getOpenWeatherMap()
{
  String result = getFromHTTP(WeatherURL, WeatherServer);

  result.replace('[', ' ');
  result.replace(']', ' ');

  char jsonArray[result.length() + 1];
  result.toCharArray(jsonArray, sizeof(jsonArray));
  jsonArray[result.length() + 1] = '\0';

  StaticJsonBuffer<1024> json_buf;
  JsonObject &root = json_buf.parseObject(jsonArray);
  if (!root.success())
    return;

  //sprintf(WeatherForecast.location, "%s", (const char*)root["city"]["name"]);
  //sprintf(WeatherForecast.weather, "%s", (const char*)root["list"]["weather"]["main"]);
  sprintf(WeatherForecast.description, "%s", (const char*)root["list"]["weather"]["description"]);
  sprintf(WeatherForecast.iconID, "%s", (const char*)root["list"]["weather"]["icon"]);
  WeatherForecast.temp = (float)root["list"]["main"]["temp"];
  WeatherForecast.humi = (float)root["list"]["main"]["humidity"];
  WeatherForecast.temp_min = (float)root["list"]["main"]["temp_min"];
  WeatherForecast.temp_max = (float)root["list"]["main"]["temp_max"];
  WeatherForecast.wind_speed = (float)root["list"]["wind"]["speed"];
  //WeatherForecast.wind_deg = (float)root["list"]["wind"]["deg"];
  WeatherForecast.clouds = (float)root["list"]["clouds"]["all"];
  WeatherForecast.LastTimeUpdated = timeClient.getEpochTime();

  WeatherStore.write(WeatherForecast);
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

  // Get the current temperature
  Temperature = TempAverage.GetAverage();

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

void UpdateThermostat(int redraw) // default = 3
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

// Code From: https://stackoverflow.com/questions/9072320/split-string-into-string-array
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}
/*
void TftLog(String txt)
{
  tft.setFont();
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(2, 230);
  tft.print(txt);
}
*/

void WiFiConnect()
{
  tft.fillRect(0, 100, 320, 40, 0);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 120);
  tft.print("Connessione in corso..."); // "Connecting to server..."

  // Attempt to connect to Wifi network:
  while (status != WL_CONNECTED)
  {
    status = WiFi.begin(ssid, pass);
    delay(1000);
  }
}

void setup()
{
  // Beeper output pin
  pinMode(PIN_BEEPER, OUTPUT);

  // Init display
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(0);

  // Turn backlight on
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, LOW);

  // Start WiFi connection
  WiFiConnect();

  // Start NTP Client
  timeClient.begin();
  timeClient.update();
  timeClient.setTimeOffset(CheckDST() ? 3600*2 : 3600); // 3600 is Timezone CET (GMT+1)

  // Setup Weather forecast service
  WeatherForecast = WeatherStore.read();
  Serial1.println(WeatherForecast.LastTimeUpdated);
  if (timeClient.getEpochTime() - WeatherForecast.LastTimeUpdated >= OWM_UDPATE_FREQ)
    if (WeatherForecast.LastTimeUpdated > 0)
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

  // Init TouchEvent instance
  touch.begin();
  tevent.setResolution(tft.width(), tft.height());
  tevent.setDblClick(300);
  tevent.registerOnTouchClick(onClick);
  tevent.registerOnTouchDblClick(onDblClick);
  tevent.registerOnTouchLong(onLongClick);
  //tevent.registerOnTouchSwipe(onSwipe);

  SecondTimer = 0;
  IdleTimer = 0;

  TempAverage.Init();
  HumiAverage.Init();

  // Start with home page
  draw_screen(kPage_Home);
  tone(PIN_BEEPER, 1760, BEEP_DURATION);
}

void loop()
{
  tevent.pollTouchScreen();

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
