#include "DHT.h"
#include "GyverOLED.h"
#include "WiFi.h"
#include "time.h"
#include "GyverStepper.h"
#include <Wire.h>

//всё что с тг связанно
#define BOT_TOKEN ""
#define CHAT_ID "" // вести свои данные от бота
const char* ssid = ""; //Название сети!!!
const char* password = ""; //Пароль!!!



GStepper<STEPPER4WIRE> stepper(2048, 0, 2, 1, 3);

// *** Переменные ***
int portionsServed = 0;
int lastDay = -1;
bool feeding = false;
int foodPercent = 0;
int currentDistance = 0;
float temp = 0;
float hum = 0; 

const char* ntpServer = "pool.ntp.org"; // время берём от сервера, который находтся в GMC +0
const long gmtOffset_sec = 39600; // Это смещение времени в секундах для часового пояса
const int daylightOffset_sec = 0;

bool fed_06 = false;
bool fed_15 = false; //Эти значения нужны чтобы действия не повторялись каждую секунду

GyverOLED<SSH1106_128x64, OLED_BUFFER> oled;

#define BUTTON_PIN 10 //инициализация кнопки
#define DHTPIN 6 //инициализация датчика влажности
#define TRIG_PIN 5
#define ECHO_PIN 9
#define STEPS_FRW 20
#define STEPS_BKW 5
#define FEED_SPEED 8000

const byte drvPins[] = {0, 1, 2, 3}; // Фазы мотора (A1, A2, B1, B2)
const byte steps[] = {0b1010, 0b0110, 0b0101, 0b1001}; // Маска фаз
int feedAmount = 10; // ЭТО ЗНАЧЕНИЕ С ТГ БРАТЬ
DHT dht(6, DHT11); //какой вид датчика

unsigned long lastTime_5min = 0; //Эти две строчки отвечают за то, что оба отчета начинаются с 0 секунд
const unsigned long interval_5min = 3000; // поменять потом на 5 минут 


//******** Function for telegramm ***************
void telegCheck() {
}
//*************************************************

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
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  leftFood();
}

void loop() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_mday != lastDay) {
    portionsServed = 0;
    lastDay = timeinfo.tm_mday;
  }
    if (timeinfo.tm_hour == 12 && timeinfo.tm_min == 15 && !fed_06) {
    catFeed();
    fed_06 = true;
  }
    if (timeinfo.tm_hour == 15 && timeinfo.tm_min == 0 && !fed_15) {
    catFeed();
    fed_15 = true;
  }
    if (timeinfo.tm_hour != 12 && timeinfo.tm_hour != 15) {
    fed_06 = false;
    fed_15 = false;
  }
}

  unsigned long currentTime = millis(); //Это мы получаем время, которое прошло

// ************** Dht data checker ***********************8
  if (currentTime - lastTime_5min >= interval_5min) {
    lastTime_5min = currentTime;
    hum = dht.readHumidity();
    temp = dht.readTemperature() - 3;
    if (isnan(hum) || isnan(temp)) {
      Serial.println("Ошибка чтения датчика температуры");
    }
    displayData();
  }

// ****** Button logic *****************
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // антидребезг
    Serial.println("Кнопка нажата");
    catFeed();
  }
}



//******** Function for easy catfeed ******************
void catFeed() {
  feeding = true;
  displayData();
  Serial.println("КормимКормим");
  for (int i = 0; i < feedAmount; i++) {
    oneRev();
  }
  disableMotor();
  portionsServed++;
  feeding = false;
  leftFood();
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

  // --- СТРОКА 1: Температура и влажность ---
  // Установил (0, 0) - самое начало. Если не видно, попробуй (2, 0)
  oled.setCursor(0, 0); 
  oled.print("HUM: ");
  if (isnan(hum)) {
    oled.print("NAN"); // Если датчик не отвечает
  } else {
    oled.print(hum, 0); oled.print("%");
  }

  oled.print(" TEMP: ");
  if (isnan(temp)) {
    oled.print("NAN"); // Если датчик не отвечает
  } else {
    oled.print(temp, 1); oled.print("C");
  }

  // --- СТРОКА 2: Выданные порции ---
  // В режиме SSH1106 вертикальный шаг идет по "строкам" (0, 1, 2... до 7)
  // Мы ставим на 2-ю строку, чтобы текст не слипался
  oled.setCursor(0, 2); 
  oled.print("Served: "); oled.print(portionsServed);

  // --- СТРОКА 3: Остаток корма ---
  oled.setCursor(0, 4); 
  oled.print("Left: "); oled.print(foodPercent); oled.print("%");

  // --- СТРОКА 4: Статус кормления ---
  if (feeding) {
    oled.setCursor(0, 6); // Самый низ экрана
    oled.print(">>> FEEDING! <<<");
  }
  oled.update(); // Отправляем всё из памяти на экран
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

