#include <WiFiManager.h>
#include <PubSubClient.h>
#include <IRremote.hpp>
#include <ArduinoJson.h>
#include <time.h>

#ifdef ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#endif

#include "LedBriloner.h"
#include "Log.h"

// ==== Definitions ====
#define SERIAL_BAUDRATE 115200
#define LED_BUILTIN 2
#define IR_PIN 23
#define MQTT_SERVER "pi-desktop"
#define MQTT_PORT 1883
#define uS_TO_S_FACTOR 1000000        // Conversion factor for microseconds
#define TIME_TO_SLEEP 5               // 5 seconds sleep
#define IDLE_TIME_BEFORE_SLEEP 60000  // 60 seconds without messages before sleeping (in ms)

#define RESTART_INTERVAL (30 * 60 * 1000UL)  // 30 minutes in ms

#define TYPE_FAN "fan"
#define TYPE_LED "led"
#define TYPE_TV "tv"

#define ID_LED_TV "Led TV"
#define ID_LED_BED "Led Cama"
#define ID_LIVING_ROOM_LIGHT "Luz Sala"

const char* LOCALE = "sala";
const char* ID = "002";

// ================= OTA CONFIG =================
#define OTA_BASE_URL "http://pi-desktop:9595"
#define CURRENT_VERSION "20260602_214237"

// ==== Global Variables ====
WiFiClient espClient;
PubSubClient mqttClient(espClient);

String lastPayload = "";
unsigned long lastMessageTime = 0;
unsigned long lastProcessedMessageTime = 0;
bool messagePending = false;

unsigned long bootTime = 0;

unsigned long lastOTACheck = 0;
const unsigned long OTA_INTERVAL = 30000;

// ==== Function Prototypes ====
void connectToMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleMQTTMessage(const String& payload);
void publishLastMessage(const String& payload);
void controlLedByTime(int desiredState);
void checkOTA();

// ==== Setup ====
void setup() {
  Log::begin(SERIAL_BAUDRATE, &mqttClient, "logs/" + String(LOCALE) + "/" + ID);
  Log::println();
  Log::println();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  IrSender.begin(IR_PIN);

  WiFiManager wm;
  wm.setTimeout(15);
  wm.setConfigPortalTimeout(20);
  if (!wm.autoConnect("ESP-Config", "senha123")) {
    Log::println("Failed to connect, restarting...");
    delay(3000);
    ESP.restart();
  }

  Log::println("WiFi connected!");
  Log::print("IP: ");
  Log::println(WiFi.localIP());

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // Setup NTP with Germany timezone
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  bootTime = millis();

  connectToMQTT();
}

// ==== Loop ====
void loop() {
  if (WiFi.isConnected()) {
    controlLedByTime(HIGH);  // normally ON when WiFi is connected
  } else {
    controlLedByTime(LOW);  // normally OFF when WiFi is disconnected
    ESP.restart();
  }

  if (!mqttClient.connected()) {
    connectToMQTT();
  }

  mqttClient.loop();

  // Restart every RESTART_INTERVAL
  if (millis() - bootTime >= RESTART_INTERVAL) {
    Log::println("Restart interval reached, restarting...");
    ESP.restart();
  }

  // Debounce burst messages and process only the last one
  if (messagePending && (millis() - lastMessageTime > 200)) {
    controlLedByTime(HIGH);  // LED indicator ON during processing

    if (lastProcessedMessageTime != 0) {
      handleMQTTMessage(lastPayload);
    } else {
      Log::println("Ignoring messages until initial state is loaded.");
    }

    controlLedByTime(LOW);  // LED indicator OFF after processing
    messagePending = false;
  }
  
  // ===== OTA CHECK =====
  if (millis() - lastOTACheck > OTA_INTERVAL) {
    lastOTACheck = millis();
    checkOTA();
  }
}

// ==== Connect to MQTT ====
void connectToMQTT() {
  while (!mqttClient.connected()) {
    Log::print("Connecting to MQTT... ");
    if (mqttClient.connect(ID)) {
      Log::println("Connected! Subscribed to: ");
      Log::println((String("alexa/") + LOCALE + "/" + ID).c_str());
      Log::println((String("alexa/") + LOCALE + "/all").c_str());
      Log::println("alexa/all");
      Log::println((String("device/") + LOCALE + "/" + ID).c_str());

      mqttClient.subscribe((String("alexa/") + LOCALE + "/" + ID).c_str());
      mqttClient.subscribe((String("alexa/") + LOCALE + "/all").c_str());
      mqttClient.subscribe("alexa/all");
      mqttClient.subscribe((String("device/") + LOCALE + "/" + ID).c_str());
    } else {
      Log::print("Failed, rc=");
      Log::print(mqttClient.state());
      Log::println(". Retrying in 3 seconds...");
      delay(3000);
    }
  }
}

