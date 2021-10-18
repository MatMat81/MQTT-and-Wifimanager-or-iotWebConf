 
#include <Arduino.h>
#include <IotWebConf.h>
#include <IotWebConfUsing.h> // This loads aliases for easier class names.
#ifndef ESP32
# include <LittleFS.h>
#else
# include <LITTLEFS.h>
# define LittleFS LITTLEFS
#endif
#include <ArduinoJson.h>

#ifndef IOTWEBCONF_ENABLE_JSON
# error platformio.ini must contain "build_flags = -DIOTWEBCONF_ENABLE_JSON"
#endif
#define JSON_FILE_MAX_SIZE 1024
#define CONFIG_FILE_NAME "config.json"

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#ifdef ESP8266
# include <ESP8266HTTPUpdateServer.h>
#elif defined(ESP32)
// For ESP32 IotWebConf provides a drop-in replacement for UpdateServer.
# include <IotWebConfESP32HTTPUpdateServer.h>
#endif


const char thingName[] = "yourThingName";
const char wifiInitialApPassword[] = "yourFirstPass";

#define CONFIG_VERSION "op4t" 
#define STATUS_PIN LED_BUILTIN

// io.adafruit.com SHA1 fingerprint
static const char *fingerprint PROGMEM = "yourFingerPrint look at WiFiClientSecure.h";

unsigned long previousMillis = 0;        // will store last time LED was updated
const long interval = 5000;           // interval at which to blink (milliseconds)
float fMessWertAktuell;
bool firstRun = false;
bool needReset = false;

#define STRING_LEN 128
#define NUMBER_LEN 32
char MqttServerValue[STRING_LEN];
char MqttUserValue[STRING_LEN];
char MqttKeyValue[STRING_LEN];
char MqttPortValue[NUMBER_LEN];

uint8_t CharToUint8(char Text);
void initProperties();
void loop();
void setup();
void handleRoot();
void wifiConnected();
byte MQTT_connect();
void readJsonConfigFile();
void configSaved();
bool formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper);

DNSServer dnsServer;
WebServer server(80);
#ifdef ESP8266
ESP8266HTTPUpdateServer httpUpdater;
#elif defined(ESP32)
HTTPUpdateServer httpUpdater;
#endif

WiFiClientSecure client;// WiFiFlientSecure for SSL/TLS support
//WiFiClient  client;//  

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameterGroup MqttGruppe = IotWebConfParameterGroup("mqtt", "Mqtt Einstellungen");
IotWebConfTextParameter MqttServer = IotWebConfTextParameter("Server param", "MqttServer", MqttServerValue, STRING_LEN);
IotWebConfNumberParameter MqttPort = IotWebConfNumberParameter("Port param", "MqttPort", MqttPortValue, NUMBER_LEN, "20", "1..9999", "min='1' max='9999' step='1'");
IotWebConfTextParameter MqttUser = IotWebConfTextParameter("User param", "MqttUser", MqttUserValue, STRING_LEN);
IotWebConfTextParameter MqttKey = IotWebConfTextParameter("Key param", "MqttKey", MqttKeyValue, STRING_LEN);

/*Setup MQTT und MQTT-Feeds *****************************/
//Adafruit_MQTT_Client mqtt(&client, MqttServerValue, MqttPortuInt16, MqttUserValue, MqttKeyValue);
// Setup a feed called 'test' for publishing. Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
//Adafruit_MQTT_Publish test = Adafruit_MQTT_Publish(&mqtt, MqttUrlFeedTest);
uint16_t MqttPortuInt16;
Adafruit_MQTT_Client *mqtt;
char MqttUrlFeedTest[100];
Adafruit_MQTT_Publish *test;

/*************************** Sketch Code ************************************/

void setup() {
  /* Initialize serial and wait up to 5 seconds for port to open */
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("Adafruit IO MQTTS (SSL/TLS) Example"));
  randomSeed(analogRead(0)); // für Zufallszahlen
  /* Configure LED pin as an output */
  pinMode(LED_BUILTIN, OUTPUT);

  MqttGruppe.addItem(&MqttServer);
  MqttGruppe.addItem(&MqttPort);
  MqttGruppe.addItem(&MqttUser);
  MqttGruppe.addItem(&MqttKey);
  iotWebConf.setStatusPin(STATUS_PIN);
  iotWebConf.addParameterGroup(&MqttGruppe);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);
   // -- Define how to handle updateServer calls.
  iotWebConf.setupUpdateServer(
    [](const char* updatePath) { httpUpdater.setup(&server, updatePath); },
    [](const char* userName, char* password) { httpUpdater.updateCredentials(userName, password); });
  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.getApTimeoutParameter()->visible = true;
  // -- Initializing the configuration.
  bool validConfig = iotWebConf.init();
  if (validConfig == true ){
    Serial.println(F("Konfig OK!"));
  }
  readJsonConfigFile();

  server.on("/", handleRoot);
  server.on("/config", []{ iotWebConf.handleConfig(); });
  server.onNotFound([](){ iotWebConf.handleNotFound(); });
  
  /****  MQTT   *****************************/
  // Zeiger auf Feed ändern und setzen:
  strcpy(MqttUrlFeedTest, MqttUserValue); //erster Teil
  strcat(MqttUrlFeedTest, "/feeds/test"); // füge Teil dazu
  // Zeiger auf Port Nr ändern und setzen:
  std::string strBuff = std::string(MqttPortValue); // Convert char*s to uint16_t
  MqttPortuInt16 = strtol(strBuff.c_str(), NULL, 10);
  /*Setup MQTT und MQTT-Feeds *****************************/
  mqtt = new Adafruit_MQTT_Client(&client, MqttServerValue, MqttPortuInt16, MqttUserValue, MqttKeyValue);
  test = new Adafruit_MQTT_Publish(mqtt, MqttUrlFeedTest);
  Serial.println("Ready.");
}

