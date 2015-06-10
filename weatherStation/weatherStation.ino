/***************************************************
  This is a library for the Adafruit 1.8" SPI display.

This library works with the Adafruit 1.8" TFT Breakout w/SD card
  ----> http://www.adafruit.com/products/358
The 1.8" TFT shield
  ----> https://www.adafruit.com/product/802
The 1.44" TFT breakout
  ----> https://www.adafruit.com/product/2088
as well as Adafruit raw 1.8" TFT display
  ----> http://www.adafruit.com/products/618

  Check out the links above for our tutorials and wiring diagrams
  These displays use SPI to communicate, 4 or 5 pins are required to
  interface (RST is optional)
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library
#include <SPI.h>
#include "DHT.h"
#include <SoftwareSerial.h>
#include "ESP8266.h"
#include <ArduinoJson.h>

// For the breakout, you can use any 2 or 3 pins
// These pins will also work for the 1.8" TFT shield
#define TFT_CS     9
#define TFT_RST    7  // you can also connect this to the Arduino reset
                      // in which case, set this #define pin to 0!
#define TFT_DC     8

// Option 1 (recommended): must use the hardware SPI pins
// (for UNO thats sclk = 13 and sid = 11) and pin 10 must be
// an output. This is much faster - also required if you want
// to use the microSD card (see the image drawing example)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS,  TFT_DC, TFT_RST);

// Option 2: use any pins but a little slower!
#define TFT_SCLK 13   // set these to be whatever pins you like!
#define TFT_MOSI 11   // set these to be whatever pins you like!
//Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

#define DHTPIN 3 // data pin to DHT22
#define DHTTYPE DHT22 // using DHT 22
#define DUST_APIN 1 // using Dust Sensor, this's a analog pin
#define DUST_DPIN 4 // using Dust Sensor, this's a digital pin
#define SWITCH_PIN 2

#define CLOCK_CYCLE             1000
#define SENSOR_CYCLE            3000
#define TIME_ADJUST_CYCLE      60000
#define WEATHER_UPDATE_CYCLE   60000 * 60 
#define RETRY_CYCLE             5000
#define SEND_DATA_CYCLE        63300

#define MODE_DI 100
#define MODE_THI 101
#define SSID "kmc_net1"
#define PASS "kim=ip07258"
#define DST_IP "192.168.0.43" 
#define DST_PORT 5000

#define TIME_HTTP_REQUEST "/present/v1/time"
#define DATE_HTTP_REQUEST "/present/v1/date"
#define WDESCRIPTION_HTTP_REQUEST "/weather/v1/description"
#define CONDITION_HTTP_REQUEST "/condition/v1"

SoftwareSerial esp8266Serial = SoftwareSerial(5, 6); // RX, TX
ESP8266 wifi = ESP8266(esp8266Serial);

DHT dht(DHTPIN, DHTTYPE);

uint32_t targetTime = 0;                    // for next 1 second timeout 
uint32_t targetSensorTime = 0;
uint32_t targetAdjustTime = 0;
uint32_t targetWeatherTime = 0;
uint32_t targetRetryTime = 0;
uint32_t targetSendTime = 0;

uint8_t hh=conv2d(__TIME__), mm=conv2d(__TIME__+3), ss=conv2d(__TIME__+6);  // Get H, M, S from compile time
uint8_t modeState = MODE_THI;

byte omm = 99;
boolean initial = 1;
boolean isPush = false;
boolean wifiEnable = false;
byte xcolon = 0;
int text_color_humidex;
int text_color_di;
boolean dateUpdateFlag = false;
String date = "";
String weather = "";

//JsonObject& makeJsonData(float rh, float temp, float pm);

void setup(void) {
  Serial.begin(9600);
  esp8266Serial.begin(9600);
  
  /************************************/
  /******      esp8266         ********/
  /************************************/  
  wifi.begin();
  wifi.setTimeout(1000);
 
 
  // test
  Serial.print(F("test: "));
  Serial.println(getStatus(wifi.test()));
 
  // restart
  Serial.print(F("restart: "));
  Serial.println(getStatus(wifi.restart()));
 
  // getVersion
  char version[16] = {};
  Serial.print(F("getVersion: "));
  Serial.print(getStatus(wifi.getVersion(version, 16)));
  Serial.print(F(" : "));
  Serial.println(version);
 
  /****************************************/
  /******        WiFi commands       ******/
  /****************************************/
  // joinAP
  Serial.print(F("joinAP: "));
  String result = getStatus(wifi.joinAP(SSID, PASS));
  Serial.println(result);
  if(result == "OK") wifiEnable = true;
  
  /************************************/
  /******        pin setting     ******/  
  /************************************/
  pinMode(DUST_DPIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  dht.begin();
  
  /************************************/
  /******   tft and time init    ******/
  /************************************/
  //tft.initR(INITR_GREENTAB);
  tft.initR(INITR_BLACKTAB);
  //tft.initR(INITR_REDTAB);
  //tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);

  tft.setTextColor(ST7735_YELLOW, ST7735_BLACK); // Note: the new fonts do not draw the background colour

  targetTime = millis() + CLOCK_CYCLE; 
  targetSensorTime = millis() + SENSOR_CYCLE;
  
  drawLargeBox(126, 160, ST7735_GREEN);
}

