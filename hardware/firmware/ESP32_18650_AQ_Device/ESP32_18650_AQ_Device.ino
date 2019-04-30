#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>

#include "Adafruit_BME680.h"

#define SEALEVELPRESSURE_HPA (1013.25)
#define MOV_AVG_WDW 10
#define NDIR_TIMEOUT 1000
#define SDS011_TIMEOUT 2000
#define WIFI_TIMEOUT 10000
#define READ_INTERVAL 1000
#define SEND_INTERVAL 10000
#define WARMUP_INTERVAL 90000
#define RESET_PIN 15

long read_start = 0;
long send_start = 0;
bool warmup_over = false;
bool MDNS_STATUS = false;

HardwareSerial ndir(1);
HardwareSerial sds011(2);
Adafruit_BME680 bme;

WebServer server(80);

const char* ssid     = "Nyi Nyi Nyan Tun";
const char* password = "nyinyitun";

const String deviceId = "aq_sense01";
const char* host = "api.thingspeak.com";
const String apiKey = "WN53H3MB5O2KJDSO";

byte cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
byte co2res[7];
byte ppmres[8];

float hum_weighting = 0.25; // so hum effect is 25% of the total air quality score
float gas_weighting = 0.75; // so gas effect is 75% of the total air quality score

float hum_score, gas_score;
float gas_reference = 250000;
float hum_reference = 40;
int getgasreference_count = 0;

int co2[MOV_AVG_WDW];
int pm25[MOV_AVG_WDW];
int pm10[MOV_AVG_WDW];
int hum[MOV_AVG_WDW];
int temp[MOV_AVG_WDW];
int pres[MOV_AVG_WDW];
int iaq[MOV_AVG_WDW];
float vbat[MOV_AVG_WDW];

void popArray(float arr[], int n) {
  for (int i = 0; i < n; i++) {
    arr[i] = 0.0;
  }
}

void popArray(int arr[], int n) {
  for (int i = 0; i < n; i++) {
    arr[i] = 0;
  }
}

void pushArray(float val, float arr[], int n) {
  for (int i = 0; i < n - 1; i++) {
    arr[i] = arr[i + 1];
  }
  arr[n - 1] = val;
}

void pushArray(int val, int arr[], int n) {
  for (int i = 0; i < n - 1; i++) {
    arr[i] = arr[i + 1];
  }
  arr[n - 1] = val;
}

float getAvg(int arr[], int n) {
  float avg = 0;
  for (int i = 0; i < n; i++) {
    avg += arr[i];
  }
  return (avg / n);
}

float getAvg(float arr[], int n) {
  float avg = 0;
  for (int i = 0; i < n; i++) {
    avg += arr[i];
  }
  return (avg / n);
}

float getBattery() {
  return (analogRead(32) * 0.00080586 * 2);
}

void GetGasReference() {
  // Now run the sensor for a burn-in period, then use combination of relative humidity and gas resistance to estimate indoor air quality as a percentage.
  Serial.println("Getting a new gas reference value");
  int readings = 10;
  for (int i = 1; i <= readings; i++) { // read gas for 10 x 0.150mS = 1.5secs
    gas_reference += bme.readGas();
  }
  gas_reference = gas_reference / readings;
}

String CalculateIAQ(float score) {
  String IAQ_text = "Air quality is ";
  score = (100 - score) * 5;
  if      (score >= 301)                  IAQ_text += "Hazardous";
  else if (score >= 201 && score <= 300 ) IAQ_text += "Very Unhealthy";
  else if (score >= 176 && score <= 200 ) IAQ_text += "Unhealthy";
  else if (score >= 151 && score <= 175 ) IAQ_text += "Unhealthy for Sensitive Groups";
  else if (score >=  51 && score <= 150 ) IAQ_text += "Moderate";
  else if (score >=  00 && score <=  50 ) IAQ_text += "Good";
  return IAQ_text;
}

void handleRoot() {
  server.send(200, "text/plain", "hello from esp32!");
}

