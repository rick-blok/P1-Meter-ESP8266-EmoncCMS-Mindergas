#include <TimeLib.h>  //required for date manipulation for mindergas. install Time by Margolis via Library Manager
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "CRC16.h"
#include <TelnetPrint.h>  //https://github.com/jandrassy/TelnetStream

//===Change values from here===
const char* ssid = "WIFISSID";
const char* password = "PASSWORD";
const char* hostName = "ESPP1Meter";
const char* serverEmoncms = "http://emoncms.org/";
const char* AuthEmoncms = "AUTHCODE";
char* EmonCMSnode = "ESP-P1";
const char* serverMindergas = "http://www.mindergas.nl/api/gas_meter_readings";
const char* AuthMindergas = "AUTHCODE";
const bool outputOnSerial = true;
unsigned long updateInterval = 59100; //milliseconds. once every minute
//===Change values to here===

unsigned long lastUpdate = -updateInterval; //process first telegram

// Vars to store meter readings
long mEVLT = 0; //Meter reading Electrics - consumption low tariff
long mEVHT = 0; //Meter reading Electrics - consumption high tariff
long mEOLT = 0; //Meter reading Electrics - return low tariff
long mEOHT = 0; //Meter reading Electrics - return high tariff
long mEAV = 0;  //Meter reading Electrics - Actual consumption
long mEAT = 0;  //Meter reading Electrics - Actual return
long mGAS = 0;    //Meter reading Gas
char tGAS[14];  // time stamp gas
char prevGAS[14];
char prevMindergas[14];

#define MAXLINELENGTH 1023 // sagemcom xs210 has long line length
char telegram[MAXLINELENGTH];

unsigned int currentCRC=0;

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(hostName);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  //TelnetPrint = NetServer(2323); // uncomment to change port
  TelnetPrint.begin();
  Serial.println("Swapping serial port to Smart Meter, debug output will continue on telnet");
  Serial.end();
  delay(100);
  
  Serial.begin(115200, SERIAL_8N1, SERIAL_RX_ONLY, 255, true);  //https://github.com/esp8266/Arduino/pull/6816/
  Serial.swap();  //swapping RX to GPIO13 = D7
  // GPIO13 has pullup enabled with uart https://github.com/esp8266/Arduino/blob/master/cores/esp8266/core_esp8266_wiring_digital.cpp
}

bool SendToEmonCms(char* idx, int nValue, char* sValue)
{
  HTTPClient http;
  bool retVal = false;
  char url[255];
  sprintf(url, "%s/input/post.json?node=%s&json={%s}&apikey=%s", serverEmoncms, idx, sValue, AuthEmoncms);
  TelnetPrint.print("[HTTP] GET... URL: ");
  TelnetPrint.println(url);
  http.begin(url); //HTTP
  int httpCode = http.GET();
  // httpCode will be negative on error
  if (httpCode > 0)
  { // HTTP header has been send and Server response header has been handled
    TelnetPrint.print("[HTTP] GET... code: ");
    TelnetPrint.println(httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      retVal = true;
    }
  }
  else
  {
    TelnetPrint.println("[HTTP] GET... failed, error: ");
    TelnetPrint.println(http.errorToString(httpCode).c_str());
  }
  http.end();
  return retVal;
}

void UpdateEmoncms() {
  if ( prevGAS[0] == '\0' ) {
    // first update after boot, skip gas update
    strcpy(prevGAS, tGAS);
  }
  char sValue[255];
  TelnetPrint.println("Updating EmonCMS...");
  TelnetPrint.print("prevGAS: ");
  TelnetPrint.println(prevGAS);
  TelnetPrint.print("tGas:    ");
  TelnetPrint.println(tGAS);
  if (strncmp(prevGAS, tGAS, strlen("150531200000S")) != 0)
  { //timestamp gas has changed, so update gas.
    TelnetPrint.println("gas changed");
    sprintf(sValue, "ELVT:%d,EVHT:%d,EOLT:%d,EOHT:%d,EAV:%d,EAT:%d,GAS:%d", mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT, mGAS);
  }
  else
  { // timestamp gas not changed
    TelnetPrint.println("gas not changed");
    sprintf(sValue, "ELVT:%d,EVHT:%d,EOLT:%d,EOHT:%d,EAV:%d,EAT:%d",        mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT);
  }
  if (SendToEmonCms(EmonCMSnode, 0, sValue))
    strcpy(prevGAS, tGAS);
}

bool SendToMindergas(char* timestamp, char* value) {
  HTTPClient http;
  bool retVal = false;
  char url[255];
  char data[100];
  sprintf(url, "%s", serverMindergas);
  sprintf(data, "{\"date\": \"%s\", \"reading_l\": %s}", timestamp, value);

  TelnetPrint.print("[HTTP] POST... URL: ");
  TelnetPrint.println(url);
  TelnetPrint.print("DAT: ");
  TelnetPrint.println(data);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("AUTH-TOKEN", AuthMindergas);
  int httpCode = http.POST(data);
  // httpCode will be negative on error
  if (httpCode > 0)
  { // HTTP header has been send and Server response header has been handled
    TelnetPrint.print("[HTTP] POST... code: ");
    TelnetPrint.println(httpCode);
    // file found at server
    if (httpCode == HTTP_CODE_CREATED) {
      String payload = http.getString();
      retVal = true;
    }
  }
  else
  {
    TelnetPrint.println("[HTTP] POST... failed, error: ");
    TelnetPrint.println(http.errorToString(httpCode).c_str());
  }
  http.end();
  return retVal;
}

