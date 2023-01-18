/*
  smart cat toilet for M5Stick C Plus
  item
  M5Atom Lite

  WEIGHT UNIT
  Load Cell 10kg https://akizukidenshi.com/catalog/g/gP-13042/

  library
  HX711 library https://github.com/RobTillaart/HX711
  ArduinoJson

  このコードはベータ版です
  問題点
  日毎の回数カウントが途中でリセットされることがあります（日付データの受信周り）
  Googleスプレッドシートへのグラフ出力未実装
*/

#include "HX711.h"
#include <M5Atom.h>
#include "MovingAverageFloat.h"

#include <WiFi.h>
#include <stdlib.h>
#include "time.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

MovingAverageFloat<16> filter;

const char* server = "maker.ifttt.com";  // IFTTT Server URL

//=====ここから個別に設定が必要な項目=====

const char* ssid = "********";      // Wi-FiのSSID
const char* password = "********";  // Wi-Fiのパスワード

String makerEvent = "water";   // Maker Webhooks
String makerKey = "********";  // Maker Webhooks

// GoogleスプレッドシートのデプロイされたURLを設定
const char* published_url = "https://script.google.com/************/exec";

const int place = 1;  //水飲みのユニークナンバー

//ロードセルの係数設定 SCALES KITは200kg
// 200kg 30.000 | 32kg 100.8 | 20kg 127.15 | 10kg 256.30 | 5kg 420.0983
#define cal 208.650

//=====ここまで個別に設定が必要な項目=====

WiFiClient client;

const char* ntpServer = "ntp.jst.mfeed.ad.jp";
const long gmtOffset_sec = 9 * 3600;
const int daylightOffset_sec = 0;

#define FRONT 1

#define X_LOCAL 40
#define Y_LOCAL 40
#define X_F 30
#define Y_F 30

const int capacity = JSON_OBJECT_SIZE(2);
StaticJsonDocument<capacity> json_request;
char buffer[255];

HX711 scale;

uint8_t dataPin = 32;
uint8_t clockPin = 26;

uint32_t start, stop;
volatile float f;

int notZero = 0;
float oldWeight = 0.00;
boolean flg = false;

float weightDay = 0;  //1日のトータル飲水量
int count = 0;        //1日の飲水回数
int today;

void wifiConnect() {
  WiFi.begin(ssid, password);              //  Wi-Fi APに接続
  while (WiFi.status() != WL_CONNECTED) {  //  Wi-Fi AP接続待ち
    M5.dis.drawpix(0, 0x000055);
    delay(500);
    M5.dis.drawpix(0, 0x000000);
    Serial.print(".");
  }
  Serial.print("WiFi connected\r\nIP address: ");
  Serial.println(WiFi.localIP());
  M5.dis.drawpix(0, 0x000000);
}

