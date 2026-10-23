/**
 * quarto/001 - Bedroom LED strip "Led Cama" (Triones, BLE) (ESP32 only)
 *
 * Listens for Alexa commands over MQTT and drives the Triones LED strip over BLE
 * (on/off + color + brightness).
 * The last executed command is stored (retained) in device/<LOCALE>/<ID>, so a
 * command is never executed twice, even across restarts.
 *
 * Boot: WiFi (WiFiManager) -> NTP -> MQTT -> OTA check -> BLE -> loop.
 * The board restarts every 30 min, so the OTA check at boot runs every 30 min.
 *
 * Logs go to Serial (115200) and to MQTT topic logs/<LOCALE>/<ID> (Grafana).
 * See README.md for details.
 */

#ifndef ESP32
  #error "quarto/001 needs BLE: build for ESP32 (esp32:esp32:esp32)"
#endif

#include <NimBLEDevice.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <time.h>

#define CHIP "esp32"
#define LED_OFF LOW           // devkit onboard LED is active-high

// ==== Device identity ====
#define LOCALE "quarto"
#define ID "001"

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

// ==== Triones BLE LED ====
#define LED_MAC "53:02:02:00:04:BB"
#define BLE_CONNECT_ATTEMPTS 3
#define BLE_CONNECT_TIMEOUT_MS 10000  // NimBLE 2.x: ms (1.x took seconds)
#define BLE_RETRY_DELAY_MS 1000
// Error 574 (HCI 0x3E, connection failed to be established): the strip missed the
// first packets of the new link. NimBLE retries it inside connect() (default 2, max 7).
#define BLE_CONN_FAIL_RETRIES 7
// Units of 1.25 ms (interval) and 10 ms (supervision): 75-100 ms interval, 6 s
// supervision timeout, so the slow strip survives Wi-Fi sharing the radio. A new
// link fails (574) if no packet arrives within 6 intervals, so longer = more slack.
#define BLE_CONN_ITVL_MIN 60
#define BLE_CONN_ITVL_MAX 80
#define BLE_SUPERVISION_TIMEOUT 600
const NimBLEUUID LED_SERVICE("FFD5");
const NimBLEUUID LED_WRITE_CHAR("FFD9");
const uint8_t LED_ON_CMD[] = {0xCC, 0x23, 0x33};
const uint8_t LED_OFF_CMD[] = {0xCC, 0x24, 0x33};
// Color: 0x56 R G B 0x00 0xF0 0xAA

WiFiClient netClient;
PubSubClient mqtt(netClient);
NimBLEClient* ble = nullptr;

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

// The strip drops idle connections, so reconnect on demand.
bool connectLed() {
  if (ble->isConnected()) return true;
  for (int i = 1; i <= BLE_CONNECT_ATTEMPTS; i++) {
    if (ble->connect(NimBLEAddress(LED_MAC, BLE_ADDR_PUBLIC))) {
      logf("BLE: connected to " LED_MAC);
      return true;
    }
    logf("BLE: connect attempt %d failed, error %d", i, ble->getLastError());
    delay(BLE_RETRY_DELAY_MS);
  }
  return false;
}

bool writeLed(const uint8_t* data, size_t len) {
  if (!connectLed()) return false;
  NimBLERemoteService* service = ble->getService(LED_SERVICE);
  NimBLERemoteCharacteristic* chr = service ? service->getCharacteristic(LED_WRITE_CHAR) : nullptr;
  if (!chr || !chr->writeValue(data, len, false)) {
    logf("BLE: write failed");
    return false;
  }
  return true;
}

// rgb: null keeps the strip's current color. brightnessPct scales the color (0-100).
bool setLedCama(bool on, JsonArrayConst rgb, int brightnessPct) {
  if (!writeLed(on ? LED_ON_CMD : LED_OFF_CMD, 3)) return false;
  logf("BLE: Led Cama %s", on ? "on" : "off");
  if (!on || rgb.isNull()) return true;

  brightnessPct = constrain(brightnessPct, 0, 100);
  uint8_t r = (int)rgb[0] * brightnessPct / 100;
  uint8_t g = (int)rgb[1] * brightnessPct / 100;
  uint8_t b = (int)rgb[2] * brightnessPct / 100;
  const uint8_t color[] = {0x56, r, g, b, 0x00, 0xF0, 0xAA};
  if (!writeLed(color, sizeof color)) return false;
  logf("BLE: Led Cama color RGB(%d,%d,%d) at %d%%", r, g, b, brightnessPct);
  return true;
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
  if (strcmp(doc["type"] | "", "led") != 0 || strcmp(doc["deviceName"] | "", "Led Cama") != 0) {
    logf("Ignored: not for this device");
    return;
  }

  // Not stored on failure, so the same command can be retried.
  if (!setLedCama(doc["state"] | false, doc["rgb_color"], doc["brightness_pct"] | 100)) return;
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
  if (httpUpdate.update(client, String(OTA_URL) + (doc["file"] | "firmware.bin")) == HTTP_UPDATE_FAILED) {
    Serial.printf("OTA: failed: %s\n", httpUpdate.getLastErrorString().c_str());
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, LED_OFF);
  Serial.printf("\nBoot quarto/001, firmware %s\n", CURRENT_VERSION);

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

  checkOta();  // before BLE: the BLE stack takes RAM the update needs

  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // max TX power
  NimBLEDevice::setMTU(23);                // required by Triones
  ble = NimBLEDevice::createClient();
  ble->setConnectionParams(BLE_CONN_ITVL_MIN, BLE_CONN_ITVL_MAX, 0, BLE_SUPERVISION_TIMEOUT);
  ble->setConnectTimeout(BLE_CONNECT_TIMEOUT_MS);
  ble->setConnectRetries(BLE_CONN_FAIL_RETRIES);
  if (!connectLed()) logf("BLE: LED not reachable, will retry on next command");
}

void loop() {
  if (millis() >= RESTART_INTERVAL_MS) restart("scheduled (30 min)");
  if (WiFi.status() != WL_CONNECTED) restart("WiFi lost");
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();
}
