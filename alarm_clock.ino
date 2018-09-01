#include <NTPClient.h>
#include "ESP8266WiFi.h"
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiUdp.h>
#include <StringSplitter.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <MQTTClient.h>
// Screen's libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

#include <ESP8266HTTPClient.h>
#if defined(ESP8266)
#include <Ticker.h>
#endif
#include "WundergroundClient.h"
#include "alarm_clock_fonts.h"
#include "alarm_clock_images.h"
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"

// WIFI SETUP
const char *ssid     = "";
const char *password = "";

// Interval betwen data update
const int UPDATE_INTERVAL_SECS = 10 * 60; // Update every 10 minutes

// PINS DEFINITION //
int buzzerPin = 5;  // OUTPUT pin  D1
int sclPin = 0;     // SCL for I2C D3
int sdaPin = 2;     // SDA for I2C D4
int redPin = 15;
int whitePin = 14;
int bluePin = 12;
int greenPin = 13;
int lightPin = 4;    // INPUT pin   D2

// ALARM VARS
struct alarmSettings {
  String active;  // is he alarm active?
  String time;    // When should it ring?
  String days;    // On which days?
  String rescheduled;  //When is the second alarm (if first attempt fails
  String sunrise_leds;   // Should it light the LEDs?
};

alarmSettings ALARM = {"", "", "", "", "true"};


String week_days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}; //Week days
String week_day = ""; // Current week day
String months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
int alarmRing = 0;    // Boolean 0 alarm not ringing, 1 ringing
int alarmTimer = 0;   // Number of seconds that the alarm has been ringing
int isReminder = 0;   // Boolean 0 is the main alarm, 1 is reminder
int maxAlarmTimer = 30;  // Max. number of seconds the alarm should ring
int sunriseTimeTrigger = 0; // If set to 1 the sunrise light starts
String sunriseTime = "";

struct rgbLEDs {
  int red;
  int green;
  int blue;
  int white;
};

rgbLEDs myLeds = {60, 0, 0, 0};


// Timezone vars
struct timezoneStruct {
  String id;
  String offset;
  String daylight;
};

timezoneStruct tz = {"", "0", ""};


// MQTT vars
struct mqtt_config {
  String server;  // IP address of the broker
  String topic;   // Topic to publish to
  String id;      // Our unique mqtt device ID
  String status;  // Current status
};

mqtt_config mqtt = {"", "", "", "OFF"};


// Weather underground vars
struct weather_config {
  String apikey;  // Weather underground API key.
  String country; // Two character ISO country code
  String city;    // City name
  String lang;    // Language ISO code (EN,ES,...)
  boolean is_metric; // Should I present the data in metric units?
  String last_update; // When did the last update take place?
};

weather_config weather = {"", "", "", "EN", true, "--"};


// flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;
bool colon = true;
int brightness = 5;

const int screen_slide_time = 5; //time in seconds each OLED screen should be presented

WiFiClient net;
MQTTClient client;

// Timing and sync VARS
unsigned long startMillis;  //some global variables available anywhere in the program
unsigned long currentMillis;
const unsigned long period = 1000;
int seconds = 0;
String currentMinute = "";
int ledStep = 500; // Value to increaso or decrease the sunrise velocity
unsigned long previousStep = 0; // for syncing leds

#if defined(ESP8266)
Ticker ticker;
Ticker mainTicker;
#else
long timeSinceLastWUpdate = 0;
#endif

int vButtonValue;
String label = "";
bool setMode = false;
int hours;
int minutes;
String btn = "";
int phase = 0;
int bouncetime = 200;
long moment;

// Firmware update setup
// Path to access firmware update page (Not Neccessary to change)
const char* update_path = "/firmware";
// Username to access the web update page
const char* update_username = "admin";
// Password to access the web update page
const char* update_password = "admin";

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiUDP ntpUDP;

// Screens Init
Adafruit_7segment matrix = Adafruit_7segment();
const int I2C_DISPLAY_ADDRESS = 0x3c;
SSD1306Wire     display(I2C_DISPLAY_ADDRESS, sdaPin, sclPin);
OLEDDisplayUi   ui( &display );

// NTP Client init and set to 60 seconds update interval and
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 0, 60000);
// Weatherunderground init
WundergroundClient wunderground(weather.is_metric);


