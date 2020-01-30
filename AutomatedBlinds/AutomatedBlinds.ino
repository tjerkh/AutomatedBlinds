
#include <OneButton.h>
#include <AccelStepper.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
//#include <PubSubClient.h>
//#include <WiFiUdp.h>
#include <WiFiManager.h>
//#include <WiFiClient.h>
#include <ArduinoJson.h>
//#include "FS.h"
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>


#define MotorInterfaceType 8
// Initialize with pin sequence IN1-IN3-IN2-IN4 for using the AccelStepper library with 28BYJ-48 stepper motor:
AccelStepper stepper = AccelStepper(MotorInterfaceType, D1, D3, D2, D4);

// Setup a new OneButton.  
OneButton buttonUp(D5, true);
OneButton buttonDown(D6, true);

// Version number for checking if there are new code releases and notifying the user
String version = "0.1";

//Configure Default Settings for AP logon
String APid = "AutoConnectAP";
String APpw = "StudioPieters";

//Fixed settings for WIFI
String mqttclientid;         //Generated MQTT client id
String outputTopic;               //MQTT topic for sending messages
String inputTopic;                //MQTT topic for listening
//char config_name[40];             //WIFI config: Bonjour name of device
//char mqtt_server[40];             //WIFI config: MQTT server config (optional)
//char mqtt_port[6] = "1883";       //WIFI config: MQTT port config (optional)

struct Config {
  long maxPosition;
  char config_name[40];             //WIFI config: Bonjour name of device
  char mqtt_server[40];             //WIFI config: MQTT server config (optional)
  char mqtt_port[6] = "1883";       //WIFI config: MQTT port config (optional)
  char mqtt_uid[40];             //WIFI config: MQTT server username (optional)
  char mqtt_pwd[40];             //WIFI config: MQTT server password (optional)
};

const char *filename = "/config2.json";
Config config;                         // <- global configuration object

long maxPosition = 2000000;         //Max position of the blind. Initial value
bool shouldSaveConfig = false;      //Used for WIFI Manager callback to save parameters

ESP8266WebServer server(80);              // TCP server at port 80 will respond to HTTP requests
WebSocketsServer webSocket = WebSocketsServer(81);  // WebSockets will respond on port 81

unsigned long startMillis;
unsigned long stopMillis;

void loadConfiguration(const char *filename, Config &config) {
  File file = SPIFFS.open(filename, "r");

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<512> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);

  if (error)
    Serial.println(F("Failed to read file, using default configuration"));

  // Copy values from the JsonDocument to the Config
  config.maxPosition = doc["maxPosition"] | 2000000;
  strlcpy(config.config_name, doc["config_name"] | "", sizeof(config.config_name));
  strlcpy(config.mqtt_server, doc["mqtt_server"] | "", sizeof(config.mqtt_server));
  strlcpy(config.mqtt_port, doc["mqtt_port"] | "1883", sizeof(config.mqtt_server));
  strlcpy(config.mqtt_uid, doc["mqtt_port"] | "", sizeof(config.mqtt_uid));

  Serial.println((String)"maxPosition: " + config.maxPosition);
  Serial.println((String)"config_name: " + config.config_name);
  Serial.println((String)"mqtt_server: " + config.mqtt_server);
  Serial.println((String)"mqtt_port: " + config.mqtt_port);

  file.close();
}

// Saves the configuration to a file
void saveConfiguration(const char *filename, const Config &config) {
  // Delete existing file, otherwise the configuration is appended to the file
  SPIFFS.remove(filename);

  // Open file for writing
  File file = SPIFFS.open(filename, "w");
  if (!file) {
    Serial.println(F("Failed to create file"));
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<256> doc;

}

/****************************************************************************************
   Loading configuration that has been saved on SPIFFS.
   Returns false if not successful
*/
bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonDocument<200> jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, buf.get());
  Serial.println("Debug P");

   // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }

  serializeJson(jsonDoc, Serial);
  Serial.println();

  Serial.println("Debug R");

  //Store variables locally
  maxPosition = long(jsonDoc["maxPosition"]);
  Serial.println("Debug S");
  //strcpy(config_name, jsonDoc["config_name"]);
  Serial.println("Debug T");
  //strcpy(mqtt_server, jsonDoc["mqtt_server"]);
  //strcpy(mqtt_port, jsonDoc["mqtt_port"]);

  Serial.println("Debug U");

  return true;
}

/**
   Save configuration data to a JSON file
   on SPIFFS
*/
bool saveConfig() {
  StaticJsonDocument<200> jsonDoc;
  JsonObject json = jsonDoc.as<JsonObject>();
  json["maxPosition"] = maxPosition;
  json["config_name"] = config.config_name;
  json["mqtt_server"] = config.mqtt_server;
  json["mqtt_port"] = config.mqtt_port;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  serializeJson(json, configFile);

  Serial.println("Saved JSON to SPIFFS");
  serializeJson(json, Serial);
  Serial.println("A");
  Serial.println();
  return true;
}

/*
   Common function to get a topic based on the chipid. Useful if flashing
   more than one device
*/
String getMqttTopic(String type) {
  return "/raw/esp8266/" + String(ESP.getChipId()) + "/" + type;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\n", num, payload);

      String res = (char*)payload;

      //Send to common MQTT and websocket function
      //processMsg(res, num);
      break;
  }
}

