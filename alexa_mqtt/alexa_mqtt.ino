#include <WiFiManager.h>
#include <PubSubClient.h>
#include <IRsend.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ir_Samsung.h>

#ifdef ESP32
#include <IRRemoteESP32.h>
#include <WiFi.h>
#else
#include <IRremoteESP8266.h>
#include <ESP8266WiFi.h>
#endif

#include "BluLed.h"
#include "LedTv.h"
#include "TvSamsung.h"

// ==== Definitions ====
#define SERIAL_BAUDRATE 115200
#define LED_BUILTIN 2
#define IR_PIN 23
#define MQTT_SERVER "pi-desktop"
#define MQTT_PORT 1883

#define TYPE_FAN "fan"
#define TYPE_LED "led"
#define TYPE_TV "tv"

#define ID_LED_TV "Led TV"
#define ID_LED_BED "Led Cama"

const char* LOCALE = "quarto";
const char* ID = "001";

// ==== Global Variables ====
WiFiClient espClient;
PubSubClient mqttClient(espClient);
IRsend irsend(IR_PIN);

String lastPayload = "";
unsigned long lastMessageTime = 0;
unsigned long lastProcessedMessageTime = 0;
bool messagePending = false;


// ==== Function Prototypes ====
void connectToMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleMQTTMessage(const String& payload);

// ==== Setup ====
void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();
  Serial.println();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  if (strcmp("quarto", LOCALE) == 0) {
    BluLed::setup();
  }

  irsend.begin();

  WiFiManager wm;
  wm.setConfigPortalTimeout(20);
  if (!wm.autoConnect("ESP-Config", "senha123")) {
    Serial.println("Failed to connect, restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("WiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // Set up NTP time
  connectToMQTT();
}

// ==== Loop ====
void loop() {
  if (WiFi.isConnected()) {
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
    ESP.restart();
  }

  if (!mqttClient.connected()) {
    connectToMQTT();
  }

  mqttClient.loop();

  // Wait 200ms after a burst to process only the last message
  if (messagePending && (millis() - lastMessageTime > 200)) {
    digitalWrite(LED_BUILTIN, HIGH);

    handleMQTTMessage(lastPayload);
    digitalWrite(LED_BUILTIN, LOW);
    messagePending = false;
  }
}

// ==== Connect to MQTT ====
void connectToMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT... ");
    if (mqttClient.connect(ID)) {
      Serial.println("Connected! Subscripbed to: ");
      Serial.println((String("alexa/") + LOCALE + "/" + ID).c_str());
      Serial.println((String("alexa/") + LOCALE + "/all").c_str());
      Serial.println("alexa/all");

      mqttClient.subscribe((String("alexa/") + LOCALE + "/" + ID).c_str());
      mqttClient.subscribe((String("alexa/") + LOCALE + "/all").c_str());
      mqttClient.subscribe("alexa/all");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(". Retrying in 3 seconds...");
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

  // Store the last received payload and time
  lastPayload = message;
  lastMessageTime = millis();
  messagePending = true;
  Serial.print("Received message: ");
  Serial.println(message);
}

// ==== Handle MQTT Payload ====
void handleMQTTMessage(const String& payload) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.println("Failed to parse JSON");
    return;
  }

  if (!doc.containsKey("createdAt")) {
    Serial.println("Missing 'createdAt' field");
    return;
  }

  long createdAt = doc["createdAt"];
  time_t now;
  time(&now);

  // Discard if message is older than 10 seconds
  if ((now - createdAt) > 10) {
    Serial.println("Received expired message. Ignoring.");
    return;
  }
  
  if (lastProcessedMessageTime == createdAt) {
    Serial.println("Received duplicated message. Ignoring.");
    return;
  }

  lastProcessedMessageTime = createdAt;

  // === Process the final valid message ===
  Serial.print("Processing message: ");
  Serial.println(payload);

  const char* device = doc["deviceName"];
  const char* type = doc["type"];

  if (strcmp(type, TYPE_LED) == 0) {
    bool state = doc["state"];
    JsonArray rgb = doc["rgb_color"];
    int red = rgb[0];
    int green = rgb[1];
    int blue = rgb[2];
    int value = doc["brightness_pct"];

    if (strcmp(device, ID_LED_BED) == 0) {
      BluLed::sendRGB(state, red, green, blue, value);
    } else if (strcmp(device, ID_LED_TV) == 0) {
      LedTv::triggerLedTv(state, red, green, blue);
    }
  } else if (strcmp(type, TYPE_TV) == 0) {
    const char* value = doc["value"];
    const char* action = doc["action"];
    TvSamsung::triggerControl(action, value);
  } else if (strcmp(type, TYPE_FAN) == 0) {
  }
}