// OLED DISPLAY AND FRAMES SETUP
//declaring prototypes
void drawProgress(OLEDDisplay *display, int percentage, String label);
void updateData(OLEDDisplay *display);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void setReadyForWeatherUpdate();


// Add frames
// this array keeps function pointers to all frames
// frames are the single views that slide from right to left
FrameCallback frames[] = { drawDateTime, drawCurrentWeather, drawForecast };
int numberOfFrames = 3;

OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;


void setup() {
  Serial.begin(115200);
  startMillis = millis();

  // Initialize buzzer pin
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // Initialize RGB Led strip pins
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(whitePin, OUTPUT);

  // Initialize light pin button
  pinMode(lightPin, INPUT);
  digitalWrite(lightPin, LOW);

  // 7 segment screen setup
  Wire.begin(sdaPin, sclPin);
  matrix.begin(0x70);
  matrix.setBrightness(brightness);

  // initialize dispaly
  display.init();
  display.clear();
  display.display();

  //display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);

  WiFi.begin(ssid, password);

  int counter = 0;
  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
    display.clear();
    display.drawString(64, 10, "Connecting to WiFi");
    display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
    display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
    display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
    display.display();
    counter++;
  }


  ui.setTargetFPS(30);

  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);

  // You can change this to
  // TOP, LEFT, BOTTOM, RIGHT
  ui.setIndicatorPosition(BOTTOM);

  // Defines where the first frame is located in the bar.
  ui.setIndicatorDirection(LEFT_RIGHT);

  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_LEFT);

  ui.setFrames(frames, numberOfFrames);

  ui.setOverlays(overlays, numberOfOverlays);

  // Inital UI takes care of initalising the display too.
  ui.init();

  Serial.println("");

#if defined(ESP8266)
  //ticker.attach(screen_slide_time, setReadyForWeatherUpdate);
  mainTicker.attach(UPDATE_INTERVAL_SECS, setReadyForWeatherUpdate);
#endif

  Serial.println("");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (!SPIFFS.begin())
  {
    // Serious problem
    Serial.println("SPIFFS Mount failed");
  } else {
    Serial.println("SPIFFS Mount succesfull");
  }

  timeClient.begin();
  timeClient.update();

  Serial.println("Cheking if it is summer: " + String(isSummertime()));

  loadConfig();
  Serial.println("Weather config: " + weather.apikey + " - " + weather.lang + " - " + weather.country + " - " + weather.city);
  week_day = week_days[timeClient.getDay()];
  Serial.println("Today is: " + week_day);
  setSeconds();
  updateData(&display);
  printTime();

  /* ##########################           WebServer setup          #############################              */
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/config", SPIFFS, "/config.json");

  server.on("/stopAlarm", []() { // if you add this subdirectory to your webserver call, you get text below :)
    alarmRing = 0;  // Stop the ring bell
    alarmTimer = 0; // Reset the timer
    noTone(buzzerPin);
    server.send(200, "text/plain", "Alarm stopped");  // send to someones browser when asked
  });
  server.on("/saveSettings", []() {
    ALARM.active = server.arg("active");
    ALARM.time = server.arg("time") + ":00";
    ALARM.days = server.arg("days");
    ALARM.sunrise_leds = server.arg("sunrise");
    tz.id      = server.arg("tzid");
    tz.offset  = server.arg("offset");
    tz.daylight = server.arg("daylight");
    mqtt.server = server.arg("mqtt_broker");
    mqtt.topic  = server.arg("mqtt_topic");
    mqtt.id     = server.arg("mqtt_id");
    weather.apikey = server.arg("wapikey");
    weather.country = server.arg("wcountry");
    weather.city = server.arg("wcity");

    sunriseTime = addTime(ALARM.time, -5);

    saveConfig();

    if (isSummertime()) {
      timeClient.setTimeOffset((tz.offset.toInt() + tz.daylight.toInt()) * 3600);
    } else {
      timeClient.setTimeOffset(tz.offset.toInt() * 3600);
    }

    printTime();

    server.send(200, "text/plain", "Done!");
  });
  server.on("/setLEDS", []() {
    analogWrite(whitePin, server.arg("w").toInt());
    analogWrite(redPin, server.arg("r").toInt());
    analogWrite(greenPin, server.arg("g").toInt());
    analogWrite(bluePin, server.arg("b").toInt());
    server.send(200, "text/plain", "Done!");
  });
  server.on("/setBrightness", []() {
    matrix.setBrightness(server.arg("b").toInt());
    server.send(200, "text/plain", "Done!");
  });
  server.on("/getTime", []() {
    server.send(200, "text/plain", timeClient.getFormattedTime());  // send to someones browser when asked
  });
  server.on("/toggleLight", []() {
    client.connect(mqtt.id.c_str());
    /*
      client.subscribe(mqtt.topic);
      delay(100);
      client.unsubscribe(mqtt.topic);
    */
    if (mqtt.status == "ON") {
      mqtt.status = "OFF";
    } else {
      mqtt.status = "ON";
    }

    client.publish(mqtt.topic, mqtt.status);
    server.send(200, "text/plain", "Done! -->" + mqtt.status);

  });
  httpUpdater.setup(&server, update_path, update_username, update_password);
  server.begin();
  Serial.println("HTTP server started");

  // Let's connect to MQTT
  client.setOptions(60, true, 1000);
  client.begin(mqtt.server.c_str(), net);
  while (!client.connect(mqtt.id.c_str()))
  {
    Serial.print(".");
    delay(1000);
  }
  client.onMessage(messageReceived);

  Serial.println("MQTT server connected.");

}