// ==== MQTT Callback ====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  String topicStr = String(topic);

  // If it's the state topic and no state is yet loaded, restore it
  if (topicStr == (String("device/") + LOCALE + "/" + ID) && lastProcessedMessageTime == 0) {
    Log::println("Restoring last state from retained message...");

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      Log::println("Failed to parse JSON");
      return;
    }

    if (!doc.containsKey("createdAt")) {
      Log::println("Missing 'createdAt' field");
      return;
    }

    lastMessageTime = doc["createdAt"];
    lastProcessedMessageTime = lastMessageTime;
    return;
  }

  // Store last received payload
  lastPayload = message;
  lastMessageTime = millis();
  messagePending = true;
  Log::print("Received message: ");
  Log::println(message);
}

// ==== Handle MQTT Payload ====
void handleMQTTMessage(const String& payload) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Log::println("Failed to parse JSON");
    return;
  }

  if (!doc.containsKey("createdAt")) {
    Log::println("Missing 'createdAt' field");
    return;
  }

  long createdAt = doc["createdAt"];
  time_t now;
  time(&now);

  // Ignore expired messages
  if ((now - createdAt) > 30) {
    Log::println("Received expired message. Ignoring.");
    return;
  }

  // Ignore duplicated messages
  if (lastProcessedMessageTime == createdAt) {
    Log::println("Received duplicated message. Ignoring.");
    return;
  }

  lastProcessedMessageTime = createdAt;

  // Process valid message
  Log::print("Processing message: ");
  Log::println(payload);

  const char* device = doc["deviceName"];
  const char* type = doc["type"];

  if (strcmp(type, TYPE_LED) == 0) {
    bool state = doc["state"];
    JsonArray rgb = doc["rgb_color"];
    int red = rgb[0];
    int green = rgb[1];
    int blue = rgb[2];
    int value = doc["brightness_pct"];

    if (strcmp(device, ID_LIVING_ROOM_LIGHT) == 0) {
      LedBriloner::triggerLight(state, red, green, blue, value);
    }
  }

  // Publish last processed message to state topic
  publishLastMessage(payload);
}

// ==== Publish last processed message ====
void publishLastMessage(const String& payload) {
  mqttClient.publish((String("device/") + LOCALE + "/" + ID).c_str(), payload.c_str(), true);
}

// ==== LED Time Control ====
void controlLedByTime(int desiredState) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    digitalWrite(LED_BUILTIN, desiredState);  // fallback if no time available
    return;
  }
  int hour = timeinfo.tm_hour;

  // Quiet hours: LED always OFF between 22h - 9h
  if (hour >= 22 || hour < 9) {
    digitalWrite(LED_BUILTIN, HIGH);  // LED off
  } else {
    digitalWrite(LED_BUILTIN, desiredState);
  }
}

// ================= OTA CHECK =================
void checkOTA() {
  if (!WiFi.isConnected()) return;

  HTTPClient http;

#ifdef ESP8266
  String url = String(OTA_BASE_URL) + "/esp8266/" + String(LOCALE) + "/" + String(ID) + "/latest.json";
  http.begin(client, url);
#else
  String url = String(OTA_BASE_URL) + "/esp32/" + String(LOCALE) + "/" + String(ID) + "/latest.json";
  http.begin(url);
#endif

  int code = http.GET();

  if (code != 200) {
    Serial.println("OTA: failed to fetch latest.json");
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload)) {
    Serial.println("OTA: JSON error");
    return;
  }

  String version = doc["version"];
  String file = doc["file"];

  Serial.println("Server version: " + version);

  if (version == CURRENT_VERSION) {
    Serial.println("OTA: already up to date");
    return;
  }

  Serial.println("OTA: updating firmware...");

#ifdef ESP8266
  String fwURL = String(OTA_BASE_URL) + "/esp8266/" + String(LOCALE) + "/" + String(ID) + "/" + file;
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, fwURL);
#else
  String fwURL = String(OTA_BASE_URL) + "/esp32/" + String(LOCALE) + "/" + String(ID) + "/" + file;
  t_httpUpdate_return ret = httpUpdate.update(client, fwURL);
#endif

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.println("OTA FAILED");
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("OTA NO UPDATE");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("OTA SUCCESS");
      break;
  }
}