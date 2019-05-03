// #include <TimeLib.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "CRC16.h"

//===Change values from here===
const char* ssid = "WIFISSID";
const char* password = "PASSWORD";
const char* hostName = "ESPP1Meter";
const char* domoticzIP = "192.168.1.35";
const int domoticzPort = 8090;
const int domoticzGasIdx = 291;
const int domoticzEneryIdx = 294;
const char* serverEmoncms = "http://emoncms.org/";
const char* AuthEmoncms = "AUTHCODE";
char* EmonCMSnode = "ESP-P1";
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


#define MAXLINELENGTH 1023 // sagemcom xs210 has long line lenghth
char telegram[MAXLINELENGTH];

#define SERIAL_RX     D5  // pin for SoftwareSerial RX
SoftwareSerial mySerial(SERIAL_RX, -1, true, MAXLINELENGTH); // (RX, TX. inverted, buffer)

unsigned int currentCRC=0;

void SendToDomoLog(char* message)
{
  char url[512];
  sprintf(url, "http://%s:%d/json.htm?type=command&param=addlogmessage&message=%s", domoticzIP, domoticzPort, message); 
}

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
  mySerial.begin(115200);

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
}

bool SendToEmonCms(char* idx, int nValue, char* sValue)
{
  HTTPClient http;
  bool retVal = false;
  char url[255];
  sprintf(url, "%s/input/post.json?node=%s&json={%s}&apikey=%s", serverEmoncms, idx, sValue, AuthEmoncms);
  Serial.printf("[HTTP] GET... URL: %s\n", url);
  http.begin(url); //HTTP
  int httpCode = http.GET();
  // httpCode will be negative on error
  if (httpCode > 0)
  { // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      retVal = true;
    }
  }
  else
  {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
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
  Serial.println("Updating EmonCMS...");
  Serial.print("prevGAS: ");
  Serial.println(prevGAS);
  Serial.print("tGas:    ");
  Serial.println(tGAS);
  if (strncmp(prevGAS, tGAS, strlen("150531200000S")) != 0)
  { //timestamp gas has changed, so update gas.
    Serial.println("gas changed");
    sprintf(sValue, "ELVT:%d,EVHT:%d,EOLT:%d,EOHT:%d,EAV:%d,EAT:%d,GAS:%d", mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT, mGAS);
  }
  else
  { // timestamp gas not changed
    Serial.println("gas not changed");
    sprintf(sValue, "ELVT:%d,EVHT:%d,EOLT:%d,EOHT:%d,EAV:%d,EAT:%d",        mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT);
  }
  if (SendToEmonCms(EmonCMSnode, 0, sValue))
    strcpy(prevGAS, tGAS);
}

bool SendToDomo(int idx, int nValue, char* sValue)
{
  HTTPClient http;
  bool retVal = false;
  char url[255];
  sprintf(url, "http://%s:%d/json.htm?type=command&param=udevice&idx=%d&nvalue=%d&svalue=%s", domoticzIP, domoticzPort, idx, nValue, sValue);
  Serial.printf("[HTTP] GET... URL: %s\n",url);
  http.begin(url); //HTTP
  int httpCode = http.GET();
  // httpCode will be negative on error
  if (httpCode > 0)
  { // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      retVal = true;
    }
  }
  else
  {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
  return retVal;
}



void UpdateGas()
{
  //sends over the gas setting to domoticz
  if(strncmp(prevGAS, tGAS, strlen("150531200000S")) != 0)
  { //timestamp gas has changed, so update gas.
    char sValue[10];
    sprintf(sValue, "%d", mGAS);
    if(SendToDomo(domoticzGasIdx, 0, sValue))
      strcpy(prevGAS, tGAS);
  }
}

void UpdateElectricity()
{
  char sValue[255];
  sprintf(sValue, "%d;%d;%d;%d;%d;%d", mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT);
  SendToDomo(domoticzEneryIdx, 0, sValue);
}


bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}

int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }
  return -1;
}

long getValidVal(long valNew, long valOld, long maxDiffer)
{
  //check if the incoming value is valid
      if(valOld > 0 && ((valNew - valOld > maxDiffer) && (valOld - valNew > maxDiffer)))
        return valOld;
      return valNew;
}

