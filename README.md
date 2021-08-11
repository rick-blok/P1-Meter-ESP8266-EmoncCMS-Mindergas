# P1-Meter-ESP8266
Software for the ESP2866 that sends P1 smart meter data to EmonCMS and Mindergas (with CRC checking and OTA firmware updates)

### Installation instrucions
- Make sure that your ESP8266 can be flashed from the Arduino environnment: https://github.com/esp8266/Arduino
- Place all files from this repository in a directory. Open the .ino file.
- Adjust WIFI, Domoticz and debug settings at the top of the file
- Compile and flash

### Connection of the P1 meter to the ESP8266
You need to connect the smart meter with a RJ12 connector. This is the pinout to use
![RJ12 P1 connetor](https://github.com/rick-blok/P1-Meter-ESP8266/blob/master/RJ12.png)

Connect GND->GND on ESP, RTS->3.3V on ESP and RxD->D7. Some meters require a pullup (~1kOhm) on RxD.