void loop() {
  currentMillis = millis();
  server.handleClient();
  
  if (digitalRead(lightPin) == HIGH) {
    process_ledButton();
  }

  // CHECK A0 (Virtual Input Buttons). The value goes from 0 - 1024. 100 is a safe choice.
  vButtonValue = analogRead(A0);
  if (vButtonValue > 100) {
    Serial.println(vButtonValue);
    if (vButtonValue >= 900 && vButtonValue <= 950 && phase == 0 && btn == "") {
      btn = "SET";
      Serial.println("Entering SET MODE");
      printTime(ALARM.time);

      StringSplitter *splitter = new StringSplitter(ALARM.time, ':', 3);
      minutes = splitter->getItemAtIndex(1).toInt();
      hours = splitter->getItemAtIndex(0).toInt();
      Serial.println("Current alarm time is:" + String(hours) + ":" + String(minutes));
      setMode = true;
      moment = millis();
      Serial.println("Entering phase 1");
      phase = 1;
    } else if (phase == 1  && (millis() - moment) >= bouncetime) {
      if (vButtonValue >= 700 && vButtonValue <= 750) {
        btn = "UP";
        hours = hours + 1;
        if (hours > 23) {
          hours = 0;
        }
        moment = millis();
        printTime(String(hours) + ":" + String(minutes) + ":00");
        Serial.println("Current alarm time is:" + String(hours) + ":" + String(minutes));
      } else if (vButtonValue >= 980 && vButtonValue <= 1024) {
        btn = "DOWN";
        hours = hours - 1;
        if (hours < 0) {
          hours = 23;
        }
        moment = millis();
        printTime(String(hours) + ":" + String(minutes) + ":00");
        Serial.println("Current time is:" + String(hours) + ":" + String(minutes));
      } else if (vButtonValue >= 900 && vButtonValue <= 950) {
        phase = 2;
        moment = millis();
        Serial.println("Entering phase 2");
      } else {
        btn = "";
      }
    } else if (phase == 2  && (millis() - moment) >= bouncetime) {
      if (vButtonValue >= 700 && vButtonValue <= 750) {
        btn = "UP";
        minutes = minutes + 1;
        if (minutes > 59) {
          minutes = 0;
        }
        moment = millis();
        printTime(String(hours) + ":" + String(minutes) + ":00");
        Serial.println("Current time is:" + String(hours) + ":" + String(minutes));
      } else if (vButtonValue >= 980 && vButtonValue <= 1024) {
        btn = "DOWN";
        minutes = minutes - 1;
        if (minutes < 0) {
          minutes = 59;
        }
        moment = millis();
        printTime(String(hours) + ":" + String(minutes) + ":00");
        Serial.println("Current time is:" + String(hours) + ":" + String(minutes));
      } else if (vButtonValue >= 900 && vButtonValue <= 950 ) {
        setMode = false;
        phase = 0;
        Serial.println("Leaving setMode");
        printTime();
        String m = minutes < 10 ? "0" + String(minutes) : String(minutes);
        String h = hours < 10 ? "0" + String(hours) : String(hours);
        ALARM.time = h + ":" + m + ":00";
        saveConfig();
        sunriseTime = addTime(ALARM.time, -5);
        btn = "SET";
        moment = millis();
      } else {
        btn = "";
      }
    } else if (btn == "SET" && (millis() - moment) >= bouncetime) {
      btn = "";
    }
  }

  if ((timeClient.getFormattedTime() == sunriseTime) && (isAlarmDay(week_day)) && (ALARM.active == "true") && (ALARM.sunrise_leds == "true")) {
    sunriseTimeTrigger = 1;
    Serial.println("Sunrise Time!!! ***");
  }

  if (sunriseTimeTrigger == 1) {
    if (millis() - previousStep >= ledStep) {
      if (myLeds.red <= 1023 ) {
        analogWrite(redPin, myLeds.red);
        analogWrite(greenPin, myLeds.green);
        myLeds.red = myLeds.red + 2;
        if (myLeds.red >= 100) {
          myLeds.green = (myLeds.red / 2) - 50;
        }
      } else if (myLeds.white <= 1023) {
        analogWrite(whitePin, myLeds.white);
        myLeds.white = myLeds.white + 2;
      }
      previousStep = millis();
    }
  }

  if (((timeClient.getFormattedTime() == ALARM.time) || (timeClient.getFormattedTime() == ALARM.rescheduled)) && (isAlarmDay(week_day)) && (ALARM.active == "true")) {
    alarmRing = 1;
    if (timeClient.getFormattedTime() == ALARM.rescheduled) {
      isReminder = 1;
    }
  }

  if (currentMillis - startMillis >= period) {
    seconds = seconds + 1;
    if ((seconds % 2) == 0 || setMode == true) {
      matrix.drawColon(true);
      if ( WiFi.status() != WL_CONNECTED ) {
        WIFI_Connect();
      }
    } else {
      matrix.drawColon(false);
      if (alarmRing == 1) {
        noTone(buzzerPin); // Just to make sure there's no strange noises...
        digitalWrite(buzzerPin, LOW);
        Serial.println("Dong!");
      }
    }
    matrix.writeDisplay();

    if ((alarmRing == 1) && (alarmTimer <= 30) && (WiFi.status() == WL_CONNECTED)) { // Only ring if there's wifi. without it it might right at any time...
      Serial.println("Ding!");
      alarmTimer++;
      if ((timeClient.getFormattedTime().substring(0, 5) == ALARM.time.substring(0, 5)) || (timeClient.getFormattedTime().substring(0, 5) == ALARM.rescheduled.substring(0, 5))) {
        // Now we're sure it only sounds then it really should
        tone(buzzerPin, 880, period / 2);
      }else{
        alarmRing == 0;
        alarmTimer == 0;
        noTone(buzzerPin);
      }
    }
    startMillis = currentMillis;
  }

  int remainingTimeBudget = ui.update();
  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
    updateData(&display);
  }

  if ((alarmRing == 1) && (alarmTimer > maxAlarmTimer)) {
    alarmTimer = 0;
    alarmRing  = 0;
    noTone(buzzerPin);
    if (isReminder == 0) {
      ALARM.rescheduled = addTime(ALARM.time, 5);
    } else {
      ALARM.rescheduled = "";
      isReminder = 0;
      Serial.println("Disabling reminder... are you even there?");
    }
  }

  if (timeClient.getFormattedTime().substring(0, 5) != currentMinute && setMode == false) {
    printTime();
    currentMinute = timeClient.getFormattedTime().substring(0, 5);
    seconds = 0;
  }

  if (timeClient.getFormattedTime() == "00:00:00") {
    week_day = week_days[timeClient.getDay()];
  }

  //is it time to update? if so, let's go!
  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
    updateData(&display);
  }

  //is it time to update? if so, let's go! 
  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
    updateData(&display);
  }
}

bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");

  if (!configFile) {
    Serial.println("Error when opening the config file!");
    return false;
  }

  size_t size = configFile.size();
  Serial.println("File size: " + size);
  if (size > 1024) {
    Serial.println("The file is too large!");
    return false;
  }

  const size_t bufferSize = 2 * JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + 100;
  DynamicJsonBuffer jsonBuffer(bufferSize);

  JsonObject& root = jsonBuffer.parseObject(configFile);

  JsonObject& timezoneobj = root["timezone"];
  tz.id = timezoneobj["id"].asString();
  tz.offset = timezoneobj["offset"].asString();
  tz.daylight = timezoneobj["useDaylightTime"].asString();

  if (isSummertime()) {
    timeClient.setTimeOffset((tz.offset.toInt() + tz.daylight.toInt()) * 3600);
  } else {
    timeClient.setTimeOffset(tz.offset.toInt() * 3600);
  }


  JsonObject& alarm = root["alarm"];
  ALARM.active = alarm["active"].asString();
  ALARM.time = alarm["time"].asString();
  ALARM.days = alarm["days"].asString();
  ALARM.sunrise_leds = alarm["sunrise_leds"].asString();
  sunriseTime = addTime(ALARM.time, -5);

  JsonObject& mqttobj = root["mqtt"];
  mqtt.server = mqttobj["server"].asString();
  mqtt.topic  = mqttobj["topic"].asString();
  mqtt.id     = mqttobj["id"].asString();

  JsonObject& weatherobj = root["weather"];
  weather.apikey = weatherobj["api_key"].asString();
  weather.country  = weatherobj["country"].asString();
  weather.city    = weatherobj["city"].asString();
  Serial.println("Configuration loaded!");
  Serial.println(weather.apikey);
  Serial.println(weather.country);
  Serial.println(weather.city);
  configFile.close();
}