void setup() {
  M5.begin(true, false, true);
  delay(50);
  M5.dis.drawpix(0, 0x000000);
  Serial.println("black");
  scale.begin(dataPin, clockPin);
  wifiConnect();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeInfo;
  getLocalTime(&timeInfo);
  char s[20];
  sprintf(s, "%04d/%02d/%02d %02d:%02d:%02d",
          timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
          timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
  today = timeInfo.tm_mday;
  WiFi.disconnect(true);

  // TODO find a nice solution for this calibration..
  // load cell factor 32 KG
  scale.set_scale(cal);

  Serial.begin(115200);
  delay(1000);
  // reset the scale to zero = 0
  scale.tare();

  oldWeight = 1;
  M5.dis.drawpix(0, 0x000000);
}

//GASにデータ送信
void sendGoogle(float AwDiff) {
  // StaticJsonDocument<500> doc;
  // char pubMessage[256];

  // JsonArray dataValues = doc.createNestedArray("value");
  // dataValues.add(weight);

  // JsonArray datawDiff = doc.createNestedArray("wdiff");
  // datawDiff.add(wDiff);

  // serializeJson(doc, pubMessage);

  // // HTTP通信開始
  // HTTPClient http;

  // Serial.print(" HTTP通信開始　\n");
  // http.begin(published_url);

  // Serial.print(" HTTP通信POST　\n");
  // int httpCode = http.POST(pubMessage);

  // if (httpCode > 0) {
  //   Serial.print(" HTTP Response:");
  //   Serial.println(httpCode);

  //   if (httpCode == HTTP_CODE_OK) {
  //     Serial.println(" HTTP Success!!");
  //     String payload = http.getString();
  //     Serial.println(payload);
  //   }
  // } else {
  //   //      Serial.println(" FAILED");
  //   Serial.printf("　HTTP　failed,error: %s\n", http.errorToString(httpCode).c_str());
  // }

  // http.end();
}

//IFTTTにパラメータを送信
void sendToIFTTT(float value1, int count, float value4) {

  //  int value2 = place;

  String url = "/trigger/" + makerEvent + "/with/key/" + makerKey;
  url += "?value1=";
  url += value1;
  url += "&value2=";
  //  url += value2;
  url += count;
  url += "&value3=";
  //  url += count;
  url += value4;
  // url += "&value4=";
  // url += value4;

  Serial.println("\nStarting connection to server...");
  if (!client.connect(server, 80)) {
    Serial.println("Connection failed!");
  } else {
    Serial.println("Connected to server!");
    // Make a HTTP request:
    client.println("GET " + url + " HTTP/1.1");
    client.print("Host: ");
    client.println(server);
    client.println("Connection: close");
    client.println();
    Serial.print("Waiting for response ");  //WiFiClientSecure uses a non blocking implementation

    while (!client.available()) {
      delay(50);  //
      Serial.print(".");
    }
    // if there are incoming bytes available
    // from the server, read them and print them:
    while (client.available()) {
      char c = client.read();
      Serial.write(c);
    }

    // if the server's disconnected, stop the client:
    if (!client.connected()) {
      Serial.println();
      Serial.println("disconnecting from server.");
      client.stop();
    }
  }
}

void loop() {
  M5.update();
  if (M5.Btn.wasReleased()) {
    scale.tare();
  }
  M5.dis.drawpix(0, 0x333300);
  // continuous scale 4x per second
  f = scale.get_units(5);
  float weight = f;
  float wDiff = weight - oldWeight;
  filter.add(wDiff);
  float AwDiff = filter.get();
  // Serial.print(f);
  // Serial.print(" ");
  struct tm timeInfo;
  getLocalTime(&timeInfo);
  char s[20];
  sprintf(s, "%04d/%02d/%02d %02d:%02d:%02d",
          timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
          timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
  Serial.println(s);
  Serial.print("今日は");
  Serial.print(timeInfo.tm_mday);
  Serial.println("日");

  Serial.print(weight);
  Serial.print("-");
  Serial.print(oldWeight);
  Serial.print("=");
  Serial.print(wDiff);
  Serial.print(":M-Ave=");
  Serial.println(AwDiff);

  // 1g以上の変動が101回連続したら送信判定してゼロ点を変更する
  if (abs(AwDiff) > 0.3) {
    notZero++;
    Serial.println(notZero);
    if (notZero > 100) {
      //3g以上マイナス変異があったらクラウド側に送信する
      if (AwDiff <= -2 && abs(AwDiff) < 100) {
        Serial.print("Send Value = ");
        Serial.println(AwDiff);
        M5.dis.drawpix(0, 0x005500);
        wifiConnect();
        M5.dis.drawpix(0, 0x005555);

        struct tm timeInfo;
        getLocalTime(&timeInfo);
        Serial.println(timeInfo.tm_mday);

        if (today != timeInfo.tm_mday) {
          count = 0;
          weightDay = 0;
          today = timeInfo.tm_mday;
        }
        weightDay = weightDay + abs(AwDiff);
        count++;
        sendGoogle(abs(AwDiff));
        sendToIFTTT(abs(AwDiff), count, weightDay);
        WiFi.disconnect(true);
        Serial.println("Wi-Fi disconnected.");
      }
      Serial.println("0 Reset");
      oldWeight = oldWeight + AwDiff;  //ゼロ点をズラす
      notZerosssss = 0;
    }
  } else notZero = 0;
}