long getValue(char* buffer, int maxlen) {
  int s = FindCharInArrayRev(buffer, '(', maxlen - 2);
  if (s < 8) return 0;
  if (s > 32) s = 32;
  int l = FindCharInArrayRev(buffer, '*', maxlen - 2) - s - 1;
  if (l < 4) return 0;
  if (l > 12) return 0;
  char res[16];
  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l)) {
    if (isNumber(res, l)) {
      return (1000 * atof(res));
    }
  }
  return 0;
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
        Serial.print(telegram[cnt]);
    }    
    //Serial.println("Start found!");
    
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
        Serial.print(telegram[cnt]);
    }    
    validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);
    if(validCRCFound)
      Serial.println("\nVALID CRC FOUND!"); 
    else
      Serial.println("\n===INVALID CRC FOUND!===");
    currentCRC = 0;
  }
  else
  {
    currentCRC=CRC16(currentCRC, (unsigned char*)telegram, len);
    if(outputOnSerial)
    {
      for(int cnt=0; cnt<len;cnt++)
        Serial.print(telegram[cnt]);
    }
  }

  // temporary variables
  long tl  = 0;
  long tld = 0;

  // 1-0:1.8.1(000992.992*kWh)
  // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v4.0)
  if (sscanf(telegram, "1-0:1.8.1(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEVLT = tl * 1000 + tld;
    Serial.print("Elektra - meterstand verbruik LAAG tarief (Wh): ");
    Serial.println(mEVLT);
  }

  // 1-0:1.8.2(000560.157*kWh)
  // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v4.0)
  if (sscanf(telegram, "1-0:1.8.2(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEVHT = tl * 1000 + tld;
    Serial.print("Elektra - meterstand verbruik HOOG tarief (Wh): ");
    Serial.println(mEVHT);
  }

  // 1-0:2.8.1(000348.890*kWh)
  // 1-0:2.8.1 = Elektra opbrengst laag tarief (DSMR v4.0)
  if (sscanf(telegram, "1-0:2.8.1(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEOLT = tl * 1000 + tld;
    Serial.print("Elektra - meterstand levering LAAG tarief (Wh): ");
    Serial.println(mEOLT);
  }

  // 1-0:2.8.2(000859.885*kWh)
  // 1-0:2.8.2 = Elektra opbrengst hoog tarief (DSMR v4.0)
  if (sscanf(telegram, "1-0:2.8.2(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEOHT = tl * 1000 + tld;
    Serial.print("Elektra - meterstand levering HOOG tarief (Wh): ");
    Serial.println(mEOHT);
  }

  // 1-0:1.7.0(00.424*kW) Actueel verbruik
  // 1-0:1.7.0 = Electricity consumption actual usage (DSMR v4.0)
  if (sscanf(telegram, "1-0:1.7.0(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEAV = tl * 1000 + tld;
    Serial.print("Elektra - Actueel verbruik (W): ");
    Serial.println(mEAV);
  }

  // 1-0:2.7.0(00.000*kW) Actuele teruglevering
  // 1-0:2.7.0 = Electricity consumption actual return (DSMR v4.0)
  if (sscanf(telegram, "1-0:2.7.0(%ld.%ld%*s" , &tl, &tld) == 2 ) {
    mEAT = tl * 1000 + tld;
    Serial.print("Elektra - Actueel teruglevering (W): ");
    Serial.println(mEAT);
  }

  // 0-1:24.2.1(150531200000S)(00811.923*m3)
  // 0-1:24.2.1 = Gas (DSMR v4.0) on Kaifa MA105 meter
  if (sscanf(telegram, "0-1:24.2.1(%13s)(%ld.%ld%*s" , tGAS, &tl, &tld) == 3 ) {
    mGAS = tl * 1000 + tld;
    Serial.print("GAS- timestamp: ");
    Serial.print(tGAS);
    Serial.print("; GAS- Meterstand (l): ");
    Serial.println(mGAS);
  }

  return validCRCFound;
}

void readTelegram() {
  if (mySerial.available()) {
    memset(telegram, 0, sizeof(telegram));
    while (mySerial.available()) {
      int len = mySerial.readBytesUntil('\n', telegram, MAXLINELENGTH);
      telegram[len] = '\n';
      telegram[len+1] = 0;
      yield();
      if( decodeTelegram(len+1) ) {
        if (millis() - lastUpdate >= updateInterval) {
          UpdateElectricity();
          UpdateGas();
          UpdateEmoncms();
        }
      }
    } 
  }
}



void loop() {
  readTelegram();
  ArduinoOTA.handle();
}