/*
   Callback from WIFI Manager for saving configuration
*/
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(9600);
  delay(100);
  Serial.println("Starting ...");

  stepper.setMaxSpeed(1000);
  stepper.setSpeed(1000);  
  stepper.setAcceleration(1000.0);

  buttonUp.attachClick(clickButtonUp);
  buttonUp.attachDoubleClick(doubleClickButtonUp);
  buttonUp.attachLongPressStart(startLongPressButtonUp);
  buttonUp.attachLongPressStop(stopLongPressButtonUp);

  buttonDown.attachClick(clickButtonDown);
  buttonDown.attachDoubleClick(doubleClickButtonDown);
  buttonDown.attachLongPressStart(startLongPressButtonDown);
  buttonDown.attachLongPressStop(stopLongPressButtonDown);
  
  //Set MQTT properties
  outputTopic = getMqttTopic("out");
  inputTopic = getMqttTopic("in");
  mqttclientid = ("ESPClient-" + String(ESP.getChipId()));
  
  Serial.println("Sending to Mqtt-topic: " + outputTopic);
  Serial.println("Listening to Mqtt-topic: " + inputTopic);
  Serial.println("MQTT Client ID: " + String(mqttclientid));

  //Set the WIFI hostname
  WiFi.hostname(config.config_name);

  //Define customer parameters for WIFI Manager
  WiFiManagerParameter custom_config_name("Name", "Bonjour name", config.config_name, 40);
  WiFiManagerParameter custom_mqtt_server("server", "MQTT server (optional)", config.mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT port", config.mqtt_port, 6);

  //Setup WIFI Manager
  WiFiManager wifiManager;

  //reset settings - for testing
  //wifiManager.resetSettings();

  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_config_name);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.autoConnect(APid.c_str(), APpw.c_str());

  Serial.println((String)"config_name: " + custom_config_name.getValue());
  Serial.println((String)"mqtt_server: " + custom_mqtt_server.getValue());

  Serial.println("Debug B");

  //Load config upon start
  if (!SPIFFS.begin()) {
    Serial.println("Debug C");
    Serial.println("Failed to mount file system");
    return;
  }

  Serial.println("Debug D");

  if (shouldSaveConfig) {
    Serial.println("Debug E");
    //read updated parameters
    strcpy(config.config_name, custom_config_name.getValue());
    strcpy(config.mqtt_server, custom_mqtt_server.getValue());
    strcpy(config.mqtt_port, custom_mqtt_port.getValue());

    //Save the data
    saveConfig();
  }

  Serial.println("Debug F");

  // Should load default config if run for the first time
  Serial.println(F("Loading configuration..."));
  loadConfiguration(filename, config);

  /*
     Try to load FS data configuration every time when
     booting up. If loading does not work, set the default
     positions
  */
  if (!loadConfig()){
    Serial.println("Debug G");
    maxPosition = 2000000;
  }

  Serial.println("Debug H");

  /*
    Setup multi DNS (Bonjour)
  */
  if (!MDNS.begin(config.config_name)) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }

  Serial.println("mDNS responder started");

  // Start TCP (HTTP) server
  server.begin();
  Serial.println("TCP server started");

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  //Start websocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop() {

  //Websocket listner
  webSocket.loop();

   /**
    Serving the webpage
  */
  server.handleClient();

  //yield();
  
  buttonUp.tick();
  buttonDown.tick();

  if (stepper.distanceToGo() == 0){
    //stopMillis = millis();
    //unsigned long rpm = (2*2038)/((stopMillis - startMillis) /60000);
    //Serial.println((String)"RPM: " + rpm);
  }

  stepper.run();

  //serveWebpage();
}



void stop() {
  long currentPosition = stepper.currentPosition();
  Serial.println((String)"Current Position: " + currentPosition);

  stepper.stop();
  stepper.runToPosition();
}

void moveUp() {
  long currentPosition = stepper.currentPosition();
  Serial.println((String)"Current Position: " + currentPosition);

  stepper.moveTo(0);
}

void moveDown() {
  long currentPosition = stepper.currentPosition();
  Serial.println((String)"Current Position: " + currentPosition);

  startMillis = millis();
  stepper.moveTo(maxPosition);
  //yield();
  //runToPosition();
  //yield();
  stopMillis = millis();
  
  unsigned long rpm = (2*2038)/((stopMillis - startMillis) /60000);
  Serial.println((String)"RPM: " + rpm);
}

// This function will be called once, when the buttonDown is cliked.
void clickButtonUp() {
  Serial.println("Button Up click");
  stop();
}

// This function will be called once, when the buttonUp is double cliked.
void doubleClickButtonUp() {
  Serial.println("Button Up double click");
  moveUp();
}

// This function will be called once, when the buttonUp is pressed for a long time.
void startLongPressButtonUp() {
  Serial.println("Button Up longPress start");
  moveUp();
}

// This function will be called once, when the button1 is released after beeing pressed for a long time.
void stopLongPressButtonUp() {
  Serial.println("Button Up longPress stop");
  stop();
}

// This function will be called once, when the buttonDown is cliked.
void clickButtonDown() {
  Serial.println("Button Down click");
  stop();
}

// This function will be called once, when the buttonDown is double cliked.
void doubleClickButtonDown() {
  Serial.println("Button Down double click");
  moveDown();
}

// This function will be called once, when the buttonDown is pressed for a long time.
void startLongPressButtonDown() {
  moveDown();
}

// This function will be called once, when the buttonDown is released after beeing pressed for a long time.
void stopLongPressButtonDown() {
  Serial.println("Button Down longPress stop");
  stop();
}