// loop start
void loop() {
  /************************************/
  /******      button part     ********/
  /************************************/ 
  // button normal HIGH signal. button push LOW Signal.
  if(digitalRead(SWITCH_PIN)) isPush = false; 
  
  if(!digitalRead(SWITCH_PIN) && !isPush){
    isPush = true;
    modeState ==  MODE_THI ? modeState = MODE_DI : modeState = MODE_THI;     
  } 
  
  
  /************************************/
  /****** time and sensor part ********/
  /************************************/  
  
  /******   update clock    ******/
  // default CLOCK_CYCLE 1000 
  if (targetTime < millis()) {
    targetTime = millis() + CLOCK_CYCLE;
    processDigitalClock();
    printDigitalClock();    
  }
  
  /******   update sensor every 3 second ****/
  // default SENSOR_CYCLE 3000    
  if (targetSensorTime < millis()) {
    targetSensorTime = millis() + SENSOR_CYCLE;
    printSensorInfo(dht.readHumidity(), dht.readTemperature(), getDustDensity(analogRead(DUST_APIN)));
    //Serial.print("esp power voltage : ");
    //Serial.println(float(analogRead(1)/1023.0*5.0));
  }
  
  /************************************/
  /***** when wifi enable status  *****/
  /************************************/
  if(wifiEnable){
    /******   adjust clock    ******/
    if (targetAdjustTime < millis()){
      targetAdjustTime = millis() + TIME_ADJUST_CYCLE;
      String tmp = getDataFromServer(TIME_HTTP_REQUEST); 
      adjustClock((char*)tmp.c_str());
    }
    
    /******   update date    ******/
    if (hh == 23 && mm == 59 && !dateUpdateFlag) dateUpdateFlag = true;  
    if (hh == 0  && mm == 0  &&  dateUpdateFlag){
      date = "";
      String tmp = getDataFromServer(DATE_HTTP_REQUEST); 
      updateDate((char*)tmp.c_str());
    }
    
    /******   update weather    ******/
    if (targetWeatherTime < millis()){
      weather = "";
      targetWeatherTime = millis() + WEATHER_UPDATE_CYCLE;
      String tmp = getDataFromServer(WDESCRIPTION_HTTP_REQUEST); 
      updateWeather((char*)tmp.c_str());
    }
    
    /******   when weather or date failed to update    ******/
    if(targetRetryTime < millis() && date == ""){
      targetRetryTime = millis() + RETRY_CYCLE;
      String tmp = getDataFromServer(DATE_HTTP_REQUEST); 
      updateDate((char*)tmp.c_str());
    }  
    if(targetRetryTime < millis() && weather == ""){
      targetRetryTime = millis() + RETRY_CYCLE;
      String tmp = getDataFromServer(WDESCRIPTION_HTTP_REQUEST); 
      updateWeather((char*)tmp.c_str());
    }
    
    if(targetSendTime < millis() && date != "" && weather != ""){
      targetSendTime = millis() + SEND_DATA_CYCLE;
      String data = makeJsonData(dht.readHumidity(), dht.readTemperature(), getDustDensity(analogRead(DUST_APIN)));
      //Serial.println(data);
      int num = data.length();  
      String cmd = "";
      cmd = "POST /condition/v1 HTTP/1.1\r\n";
      //Serial.println(cmd);
      cmd = cmd + "Content-Type: application/json\r\nContent-Length: " + String(num) + "\r\n\r\n" + data + "\r\n\r\n";
      //Serial.println(cmd);
      sendDataToServer(cmd);
      // send data 
    }
  }
}// loop end



