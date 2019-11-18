#include <Arduino.h>
#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <DNSServer.h>
#include "WiFiManager/WiFiManager.h"          //https://github.com/tzapu/WiFiManager

// Go to Tools>Flash Size and select and option with SPIFFS
#include <FS.h>   // Include the SPIFFS library https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html

#include "gamma.h"

const String MDNS_HOSTNAME = "esp8266"; //TODO: use eeprom/flash

// LED Pins
const uint8_t PIN_BLUE_CH = 16; // D0
const uint8_t PIN_RED_CH = 5;   // D1
const uint8_t PIN_GREEN_CH = 4; // D2

uint8_t rValue = 64;
uint8_t gValue = 64;
uint8_t bValue = 64;

String mimes(String filename); // convert the file extension to the MIME type
void handlerSetRgb();
void handlerStaticFile(String path);
void setLed(uint8_t r, uint8_t g, uint8_t b);

ESP8266WebServer server(80);

bool ledState = true;
void ledOn() {
  ledState = true;
  digitalWrite(LED_BUILTIN, !ledState);
}

void ledOff() {
  ledState = false;
  digitalWrite(LED_BUILTIN, !ledState);
}

void ledToggle() {
  ledState = !ledState;
  digitalWrite(LED_BUILTIN, !ledState);
}


String mimes(String filename) {
  String ext = filename.substring(filename.lastIndexOf(".") + 1);
  if(ext=="gz") return "application/x-gzip";
  if(ext=="htm") return "text/html";
  if(ext=="html") return "text/html";
  if(ext=="js") return "application/javascript";
  if(ext=="css") return "text/css";
  if(ext=="png") return "image/png";
  if(ext=="gif") return "image/gif";
  if(ext=="jpg") return "image/jpeg";
  if(ext=="ico") return "image/x-icon";
  if(ext=="xml") return "text/xml";
  if(ext=="pdf") return "application/x-pdf";
  if(ext=="zip") return "application/x-zip";
  return "text/plain";
}

// Serve static files located in the SPIFFS (flash file system)
void handlerStaticFile(String reqPath) {
  Serial.println("handlerStaticFile: " + reqPath);

  if (reqPath.endsWith("/")) reqPath += "index.html"; // default folder resource

  String resourceFile = reqPath + ".gz";
  bool fileExist = SPIFFS.exists(resourceFile);
  if(!fileExist){
    resourceFile = reqPath;
    fileExist = SPIFFS.exists(resourceFile);
  }

  if (fileExist) {
    File file = SPIFFS.open(resourceFile, "r");
    const String contentType = mimes(reqPath); 
    size_t sent = server.streamFile(file, contentType);
    file.close(); 
    Serial.println(String("Sent: ") + reqPath);
  }
  else {
    Serial.println(String("File Not Found: ") + reqPath);
    server.send(404, "text/plain", "[404] Not Found");
  }
}


void handlerSetRgb() {
  
  uint8_t r = server.arg("r").toInt();
  uint8_t g = server.arg("g").toInt();
  uint8_t b = server.arg("b").toInt();

  setLed(r, g, b);
  server.send(204);
}

void handlerGetRgb() {
  String resp = "{\"r\":"+String(rValue)+",\"g\":"+String(gValue)+",\"b\":"+String(bValue)+"}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", resp);
}

void setLed(uint8_t red, uint8_t green, uint8_t blue)
{
  rValue = red;
  gValue = green;
  bValue = blue;

  // map 8bit value to 10bit PWM
  uint16_t rPwm = (gamma_table[red]);
  uint16_t gPwm = (gamma_table[green]);
  uint16_t bPwm = (gamma_table[blue]);

  // calibration
  rPwm = rPwm * 0.299;
  gPwm = gPwm * 0.587;
  bPwm = bPwm * 0.114;

  analogWrite(PIN_RED_CH, rPwm);
  analogWrite(PIN_GREEN_CH, gPwm);
  analogWrite(PIN_BLUE_CH, bPwm);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_RED_CH, OUTPUT);
  pinMode(PIN_GREEN_CH, OUTPUT);
  pinMode(PIN_BLUE_CH, OUTPUT);

  setLed(16, 0, 0); // Power on color

  // put your setup code here, to run once:
  Serial.begin(115200);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset settings - for testing
  //wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(60 * 3);
  wifiManager.setConfigPortalTimeout(60 * 5);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration

  WiFiManagerParameter hostname("hostname", "Hostname.local", "esp8266", 32);
  wifiManager.addParameter(&hostname);

  String ssid = "SETUP " + String(ESP.getChipId());
  if (!wifiManager.autoConnect(ssid.c_str(), "12345678")) {
    Serial.println("failed to connect and hit timeout");   
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  ledOn();
  setLed(0, 0, 16);

  Serial.println("");
  Serial.print("Connected to: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  // Start the SPI Flash Files System
  SPIFFS.begin();

  // Http server
  server.on("/setRGB", handlerSetRgb);
  server.on("/getRGB", handlerGetRgb);

  server.onNotFound([]() {
    handlerStaticFile(server.uri());
  });

  // Start TCP (HTTP) server
  server.begin();
  Serial.println("HTTP server started");

  // Set up mDNS responder:
  // - first argument is the domain name, in this example
  //   the fully-qualified domain name is "esp8266.local"
  // - second argument is the IP address to advertise
  //   we send our IP address on the WiFi network
  if (MDNS.begin(MDNS_HOSTNAME)){ // Start the mDNS responder for esp8266.local
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  setLed(0, 16, 16);
}

void loop() {
  server.handleClient();
  MDNS.update();
}