void saveConfig() {
  // Delete old version
  SPIFFS.remove("/config.json");
  // Create the new one
  File f = SPIFFS.open("/config.json", "w");
  String json_txt  = "{\n\"timezone\": {\n\"id\": \"" + tz.id + "\",\n\"offset\": \"" + tz.offset + "\",\n\"useDaylightTime\": \"" + tz.daylight + "\"\n},";
  json_txt += "\n\"alarm\": {\n\"active\": \"" + ALARM.active + "\",\n\"time\": \"" + ALARM.time + "\",\n\"days\": \"" + ALARM.days + "\",\n\"sunrise_leds\": \"" + ALARM.sunrise_leds + "\"\n},";
  json_txt += "\n\"mqtt\": \n{\n\"server\":\"" + mqtt.server + "\",\n\"topic\":\"" + mqtt.topic + "\",\n\"id\":\"" + mqtt.id + "\"\n},";
  json_txt += "\n\"weather\": \n{\n\"api_key\":\"" + weather.apikey + "\",\n\"country\": \"" + weather.country + "\",\n\"city\": \"" + weather.city + "\"\n}\n}\n";

  Serial.println(json_txt);
  f.print(json_txt);
  f.close();
}


bool isAlarmDay(String wday) {
  boolean isPresent = false;
  if (ALARM.days.indexOf(wday) != -1) {
    isPresent = true;
  }
  return isPresent;
}

String addTime(String current_time, int minutes) {
  StringSplitter *splitter = new StringSplitter(current_time, ':', 3);
  int itemCount = splitter->getItemCount();
  int newMinutes = splitter->getItemAtIndex(1).toInt() + minutes;
  int newHours = splitter->getItemAtIndex(0).toInt();

  if (newMinutes >= 60) {
    newMinutes = newMinutes - 60;
    newHours = newHours + 1;
  }

  if (newMinutes < 0) {
    newMinutes = newMinutes + 60;
    newHours = newHours - 1;
  }

  String m = newMinutes < 10 ? "0" + String(newMinutes) : String(newMinutes);
  String h = newHours < 10 ? "0" + String(newHours) : String(newHours);
  Serial.println("Reminder alarm set to: " + h + ":" + m + ":00");
  return h + ":" + m + ":00";
}



bool isSummertime ()
{
  //localtime(timeClient.getEpochTime());
  setSyncProvider(myDateTime);
  //Serial.println(timeStatus());
  time_t t = now();
  int cyear = year(t);
  int cmonth = month(t);
  int chour  = hour(t);
  int cday   = day(t);
  int tzHours = 0;

  if ((cmonth < 3) || (cmonth > 10)) return false; // keine Sommerzeit in Jan, Feb, Nov, Dez
  if ((cmonth > 3) && (cmonth < 10)) return true; // Sommerzeit in Apr, Mai, Jun, Jul, Aug, Sep
  if (cmonth == 3 && (chour + 24 * cday) >= (1 + tzHours + 24 * (31 - (5 * cyear / 4 + 4) % 7)) || cmonth == 10 && (chour + 24 * cday) < (1 + tzHours + 24 * (31 - (5 * cyear / 4 + 1) % 7)))
    return true;
  else
    return false;

}