// =================================================================//
static uint8_t conv2d(const char* p) {
  uint8_t v = 0;
  if ('0' <= *p && *p <= '9')
    v = *p - '0';
  return 10 * v + *++p - '0';
}

void drawLargeBox(int16_t x, int16_t y, uint16_t color){
  tft.drawRect(tft.width()/2 -x/2, tft.height()/2 -y/2, x, y, color);
  tft.drawLine(126, 39, 1, 39, ST7735_GREEN);
  tft.drawLine(126, 64, 1, 64, ST7735_GREEN);
  tft.drawLine(126, 137, 1, 137, ST7735_GREEN);
}

void processDigitalClock(){
  ss++;              // Advance second
  if (ss==60) {      // ss start
    ss=0;
    omm = mm;
    mm++;            // Advance minute
    if(mm>59) {      // mm start
      mm=0;
      hh++;          // Advance hour
      if (hh>23) {
        hh=0;
      } 
    }                // mm end
  }                  // ss end
}

void printDigitalClock(){
//  if (/*ss==0 ||*/ initial) {
//    initial = 0;
//    tft.setTextColor(0xF81F, ST7735_BLACK);
//    tft.setCursor (27, 43);
//    tft.setTextSize(1);
//    tft.print(__DATE__); 
//
//    tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
//    tft.setCursor (27, 54);
//    tft.print("It is windy");
//  }

  // Update digital time
  byte xpos = 6;
  byte ypos = 3;
  if (omm != mm) { 
    tft.setTextSize(4);
    tft.setTextColor(0x39C4, ST7735_BLACK);  
    tft.setCursor (6, 6);
    tft.print("88:88");
    tft.setTextColor(ST7735_GREEN, ST7735_BLACK); 
    omm = mm;
    
    tft.setCursor (6, 6);
    if (hh<10) tft.print('0');
      
    tft.print(hh);
      
    tft.print(':');
    if (mm<10) tft.print('0');
    tft.print(mm);
  }

  if (ss%2) { // Flash the colon
    tft.setTextColor(0x39C4, ST7735_BLACK);
    tft.setCursor (53, 6);
    tft.setTextSize(4);
    tft.print(':');
    tft.setTextColor(ST7735_GREEN, ST7735_BLACK);
  }
  else {
    tft.setTextColor(ST7735_GREEN, ST7735_BLACK);
    tft.setCursor (53, 6);
    tft.setTextSize(4);
    tft.print(':');   
  }
}

String getDataFromServer(String cmd){
  //wifi.setTimeout(1000);
  Serial.print(F("connect: "));    
  Serial.println(getStatus(wifi.connect(ESP8266_PROTOCOL_TCP, DST_IP, DST_PORT)));
  delay(300);
    
  Serial.print(F("send: "));
  cmd = "GET "+ cmd + " HTTP/1.1\n\n";
  Serial.println(cmd);
  Serial.println(getStatus(wifi.send(cmd)));
    
  delay(200);
    
  // read data
  unsigned int id;
  int length;
  int totalRead;
  char buffer[300] = {};
  boolean cutFlag = false;
  String tmp = "";
    
  if ((length = wifi.available()) > 0) {
    id = wifi.getId();
    totalRead = wifi.read(buffer, 300);
 
    if (length > 0) {
      Serial.print(F("Received "));
      Serial.print(totalRead);
      Serial.print("/");
      Serial.print(length);
      Serial.print(F(" bytes from client "));
      //Serial.print("from client ");
      Serial.print(id);
      Serial.print(F(" : "));
      Serial.println((char*)buffer);
               
      //parse json data
      for(int i=0; i<300;i++){
        if(!cutFlag && buffer[i] == '{') cutFlag = true;     
                  
        if(cutFlag) tmp += buffer[i];
          
        if(cutFlag && buffer[i] == '}') cutFlag = false;
      }
      return tmp;
    }      
  }
}