void UpdateMindergas() {
  if ( prevMindergas[0] == '\0' ) {
    // first update after boot, skip gas update
    strcpy(prevMindergas, tGAS);
  }
  if (strncmp(prevMindergas, tGAS, strlen("170403")))
  { // new day, upload to mindergas
    TelnetPrint.println("updating mindergas...");
    char timestamp[11];
    char sValue[10];
    int Year, Month, Day;

    sprintf(sValue, "%d", mGAS);
    // create timestamp, yesterday
    sscanf(tGAS, "%2d%2d%2d%*s", &Year, &Month, &Day);
    //setTime(Hour, Minute, Second, Day, Month, Year);
    setTime(23, 0, 0, Day, Month, Year);
    time_t gister = now() - 86400;
    sprintf(timestamp, "%04d-%02d-%02d", year(gister), month(gister), day(gister));

    if (SendToMindergas(timestamp, sValue))
      strcpy(prevMindergas, tGAS);
  }
}

int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }
  return -1;
}

bool decodeTelegram(int len) {
  //need to check for start
  int startChar = FindCharInArrayRev(telegram, '/', len);
  int endChar = FindCharInArrayRev(telegram, '!', len);
  bool validCRCFound = false;
  if(startChar>=0)
  {
    //start found. Reset CRC calculation
    currentCRC=CRC16(0x0000,(unsigned char *) telegram+startChar, len-startChar);
    if(outputOnSerial)
    {
      for(int cnt=startChar; cnt<len-startChar;cnt++)
        TelnetPrint.print(telegram[cnt]);
    }        
  }
  else if(endChar>=0)
  {
    //add to crc calc 
    currentCRC=CRC16(currentCRC,(unsigned char*)telegram+endChar, 1);
    char messageCRC[5];
    strncpy(messageCRC, telegram + endChar + 1, 4);
    messageCRC[4]=0; //thanks to HarmOtten (issue 5)
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        TelnetPrint.print(telegram[cnt]);
    }    
    validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);
    if(validCRCFound)
      TelnetPrint.println("\nVALID CRC FOUND!\n");
    else
      TelnetPrint.println("\n===INVALID CRC FOUND!===\n");
    currentCRC = 0;
  }
  else
  {
    currentCRC=CRC16(currentCRC, (unsigned char*)telegram, len);
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        TelnetPrint.print(telegram[cnt]);
    }
  }

  // temporary variables
  long tl  = 0;
  long tld = 0;

  // 1-0:1.8.1(000992.992*kWh)
  // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v4.0)
  if (sscanf(telegram, "1-0:1.8.1(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEVLT = tl * 1000 + tld;
    TelnetPrint.print("Elektra - meterstand verbruik LAAG tarief (Wh): ");
    TelnetPrint.println(mEVLT);
  }

  // 1-0:1.8.2(000560.157*kWh)
  // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v4.0)
  if (sscanf(telegram, "1-0:1.8.2(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEVHT = tl * 1000 + tld;
    TelnetPrint.print("Elektra - meterstand verbruik HOOG tarief (Wh): ");
    TelnetPrint.println(mEVHT);
  }

  // 1-0:2.8.1(000348.890*kWh)
  // 1-0:2.8.1 = Elektra opbrengst laag tarief (DSMR v4.0)
  if (sscanf(telegram, "1-0:2.8.1(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEOLT = tl * 1000 + tld;
    TelnetPrint.print("Elektra - meterstand levering LAAG tarief (Wh): ");
    TelnetPrint.println(mEOLT);
  }

  // 1-0:2.8.2(000859.885*kWh)
  // 1-0:2.8.2 = Elektra opbrengst hoog tarief (DSMR v4.0)
  if (sscanf(telegram, "1-0:2.8.2(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEOHT = tl * 1000 + tld;
    TelnetPrint.print("Elektra - meterstand levering HOOG tarief (Wh): ");
    TelnetPrint.println(mEOHT);
  }

  // 1-0:1.7.0(00.424*kW) Actueel verbruik
  // 1-0:1.7.0 = Electricity consumption actual usage (DSMR v4.0)
  if (sscanf(telegram, "1-0:1.7.0(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEAV = tl * 1000 + tld;
    TelnetPrint.print("Elektra - Actueel verbruik (W): ");
    TelnetPrint.println(mEAV);
  }

  // 1-0:2.7.0(00.000*kW) Actuele teruglevering
  // 1-0:2.7.0 = Electricity consumption actual return (DSMR v4.0)
  if (sscanf(telegram, "1-0:2.7.0(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEAT = tl * 1000 + tld;
    TelnetPrint.print("Elektra - Actueel teruglevering (W): ");
    TelnetPrint.println(mEAT);
  }

  // 0-1:24.2.1(150531200000S)(00811.923*m3)
  // 0-1:24.2.1 = Gas (DSMR v4.0) on Kaifa MA105 meter
  if (sscanf(telegram, "0-1:24.2.1(%13s)(%ld.%ld%*s" , tGAS, &tl, &tld) == 3 ) {
    mGAS = tl * 1000 + tld;
    TelnetPrint.print("GAS- timestamp: ");
    TelnetPrint.print(tGAS);
    TelnetPrint.print("; GAS- Meterstand (l): ");
    TelnetPrint.println(mGAS);
  }

  return validCRCFound;
}

void readTelegram() {
  if (Serial.available()) {
    memset(telegram, 0, sizeof(telegram));
    while (Serial.available()) {
      int len = Serial.readBytesUntil('\n', telegram, MAXLINELENGTH);
      telegram[len] = '\n';
      telegram[len+1] = 0;
      yield();
      if( decodeTelegram(len+1) ) {
      if( decodeTelegram(len+1) ) {
        if (millis() - lastUpdate >= updateInterval) {
          UpdateEmoncms();
          UpdateMindergas();
        }
      }
    } 
  }
}



void loop() {
  readTelegram();
  ArduinoOTA.handle();
}



