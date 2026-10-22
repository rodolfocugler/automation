/**
 * sala/002 - Briloner living room light (ESP32 / ESP8266)
 *
 * Listens for Alexa commands over MQTT and toggles the Briloner lamp via IR.
 * The last executed command is stored (retained) in device/<LOCALE>/<ID>, so a
 * command is never executed twice, even across restarts.
 *
 * Boot: WiFi (WiFiManager) -> NTP -> MQTT -> OTA check -> loop.
 * The board restarts every 30 min, so the OTA check at boot runs every 30 min.
 *
 * Logs go to Serial (115200) and to MQTT topic logs/<LOCALE>/<ID> (Grafana).
 * See README.md for details.
 */

// Keep IRremote from blinking the onboard LED (also saves flash).
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

#ifdef ESP32
  #include <HTTPClient.h>
  #include <HTTPUpdate.h>
  #define CHIP "esp32"
  #define IR_PIN 23
  #define LED_OFF LOW         // devkit onboard LED is active-high
  #define otaUpdater httpUpdate
#else
  #include <ESP8266HTTPClient.h>
  #include <ESP8266httpUpdate.h>
  #define CHIP "esp8266"
  #define IR_PIN 4            // D2; GPIO 2 is the onboard LED
  #define LED_OFF HIGH        // ESP-12 onboard LED is active-low
  #define otaUpdater ESPhttpUpdate
#endif

// ==== Device identity ====
#define LOCALE "sala"
#define ID "002"

// Injected by Jenkins: -DCURRENT_VERSION="\"<version>\"" (must match latest.json).
#ifndef CURRENT_VERSION
  #define CURRENT_VERSION "dev"
#endif

// ==== Config ====
#define ONBOARD_LED 2
#define MQTT_SERVER "pi-desktop"
#define MQTT_PORT 1883
#define MQTT_BUFFER 512
#define OTA_URL "http://pi-desktop:9595/" CHIP "/" LOCALE "/" ID "/"
#define RESTART_INTERVAL_MS (30UL * 60 * 1000)
#define MQTT_RETRY_MS 5000
#define MAX_COMMAND_AGE_S 30
#define VALID_EPOCH 1700000000   // anything below means NTP has not synced yet

// ==== Topics (compile-time strings, no heap) ====
#define TOPIC_STATE "device/" LOCALE "/" ID   // last executed command (retained)
#define TOPIC_LOGS "logs/" LOCALE "/" ID
const char* const COMMAND_TOPICS[] = {"alexa/" LOCALE "/" ID, "alexa/" LOCALE "/all", "alexa/all"};

// ==== Briloner IR code (NEC). Single power toggle, the lamp has no discrete on/off.
// https://github.com/Lucaslhm/Flipper-IRDB/blob/main/LED_Lighting/Briloner/Briloner_leuchten.ir
#define BRILONER_ADDRESS 0x00
#define BRILONER_TOGGLE 0x45

WiFiClient netClient;
PubSubClient mqtt(netClient);

uint32_t lastExecuted = 0;      // createdAt of the last executed command (0 = none)
unsigned long lastMqttAttempt = 0;

// Log to Serial and, when connected, to MQTT (picked up by Grafana).
void logf(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  Serial.println(buf);
  if (mqtt.connected()) mqtt.publish(TOPIC_LOGS, buf);
}

void restart(const char* reason) {
  logf("Restarting: %s", reason);
  delay(200);  // let Serial/MQTT flush
  ESP.restart();
}

void toggleBriloner() {
  IrSender.sendNEC(BRILONER_ADDRESS, BRILONER_TOGGLE, 0);
  delay(40);
  IrSender.sendNECRepeat();
  logf("IR: Briloner toggle sent");
}