void sendDataToServer(String cmd){
  //wifi.setTimeout(1500);
  //data.printTo(Serial);
  //Serial.println(cmd);
  Serial.print(F("connect: "));    
  Serial.println(getStatus(wifi.connect(ESP8266_PROTOCOL_TCP, DST_IP, DST_PORT)));
  delay(300);
  
  Serial.print(F("send: "));
  Serial.println(getStatus(wifi.send(cmd)));  
  delay(200);
    
  // read data
  unsigned int id;
  int length;
  int totalRead;
  char buffer[300] = {};
  boolean cutFlag = false;
  String tmp = "";
    
  if ((length = wifi.available()) > 0) {
    id = wifi.getId();
    totalRead = wifi.read(buffer, 300);
 
    if (length > 0) {
      Serial.print(F("Received "));
      Serial.print(totalRead);
      Serial.print(F("/"));
      Serial.print(length);
      Serial.print(F(" bytes from client "));
      //Serial.print("from client ");
      Serial.print(id);
      Serial.print(F(" : "));
      Serial.println((char*)buffer);
               
    }      
  }
}

void adjustClock(char* timeInfo){
  //Serial.println(timeInfo);
  StaticJsonBuffer<100> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(timeInfo);

  if (!root.success()) {
    Serial.println(F("parseObject() failed")); 
  }
  else{
    Serial.println(F("parse success"));
    hh = root["ho"];
    mm = root["mi"];
    ss = root["se"];
  }
}

void updateDate(char* dateInfo){
  Serial.println(dateInfo);
  StaticJsonBuffer<100> jsonBuffer;
  JsonObject& data = jsonBuffer.parseObject(dateInfo);

  if (!data.success()) {
    Serial.println(F("parseObject() failed")); 
  }
  else{
    Serial.println(F("parse success"));
    const char* mo = data["mo"];
    int da = data["da"];
    const char* yo = data["yo"];
    int ye  = data["ye"];

    date = mo;  date += " ";
    date += da; date += " ";
    date += yo; date += " ";
    date += ye; 
    //Serial.println(date);
    
    tft.setTextColor(0xF81F, ST7735_BLACK);
    tft.setTextSize(1);
    tft.setCursor (23, 43);
    tft.print(F("              "));
    tft.setCursor (23, 43);
    tft.print(date);
    
    dateUpdateFlag = false;
  }
}

void updateWeather(char* weatherInfo){
  Serial.println(weatherInfo);
  StaticJsonBuffer<100> jsonBuffer;
  JsonObject& data = jsonBuffer.parseObject(weatherInfo);

  if (!data.success()) {
    Serial.println(F("parseObject() failed")); 
  }
  else{
    Serial.println(F("parse success"));
    const char* des = data["des"];
    weather = des;
    tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
    tft.setTextSize(1);
    tft.setCursor (27, 54);
    tft.print(F("              "));
    tft.setCursor (27, 54);
    tft.print(weather);
  }
}

String makeJsonData(float rh, float temp, float pm){
  String data = "{\"uid\":"+ String(1) + "," + "\"temp\":" + String(temp) + "," +  
                 "\"hum\":" + String(rh) + "," + /*"\"thi\":" + String(calHumidex(temp, rh)) + "," + "\"di\":" + String(calDi(temp, rh)) + "," +*/ 
                 "\"pm\":" + String(pm)+ "}";
  return data;
}

void printSensorInfo(float rh, float temp, float pm){
  tft.setTextSize(2);
  if(temp >= 10 && temp < 20){
    tft.setTextColor(tft.Color565(0,245,255), ST7735_BLACK);
  }
  else if(temp >= 20 && temp < 30){
    tft.setTextColor(tft.Color565(255,228,225), ST7735_BLACK);
  }
  else if(temp >= 30) tft.setTextColor(ST7735_RED, ST7735_BLACK);
  tft.setCursor (10, 69);
  tft.print("Tem:"); tft.print(temp, 1); tft.print("C");
  
  if(rh >= 10 && rh < 50){
    tft.setTextColor(getColor(51,102,255), ST7735_BLACK);
  }
  else if(rh >= 50){
    tft.setTextColor(ST7735_BLUE, ST7735_BLACK);
  }
  tft.setCursor (10, 88);
  tft.print("RH :"); tft.print(rh,1); tft.print("%");
  
  String message;  
  tft.setCursor(10, 108);
  if(modeState == MODE_THI){
    float thi = calHumidex(temp, rh);  // American THI
    message = getHumidexMessage(thi);
    tft.setTextColor(text_color_humidex, ST7735_BLACK);
    tft.print("THI:"); tft.print(thi, 1); tft.print("C");
  }
  else{
    float di = calDi(temp, rh);  // korean THI
    if(di >= 100) di = 99.9;
    message = getDiMessage(di);
    tft.setTextColor(text_color_di, ST7735_BLACK);
    tft.print("DI :"); tft.print(di, 1); tft.print("%");
  }
  
  tft.setCursor(10, 125);
  tft.setTextSize(1);
  tft.print(message);
    
  tft.setCursor(10, 142);
  tft.setTextSize(2);
  tft.setTextColor(getColor(193,205,193), ST7735_BLACK);
  tft.print("PM:");  tft.print(pm); tft.print("mg");
}