String todaysDate() {
  setSyncProvider(myDateTime);
  time_t t = now();

  if (isSummertime()) {
    t = t + ((tz.offset.toInt() + tz.daylight.toInt()) * 3600);
  } else {
    t = t + (tz.offset.toInt() * 3600);
  }

  int cyear = year(t);
  int cmonth = month(t);
  int chour  = hour(t);
  int cday   = day(t);
  int tzHours = 0;

  //String nice_month = cmonth < 10 ? "0" + String(cmonth) : String(cmonth);
  String nice_days = cday < 10 ? "0" + String(cday) : String(cday);

  return week_day + " " + nice_days + " " + months[(cmonth - 1)] + " " + String(cyear); // String(cyear) +"/"+ nice_month +"/"+ nice_days;
}

void messageReceived(String &topic, String &payload) {
  mqtt.status = payload;
}

time_t myDateTime() {
  return time_t(timeClient.getEpochTime());
}


void printTime() {
  StringSplitter *splitter = new StringSplitter(timeClient.getFormattedTime(), ':', 3);
  int itemCount = splitter->getItemCount();
  int newMinutes = splitter->getItemAtIndex(1).toInt();
  int newHours = splitter->getItemAtIndex(0).toInt();

  int displayValue = newHours * 100 + newMinutes;

  // Now print the time value to the display.
  matrix.print(displayValue, DEC);
  matrix.drawColon(true);
  // In case it midnight we have to pad the values...
  if (newHours == 0) {
    // Pad hour 0.
    matrix.writeDigitNum(1, 0);
    // Also pad when the 10's minute is 0 and should be padded.
    if (newMinutes < 10) {
      // Position was 2, but with the clock colon the correct position for minutes padding is 3
      matrix.writeDigitNum(3, 0);
    }
  }
  matrix.writeDisplay();
}

void printTime(String myTime) {
  StringSplitter *splitter = new StringSplitter(myTime, ':', 3);
  int itemCount = splitter->getItemCount();
  int newMinutes = splitter->getItemAtIndex(1).toInt();
  int newHours = splitter->getItemAtIndex(0).toInt();

  int displayValue = newHours * 100 + newMinutes;

  // Now print the time value to the display.
  matrix.print(displayValue, DEC);
  matrix.drawColon(true);
  // In case it midnight we have to pad the values...
  if (newHours == 0) {
    // Pad hour 0.
    matrix.writeDigitNum(1, 0);
    // Also pad when the 10's minute is 0 and should be padded.
    if (newMinutes < 10) {
      matrix.writeDigitNum(2, 0);
    }
  }
  matrix.writeDisplay();
}

String addMinutes(String tmp_time, int minutes) {
  StringSplitter *splitter = new StringSplitter(tmp_time, ':', 3);
  int itemCount = splitter->getItemCount();
  int newMinutes = splitter->getItemAtIndex(1).toInt() + minutes;
  int newHours = splitter->getItemAtIndex(0).toInt();

  if (newMinutes >= 60) {
    newMinutes = newMinutes - 60;
  }

  if (newMinutes < 0) {
    newMinutes = 60 - newMinutes;
  }

  String m = newMinutes < 10 ? "0" + String(newMinutes) : String(newMinutes);
  String h = newHours < 10 ? "0" + String(newHours) : String(newHours);
  Serial.println("Reminder alarm set to: " + h + ":" + m + ":00");
  return h + ":" + m + ":00";
}

String addHours(String tmp_time, int hours) {
  StringSplitter *splitter = new StringSplitter(tmp_time, ':', 3);
  int itemCount = splitter->getItemCount();
  int newMinutes = splitter->getItemAtIndex(1).toInt();
  int newHours = splitter->getItemAtIndex(0).toInt() + hours;

  if (newHours > 23) {
    newHours = newHours - 24;
  }

  if (newHours < 0) {
    newHours = 24 - newHours;
  }

  String m = newMinutes < 10 ? "0" + String(newMinutes) : String(newMinutes);
  String h = newHours < 10 ? "0" + String(newHours) : String(newHours);
  Serial.println("Reminder alarm set to: " + h + ":" + m + ":00");
  return h + ":" + m + ":00";
}