// Handles both the retained state topic and the command topics.
void onMessage(char* topic, byte* payload, unsigned int len) {
  // Copy: the payload lives in PubSubClient's buffer, which publish() reuses.
  char msg[MQTT_BUFFER];
  len = min(len, (unsigned int)(sizeof msg - 1));
  memcpy(msg, payload, len);
  msg[len] = '\0';

  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, (const char*)msg)) {
    logf("Invalid JSON on %s: %s", topic, msg);
    return;
  }
  uint32_t createdAt = doc["createdAt"] | 0u;

  if (strcmp(topic, TOPIC_STATE) == 0) {
    if (createdAt > lastExecuted) lastExecuted = createdAt;
    logf("Last executed command: createdAt=%lu", (unsigned long)lastExecuted);
    return;
  }

  logf("Command on %s: %s", topic, msg);
  if (!createdAt) {
    logf("Ignored: missing createdAt");
    return;
  }
  long age = (long)(time(nullptr) - (time_t)createdAt);
  if (age > MAX_COMMAND_AGE_S) {
    logf("Ignored: expired (%lds old)", age);
    return;
  }
  if (createdAt <= lastExecuted) {
    logf("Ignored: already executed");
    return;
  }
  if (strcmp(doc["type"] | "", "led") != 0 || strcmp(doc["deviceName"] | "", "Luz Sala") != 0) {
    logf("Ignored: not for this device");
    return;
  }

  toggleBriloner();
  lastExecuted = createdAt;
  if (!mqtt.publish(TOPIC_STATE, msg, true)) logf("Failed to publish state");
}

// Single non-blocking attempt, rate limited.
void connectMqtt() {
  if (millis() - lastMqttAttempt < MQTT_RETRY_MS && lastMqttAttempt) return;
  lastMqttAttempt = millis();

  if (!mqtt.connect(LOCALE "-" ID)) {
    Serial.printf("MQTT connect failed, rc=%d\n", mqtt.state());
    return;
  }
  // Subscribe to the state topic FIRST: the broker sends its retained message
  // before anything from the command topics, so lastExecuted is known before
  // any command is evaluated. No retained state = execute any fresh command.
  mqtt.subscribe(TOPIC_STATE);
  for (const char* t : COMMAND_TOPICS) mqtt.subscribe(t);
  logf("MQTT connected, firmware %s, IP %s", CURRENT_VERSION, WiFi.localIP().toString().c_str());
}

// Fetch latest.json; flash firmware if its version differs from ours (reboots on success).
void checkOta() {
  WiFiClient client;  // separate socket, netClient belongs to MQTT
  HTTPClient http;
  http.begin(client, OTA_URL "latest.json");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    logf("OTA: latest.json HTTP %d", code);
    http.end();
    return;
  }
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    logf("OTA: invalid latest.json");
    return;
  }

  const char* version = doc["version"] | "";
  if (strcmp(version, CURRENT_VERSION) == 0) {
    logf("OTA: up to date (%s)", version);
    return;
  }
  logf("OTA: updating %s -> %s", CURRENT_VERSION, version);
  if (otaUpdater.update(client, String(OTA_URL) + (doc["file"] | "firmware.bin")) == HTTP_UPDATE_FAILED) {
    Serial.printf("OTA: failed: %s\n", otaUpdater.getLastErrorString().c_str());
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, LED_OFF);
  IrSender.begin(IR_PIN);
  Serial.printf("\nBoot sala/002, firmware %s\n", CURRENT_VERSION);

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("ESP-Config", "senha123")) restart("WiFi not configured");

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // UTC is enough: only ages are compared
  for (int i = 0; time(nullptr) < VALID_EPOCH; i++) {
    if (i > 60) restart("NTP sync timeout");
    delay(250);
  }

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setBufferSize(MQTT_BUFFER);
  mqtt.setCallback(onMessage);
  connectMqtt();

  checkOta();
}

void loop() {
  if (millis() >= RESTART_INTERVAL_MS) restart("scheduled (30 min)");
  if (WiFi.status() != WL_CONNECTED) restart("WiFi lost");
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();
}