float calDi(float temp, float rh){
  float di = (9/5 * temp) - 0.55*(1.0 - rh) * (9/5 * temp - 26.0) + 32.0;   
  return di;
}

float calHumidex(float temp,float rh) {
  float e = (6.112 * pow(10,(7.5 * temp/(237.7 + temp))) * rh/100); //vapor pressure

  float thi = temp + 0.55555555 * (e - 10.0); //humidex  
  
  return thi;
}

float getDustDensity(int val){ return 0.17*(val*0.0049)-0.1; }

String getDiMessage(float di){
  String message;
  if ( di < 70 ){
    text_color_di=tft.Color565(255,250,205);
    message= "Comfortable       ";
  } // dark green
  else if ((di >= 70 )&&(di < 75)){
    text_color_di=tft.Color565(255,255,0); // yellow
    message= "Some discomfort   ";
  }
  else if ((di >= 75 )&&(di < 80)){
    text_color_di=tft.Color565(221, 128, 0);  //dark orange
    message= "discomfort        ";
  } 
  else if (di >= 80){
    text_color_di=tft.Color565(255, 0, 0);  // red
    message= "Great discomfort  ";
  } 
  
  return message;
}

String getHumidexMessage(float humidex){
  String message;
  
  if ((humidex >= 21 )&&(humidex < 30)){
    text_color_humidex=tft.Color565(255,250,205);
    message= "No discomfort     ";
  } // dark green
  //  if ((humidex >= 27 )&&(humidex < 35))
  else if ((humidex >= 30 )&&(humidex < 35)){
    text_color_humidex=tft.Color565(255,255,0); // yellow
    message= "Some discomfort   ";
  }
  else if ((humidex >= 35 )&&(humidex < 40)){
    text_color_humidex=tft.Color565(255,185,15); // 
    message= "Great discomfort  ";
  } 
  else if ((humidex >= 40 )&&(humidex < 46)){
    text_color_humidex=tft.Color565(255, 140, 0);  //light orange
    message= "Health risk       ";
  } 
  else if ((humidex >= 46 )&&(humidex < 54)){
    text_color_humidex=tft.Color565(221, 128, 0);  //dark orange
    message= "Great health risk ";
  } 
  else if ((humidex >= 54 )){
    text_color_humidex=tft.Color565(255, 0, 0);  // red
    message= "Heat stroke danger";
  } 
  
  return message;
}

uint16_t getColor(uint8_t red, uint8_t green, uint8_t blue){
  red   >>= 3;
  green >>= 2;
  blue  >>= 3;
  return (red << 11) | (green << 5) | blue;
}

//=================================================================//
String getStatus(bool status)
{
    if (status)
        return "OK";
 
    return "KO";
}
 
String getStatus(ESP8266CommandStatus status)
{
    switch (status) {
    case ESP8266_COMMAND_INVALID:
        return "INVALID";
        break;
 
    case ESP8266_COMMAND_TIMEOUT:
        return "TIMEOUT";
        break;
 
    case ESP8266_COMMAND_OK:
        return "OK";
        break;
 
    case ESP8266_COMMAND_NO_CHANGE:
        return "NO CHANGE";
        break;
 
    case ESP8266_COMMAND_ERROR:
        return "ERROR";
        break;
 
    case ESP8266_COMMAND_NO_LINK:
        return "NO LINK";
        break;
 
    case ESP8266_COMMAND_TOO_LONG:
        return "TOO LONG";
        break;
 
    case ESP8266_COMMAND_FAIL:
        return "FAIL";
        break;
 
    default:
        return "UNKNOWN COMMAND STATUS";
        break;
    }
}