void espReboot(){
  Serial.println("Restarting ESP...");
  ESP.restart();
}

bool checkWifi() {
  Serial.println("[Wifi]");
  Serial.println("==========");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Status: Disconnected");
    Serial.print("Connecting SSID: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    long wifi_start = millis();
    bool timeout = false;
    while ((WiFi.status() != WL_CONNECTED) && (!timeout)) {
      delay(500);
      Serial.print("=");
      if ((millis() - wifi_start) > WIFI_TIMEOUT) timeout = true;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Status: Connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      if (MDNS_STATUS) {
        Serial.print("MDNS: Resetting");
        MDNS.end();
        MDNS_STATUS = false;
      }
      if (!MDNS.begin("esp32")) {
        Serial.println("MDNS: Error Setting Up");
      } else {
        Serial.println("MDNS: Listening on esp32.local");
        MDNS_STATUS = true;
      }
      Serial.println();
      return true;
    } else if (timeout) {
      Serial.println("Status: Connection Timeout");
      if (MDNS_STATUS) {
        Serial.print("MDNS: Resetting");
        MDNS.end();
        MDNS_STATUS = false;
      }
      Serial.println();
      return false;
    }
  } else if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Status: Connected");
    if (!MDNS_STATUS) {
      if (!MDNS.begin("esp32")) {
        Serial.println("MDNS: Error Setting Up");
      } else {
        Serial.println("MDNS: Listening on esp32.local");
        MDNS_STATUS = true;
      }
      Serial.println();
    }
    return true;
  }
}

void setup() {
  Serial.begin(9600);
  sds011.begin(9600, SERIAL_8N1, 2, 4);
  ndir.begin(9600, SERIAL_8N1, 16, 17);

  if (!bme.begin()) {
    Serial.println("Could not find a valid BME680 sensor, check wiring!");
    while (1);
  }
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);

  popArray(co2, MOV_AVG_WDW);
  popArray(pm25, MOV_AVG_WDW);
  popArray(pm10, MOV_AVG_WDW);
  popArray(hum, MOV_AVG_WDW);
  popArray(temp, MOV_AVG_WDW);
  popArray(pres, MOV_AVG_WDW);
  popArray(iaq, MOV_AVG_WDW);
  popArray(vbat, MOV_AVG_WDW);

  read_start = millis();
  send_start = read_start;

  Serial.println();
  Serial.println("[WARM UP]");
  Serial.println("==========");
  Serial.println("Status: Warm Up Started");
  Serial.println();
  WiFi.disconnect();
  checkWifi();
  Serial.println("TCP server started");

  // Add service to MDNS-SD
  server.on("/", handleRoot);
  server.begin();
  delay(1000);
}