void loop() {
  iotWebConf.doLoop();
  unsigned long currentMillis = millis();
 
  if (needReset)
  {
    Serial.println("Rebooting after 1 second.");
    iotWebConf.delay(1000);
    ESP.restart();
  }
 
  if (iotWebConf.getState() == 4) {
    // online:
    if (firstRun == false){
      firstRun = true;
      // setze Fingerabdruck für SSL-Verbindung:
      client.setFingerprint(fingerprint);
    }
    if (MqttPortuInt16 > 0) {
      byte retMQTT = MQTT_connect();
      if (retMQTT == 2){
        // verbunden mit MQTT-Server: 
        // Serial.println((String) F("MQTT_connect: ") + retMQTT);
        if (currentMillis - previousMillis >= interval) {
          Serial.println("5 Sekunden:");
          // save the last time you blinked the LED
          previousMillis = currentMillis;     
          fMessWertAktuell = random(0, 2000);
          Serial.println(fMessWertAktuell);
          if (! test->publish(fMessWertAktuell)) {
            Serial.println(F("Failed"));
          } else {
            Serial.println(F("OK!"));
          }
        }
      } 
    } else {
       Serial.print("MqttPortuInt16 ist null! :>");
       Serial.println(MqttPortuInt16);
    }
  } else {
    firstRun = false;
  }
} 
  
void handleRoot() {
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal()) {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>IotWebConf 01 Minimal</title></head><body>";
  s += "<ul>";
  s += "<li>MqttServerValue: ";
  s += MqttServerValue;
  s += "<li>MqttPortValue: ";
  s += atoi(MqttPortValue);
  s += "<li>sizeof: atoi(MqttPortValue): ";
  s += sizeof(atoi(MqttPortValue));
  s += "<li>MqttPortValue: ";
  s += MqttPortValue;
  s += "<li>sizeof: MqttPortValue: ";
  s += sizeof(MqttPortValue);
  s += "<li>MqttUserValue: ";
  s += MqttUserValue;
  s += "<li>MqttKeyValue: ";
  s += MqttKeyValue;
  s += "</ul>";
  s += "Go to <a href='config'>configure page</a> to change settings.";
  s += "</body></html>\n";
  server.send(200, "text/html", s);
}

void wifiConnected() {
   Serial.print("Verbunden: ");
   Serial.println(iotWebConf.getState());
   iotWebConf.blink(5000, 95);
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
byte MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt->connected()) {
    return 2;
  }

  Serial.println("Connecting to MQTT... ");
  uint8_t retries = 3;
  while ((ret = mqtt->connect()) != 0) { // connect will return 0 for connected
       Serial.print((String) F("Versuch Nr: ") + retries + F(" MQTT-Fehler: "));
       Serial.println(mqtt->connectErrorString(ret));
       mqtt->disconnect();
       delay(50);  // wait 0,5 seconds
       retries--; // runterzählen
       if (retries == 0) {     
         //while (1); // löst Watchdog aus!!! -> basically die and wait for WDT to reset me
         return 3;
       }
  }
  Serial.println("MQTT Connected!");
  return 1;
}

uint8_t CharToUint8(char Text){
 //local ret = strtol(str, &ptr, 10);
  return 0;
}

void configSaved() { 
  Serial.println("Configuration was updated.");
  needReset = true;
}

bool formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper) {
  Serial.println("Validating form.");
  bool valid = true;

/*
  int l = webRequestWrapper->arg(stringParam.getId()).length();
  if (l < 3)
  {
    stringParam.errorMessage = "Please provide at least 3 characters for this test!";
    valid = false;
  }
*/
  return valid;
}

void readJsonConfigFile() {
  LittleFS.begin();
  File configFile = LittleFS.open(CONFIG_FILE_NAME, "r");
  if (configFile) {
    Serial.println(F("Reading config file"));
    StaticJsonDocument<JSON_FILE_MAX_SIZE> doc;

    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error)
    {
      Serial.println(F("Failed to read file, using default configuration"));
      return;
    }
    JsonObject documentRoot = doc.as<JsonObject>();

    // -- Apply JSON configuration.
    iotWebConf.getRootParameterGroup()->loadFromJson(documentRoot);
    iotWebConf.saveConfig();

    // -- Remove file after finished loading it.
    LittleFS.remove(CONFIG_FILE_NAME);
  }
  else
  {
    Serial.println(F("Config file not found, skipping."));
  }
  
  LittleFS.end();
}

