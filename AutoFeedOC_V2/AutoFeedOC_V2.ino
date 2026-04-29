#include "DHT.h"
#include "GyverOLED.h"
#include "WiFi.h"
#include "time.h"
#include "GyverStepper.h"
#include <Wire.h>
#include <WiFiClientSecure.h>
#include <AsyncTelegram2.h>

//всё что с тг связанно
#define BOT_TOKEN "" //ТОКЕН
#define CHAT_ID "" //ID
const char* ssid = ""; 
const char* password = "";  //Свои данные Wifi

WiFiClientSecure client;
AsyncTelegram2 bot(client);

GStepper<STEPPER4WIRE> stepper(2048, 0, 2, 1, 3);

// *** Переменные ***
int portionsServed = 0;
int lastDay = -1;
int foodPercent = 0;
int currentDistance = 0;
float temp = 0;
float hum = 0; 

const char* ntpServer = "pool.ntp.org"; // время берём от сервера, который находтся в GMC +0
const long gmtOffset_sec = 39600; // Это смещение времени в секундах для часового пояса (поставить своё значение)
const int daylightOffset_sec = 0;

bool fed_06 = false;
bool fed_15 = false;
bool tempWarned = false;
bool humWarned = false;
bool leftWarned = false;

GyverOLED<SSH1106_128x64, OLED_BUFFER> oled;

#define BUTTON_PIN 10
#define DHTPIN 6 
#define TRIG_PIN 5
#define ECHO_PIN 9 //****Необходимо поставить свои пины****
#define STEPS_FRW 20
#define STEPS_BKW 5
#define FEED_SPEED 8000

const byte drvPins[] = {0, 1, 2, 3}; // Фазы мотора (A1, A2, B1, B2)
const byte steps[] = {0b1010, 0b0110, 0b0101, 0b1001}; // Маска фаз
int feedAmount = 10;
DHT dht(6, DHT11);

unsigned long lastTime_5min = 0;
unsigned long lastTime_Oled = 0;
unsigned long startAttemptTime = 0;
const unsigned long interval_5min = 3000; //Как часто будет опрашиваться датчик DHT

void setup() {
  Serial.begin(115200);
  dht.begin();
  Wire.begin(7, 8);
  oled.init();
  oled.clear();
  oled.update();
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  for (byte i = 0; i < 4; i++) pinMode(drvPins[i], OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  disableMotor();
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() -startAttemptTime< 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi Connected");
    client.setInsecure(); 
    bot.setUpdateTime(1000); //Как часто будут проверятся сообщения в телеграмме
    bot.setTelegramToken(BOT_TOKEN);
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) {
      delay(1000);
      retry++;
    }
  }
  else {
    Serial.println("Offline Mode");
    delay(2000);
  }
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  hum = dht.readHumidity();
  temp = dht.readTemperature();
  leftFood();
  displayData();
}

void loop() {
TBMessage msg;
  if (bot.getNewMessage(msg)) {
    if (msg.text == "/feed") {
      catFeed();
    } 
    else if (msg.text == "/status") {
      String statusMsg = "📊 STATUS:\n";
      statusMsg += "🌡 Temp: " + String(temp, 1) + "°C\n";
      statusMsg += "💧 Hum: " + String(hum, 0) + "%\n";
      statusMsg += "🥣 Left: " + String(foodPercent) + "%\n";
      statusMsg += "✅ Served: " + String(portionsServed);
      bot.sendMessage(msg, statusMsg);
    }
    else if (msg.text == "/start") {
      bot.sendMessage(msg, "Bot is online!\nCommands:\n/feed - Start feeding process\n/status - info\n/set - change amount of feed");
    }
    else if (msg.text.startsWith("/set ")) {
      String val = msg.text.substring(5);
      int newValue = val.toInt();
      feedAmount = newValue;
      bot.sendMessage(msg, "✅ New portion: " + String(feedAmount));
  }
  }

//******Лошика времени********
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_mday != lastDay) {
    portionsServed = 0;
    lastDay = timeinfo.tm_mday;
  }
    if (timeinfo.tm_hour == 6 && timeinfo.tm_min == 40 && !fed_06) {
    catFeed();
    fed_06 = true;
  }
    if (timeinfo.tm_hour == 15 && timeinfo.tm_min == 0 && !fed_15) {
    catFeed();
    fed_15 = true;
  }
    if (timeinfo.tm_hour != 6 && timeinfo.tm_hour != 15) {
    fed_06 = false;
    fed_15 = false;
  }
}
unsigned long currentTime = millis();
// ************** Dht data checker ***********************8
  if (currentTime - lastTime_5min >= interval_5min) {
    lastTime_5min = currentTime;
    hum = dht.readHumidity();
    temp = dht.readTemperature();
    leftFood();
    if (hum >= 40 && !humWarned){
      bot.sendTo(atoll(CHAT_ID), "❗️High humidity. Current: " + String(hum,0) + "%");
      humWarned = true;
    }
    else {
      if (hum < 38) {
        humWarned = false;
      }
    }
    if (temp >= 33 && !tempWarned) {
      bot.sendTo(atoll(CHAT_ID), "❗️High temperature. Current: " + String(temp,0) + "°C");
      tempWarned = true;
    }
     else {
      if (temp < 30) {
        tempWarned = false;
      }
     }
    displayData();
  }
  if (currentTime - lastTime_Oled >= 30000) {
    lastTime_Oled = currentTime;
    displayData();
  }