void loop() {
  if(digitalRead(RESET_PIN)){
    espReboot();
  }
  server.handleClient();
  if ((millis() - read_start) >= READ_INTERVAL) {
    Serial.println("**DEBUG**");
    Serial.println("[POWER]");
    Serial.println("==========");
    pushArray(getBattery(), vbat, MOV_AVG_WDW);
    Serial.print("Battery: ");
    Serial.print(getAvg(vbat, MOV_AVG_WDW));
    Serial.println("V");
    Serial.println();

    //Read BME680 Data
    Serial.println("[BME680]");
    Serial.println("==========");
    if (! bme.performReading()) {
      Serial.println("Failed to perform reading :(");
      pushArray(0, temp, MOV_AVG_WDW);
      pushArray(0, pres, MOV_AVG_WDW);
      pushArray(0, hum, MOV_AVG_WDW);
      pushArray(0, iaq, MOV_AVG_WDW);
    } else {
      pushArray(bme.temperature, temp, MOV_AVG_WDW);
      pushArray(bme.pressure, pres, MOV_AVG_WDW);
      pushArray(bme.humidity, hum, MOV_AVG_WDW);

      float current_humidity = bme.humidity;
      if (current_humidity >= 38 && current_humidity <= 42)
        hum_score = 0.25 * 100; // Humidity +/-5% around optimum
      else
      { //sub-optimal
        if (current_humidity < 38)
          hum_score = 0.25 / hum_reference * current_humidity * 100;
        else
        {
          hum_score = ((-0.25 / (100 - hum_reference) * current_humidity) + 0.416666) * 100;
        }
      }

      //Calculate gas contribution to IAQ index
      float gas_lower_limit = 5000;   // Bad air quality limit
      float gas_upper_limit = 50000;  // Good air quality limit
      if (gas_reference > gas_upper_limit) gas_reference = gas_upper_limit;
      if (gas_reference < gas_lower_limit) gas_reference = gas_lower_limit;
      gas_score = (0.75 / (gas_upper_limit - gas_lower_limit) * gas_reference - (gas_lower_limit * (0.75 / (gas_upper_limit - gas_lower_limit)))) * 100;

      //Combine results for the final IAQ index value (0-100% where 100% is good quality air)
      float air_quality_score = hum_score + gas_score;
      /*
        Serial.println("Air Quality = " + String(air_quality_score, 1) + "% derived from 25% of Humidity reading and 75% of Gas reading - 100% is good quality air");
        Serial.println("Humidity element was : " + String(hum_score / 100) + " of 0.25");
        Serial.println("     Gas element was : " + String(gas_score / 100) + " of 0.75");
        if (bme.readGas() < 120000) Serial.println("***** Poor air quality *****");
        Serial.println();
      */
      if ((getgasreference_count++) % 10 == 0) GetGasReference();
      /*
         Serial.println(CalculateIAQ(air_quality_score));
        Serial.println("------------------------------------------------");
        delay(2000);
      */
      pushArray(air_quality_score, iaq, MOV_AVG_WDW);
    }
    Serial.println();

    //Read NDIR Data
    Serial.println("[NDIR]");
    Serial.println("==========");
    memset(co2res, 0, 7);
    ndir.write(cmd, 9);
    long ndir_start = millis();
    bool timeout = false;
    bool ndir_avail = false;
    while ((!timeout) && (!ndir_avail)) {
      if ((millis() - ndir_start) >= NDIR_TIMEOUT) timeout = true;
      if (ndir.available()) ndir_avail = true;
    }
    if (ndir_avail) {
      while (ndir.available()) {
        if ((ndir.read() == 0xff) && (ndir.read() == 0x86)) {
          ndir.readBytes(co2res, 7);
          Serial.print("RAW BYTES: ");
          for (int i = 0; i < 7; i++) {
            Serial.print(co2res[i], HEX);
          }
          Serial.println();
          int co2buf = (co2res[0] * 256) + co2res[1];
          Serial.print("CO2: ");
          Serial.print(co2buf);
          Serial.println("PPM");
          pushArray(co2buf, co2, MOV_AVG_WDW);
        }
      }
    } else if (timeout) {
      pushArray(0, co2, MOV_AVG_WDW);
    }
    Serial.println();

    //Read SDS011 Data
    Serial.println("[SDS011]");
    Serial.println("==========");
    memset(ppmres, 0, 8);
    while (sds011.available()) {
      sds011.read();
    }
    long sds011_start = millis();
    timeout = false;
    bool sds011_avail = false;
    while ((!timeout) && (!sds011_avail)) {
      if ((millis() - sds011_start) >= SDS011_TIMEOUT) timeout = true;
      if (sds011.available()) sds011_avail = true;
    }
    if (sds011_avail) {
      while (sds011.available())  {
        byte f_b = sds011.read();
        if (f_b == 0xaa) {
          byte s_b = sds011.peek();
          if (s_b == 0xc0) {
            Serial.print("RAW BYTES: ");
            Serial.print(f_b, HEX);
            Serial.print(s_b, HEX);
            sds011.read();
            sds011.readBytes(ppmres, 8);
            for (int i = 0; i < 8; i++) {
              Serial.print(ppmres[i], HEX);
            }
            Serial.println();
            if (ppmres[7] == 0xab) {
              int pm25buf = (int(ppmres[1]) * 256 + int(ppmres[0])) / 10;
              int pm10buf = (int(ppmres[3]) * 256 + int(ppmres[2])) / 10;
              Serial.print("PM2.5: ");
              Serial.print(pm25buf);
              Serial.println("ug/m^3");
              Serial.print("PM10: ");
              Serial.print(pm10buf);
              Serial.println("ug/m^3");
              pushArray(pm25buf, pm25, MOV_AVG_WDW);
              pushArray(pm10buf, pm10, MOV_AVG_WDW);
            } else {
              pushArray(0, pm25, MOV_AVG_WDW);
              pushArray(0, pm10, MOV_AVG_WDW);
            }
          }
        } else {
        Serial.print("Invalid Start Byte: ");
        Serial.print(f_b, HEX);
      }
    }
  } else if (timeout) {
    pushArray(0, pm25, MOV_AVG_WDW);
    pushArray(0, pm10, MOV_AVG_WDW);
  }
  read_start = millis();
  Serial.println();
}

if (((millis() - send_start) >= SEND_INTERVAL) && (warmup_over)) {
  Serial.println("[INFORMATION]");
  Serial.println("==========");
  Serial.print("PM10: ");
  Serial.print(getAvg(pm10, MOV_AVG_WDW));
  Serial.println("ug/m^3");
  Serial.print("PM25: ");
  Serial.print(getAvg(pm25, MOV_AVG_WDW));
  Serial.println("ug/m^3");
  Serial.print("Temperature: ");
  Serial.print(getAvg(temp, MOV_AVG_WDW));
  Serial.println("*C");
  Serial.print("Pressure: ");
  Serial.print(getAvg(pres, MOV_AVG_WDW) / 100.0);
  Serial.println("hPa");
  Serial.print("Humidity: ");
  Serial.print(getAvg(hum, MOV_AVG_WDW));
  Serial.println("%");
  Serial.print("CO2: ");
  Serial.print(getAvg(co2, MOV_AVG_WDW));
  Serial.println("PPM");
  Serial.print("IAQ(VOC): ");
  Serial.print(getAvg(iaq, MOV_AVG_WDW));
  Serial.println("%");
  Serial.print("Battery: ");
  Serial.print(getAvg(vbat, MOV_AVG_WDW));
  Serial.println("V");
  Serial.println();
  if (checkWifi()) {
    sendData(pm25, pm10, co2, temp, pres, hum, iaq, vbat, MOV_AVG_WDW);
  }
  send_start = millis();
} else if (!warmup_over) {
  if (millis() > WARMUP_INTERVAL) {
    Serial.println("[WARM UP]");
    Serial.println("==========");
    Serial.println("Status: Warm Up Finished");
    Serial.println();
    warmup_over = true;
  }
}
}

void sendData(int pm25[], int pm10[], int co2[], int temp[], int pres[], int hum[], int iaq[], float bat[], int window) {
  Serial.println("[UPLOAD]");
  Serial.println("==========");

  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("Error: Connection Failed");
    return;
  }

  String url = "/update?api_key=" + apiKey +
               "&field1=" + String(getAvg(pm25, window)) +
               "&field2=" + String(getAvg(pm10, window)) +
               "&field3=" + String(getAvg(co2, window)) +
               "&field4=" + String(getAvg(temp, window)) +
               "&field5=" + String(getAvg(pres, window) / 100.0) +
               "&field6=" + String(getAvg(hum, window)) +
               "&field7=" + String(getAvg(iaq, window)) +
               "&field8=" + String(getAvg(bat, window));
               // + "&id=" + deviceId;

  Serial.print("Request URL: ");
  Serial.println(url);

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println("Error: Client Timeout !");
      client.stop();
      return;
    }
  }

  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  Serial.println();
  Serial.println("Status: Connection Closed");
  Serial.println();
}