void setSeconds() {
  StringSplitter *splitter = new StringSplitter(timeClient.getFormattedTime(), ':', 3);
  seconds = splitter->getItemAtIndex(2).toInt() + 2; // Substract 2 second delay. Don't know why that is needed YET
}

void drawProgress(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void updateData(OLEDDisplay *display) {
  if ( WiFi.status() == WL_CONNECTED ) {
    WIFI_Connect();
  }
  drawProgress(display, 30, "Updating time...");
  timeClient.update();
  drawProgress(display, 60, "Updating conditions...");
  wunderground.updateConditions(weather.apikey, weather.lang, weather.country, weather.city);
  drawProgress(display, 90, "Updating forecasts...");
  wunderground.updateForecast(weather.apikey, weather.lang, weather.country, weather.city);
  weather.last_update = timeClient.getFormattedTime();
  readyForWeatherUpdate = false;
  drawProgress(display, 100, "Done...");
  delay(500);
}


void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = todaysDate(); //wunderground.getDate();
  int textWidth = display->getStringWidth(date);
  display->drawString(64 + x, 5 + y, date);
  display->setFont(ArialMT_Plain_24);
  String time = timeClient.getFormattedTime();
  textWidth = display->getStringWidth(time);
  display->drawString(64 + x, 15 + y, time);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(60 + x, 5 + y, wunderground.getWeatherText());

  display->setFont(ArialMT_Plain_24);
  String temp = wunderground.getCurrentTemp() + "°C";
  display->drawString(60 + x, 15 + y, temp);
  int tempWidth = display->getStringWidth(temp);

  display->setFont(Meteocons_Plain_42);
  String weatherIcon = wunderground.getTodayIcon();
  int weatherIconWidth = display->getStringWidth(weatherIcon);
  display->drawString(32 + x - weatherIconWidth / 2, 05 + y, weatherIcon);
}


void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 2);
  drawForecastDetails(display, x + 88, y, 4);
}

void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String day = wunderground.getForecastTitle(dayIndex).substring(0, 3);
  day.toUpperCase();
  display->drawString(x + 20, y, day);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, wunderground.getForecastIcon(dayIndex));

  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, wunderground.getForecastLowTemp(dayIndex) + "|" + wunderground.getForecastHighTemp(dayIndex));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  String time = timeClient.getFormattedTime().substring(0, 5);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, time);
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  String temp = wunderground.getCurrentTemp() + "°C";
  display->drawString(128, 54, temp);
  display->drawHorizontalLine(0, 52, 128);
}

void setReadyForWeatherUpdate() {
  //Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}

void process_ledButton() {
  // Multi-purpose button while alarm is ringing it stops the alarm.
  // otherwise it toggles the light on the bedroom.
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();
  if (interrupt_time - last_interrupt_time > 200) {
    if (alarmRing == 1) {
      Serial.println("Stopping alarm...");
      // THe alarm is ringing --> STOP alarm
      alarmRing = 0;  // Stop the ring bell
      alarmTimer = 0; // Reset the timer
      noTone(buzzerPin); //Stop sound
      ALARM.rescheduled = "";
      isReminder = 0;
      // Reset sunrise values
      analogWrite(redPin, 0);
      analogWrite(whitePin, 0);
      analogWrite(greenPin, 0);
      sunriseTimeTrigger = 0;
      myLeds.red = 50;
      myLeds.green = 10;
      myLeds.white = 0;
    } else {
      Serial.print("Toggling the light...");
      // Let's toggle light
      client.connect(mqtt.id.c_str());
      mqtt.status = "ON";
      client.publish(mqtt.topic, mqtt.status);
    }
  }
  last_interrupt_time = interrupt_time;
}

void WIFI_Connect() {
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  // Wait for connection
  for (int i = 0; i < 25; i++)  {
    if ( WiFi.status() != WL_CONNECTED ) {
      delay ( 250 );
      Serial.print ( "." );
      delay ( 250 );
    } else {
      // We're connected, let's exit the loop
      i = 25;
    }
  }

  if ( WiFi.status() != WL_CONNECTED ) {
    // If we got here something wrong is going on
    // Let's reboot the device
    delay(1000);
    ESP.restart();
  }
}