// ****** Button logic *****************
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    Serial.println("Кнопка нажата");
    catFeed();
  }
}


//******** Function for feed ******************
void catFeed() {
  bot.sendTo(atoll(CHAT_ID), "Start Feeding...");
  for (int i = 0; i < feedAmount; i++) {
    oneRev();
  }
  disableMotor();
  portionsServed++;
  leftFood();
  if (foodPercent < 10 && !leftWarned) {
    bot.sendTo(atoll(CHAT_ID), "❗️Small amount of feed. Left: " + String(foodPercent, 0) + "%");
    leftWarned = true;
  }
  else {
    if (foodPercent > 20) {
      leftWarned = false;
    }
  }
  bot.sendTo(atoll(CHAT_ID), "Complete. Served today🥣: " + String(portionsServed));
  displayData();
}

//*********************** Motor move ********************************************
void oneRev() {
  for (int i = 0; i < STEPS_BKW; i++) runMotor(-1);
  for (int i = 0; i < STEPS_FRW; i++) runMotor(1);
}
void runMotor(int8_t dir) {
  static byte step = 0;
  step += dir;
  for (byte i = 0; i < 4; i++) {
    digitalWrite(drvPins[i], bitRead(steps[step & 0b11], i));
  }
  delayMicroseconds(FEED_SPEED);
}
void disableMotor() {
  for (byte i = 0; i < 4; i++) digitalWrite(drvPins[i], 0);
}

//******************* Oled data update *************************************

void displayData() {
  oled.clear();
  oled.setScale(1);
//******Строка 1************
  oled.setCursor(0, 0); 
  oled.print("HUM: ");
  if (isnan(hum)) {
    oled.print("NAN");
  } else {
    oled.print(hum, 0); oled.print("%");
  }
  oled.print(" TEMP: ");
  if (isnan(temp)) {
    oled.print("NAN");
  } else {
    oled.print(temp, 1); oled.print("C");
  }

//******Строка 2************
  oled.setCursor(0, 2); 
  oled.print("Served: "); oled.print(portionsServed);

//******Строка 3************
  oled.setCursor(0, 4); 
  oled.print("Left: "); oled.print(foodPercent); oled.print("%");

//******Строка 4************
struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int now = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int feed1 = 6 * 60 + 40;
    int feed2 = 15 * 60;
    oled.setCursor(0, 6);
    oled.print("Next: ");
    int left = 0;
    if (now < feed1) {
      left = (feed1 - now) / 60;
    }
    else if (now < feed2) {
      left = (feed2 - now) / 60;
    }
    else {
      left = (1440 - now + feed1) / 60;
    }
    if (left < 1) {
      oled.print("<1 h");
    }
    else { oled.print("~"); oled.print(left); oled.print(" h"); }
  }
oled.update();
}

//******************* Left Food Percent ***************************
void leftFood() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration > 0) {
    currentDistance = duration * 0.034 / 2;
    foodPercent = map(currentDistance, 25, 3, 0, 100); 
    foodPercent = constrain(foodPercent, 0, 100);
  }
}