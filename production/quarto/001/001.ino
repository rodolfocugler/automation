/**
 * quarto/001 - Bedroom LED strip "Led Cama" (Triones, BLE) (ESP32 only)
 *
 * Listens for Alexa commands over MQTT and drives the Triones LED strip over BLE
 * (on/off + color + brightness).
 * The last executed command is stored (retained) in device/<LOCALE>/<ID>, so a
 * command is never executed twice, even across restarts.
 *
 * Boot: WiFi (WiFiManager) -> NTP -> MQTT -> OTA check -> BLE task -> loop.
 * The board restarts every 30 min, so the OTA check at boot runs every 30 min.
 *
 * Two tasks, so neither can block the other:
 * - loop (Arduino task): MQTT only. Validates commands and queues them for BLE.
 * - bleTask: owns NimBLE (init, connect, write). Reports results and logs back
 *   through queues, because PubSubClient is not thread-safe (only loop touches it).
 *
 * Logs go to Serial (115200) and to MQTT topic logs/<LOCALE>/<ID> (Grafana).
 * See README.md for details.
 */

#ifndef ESP32
  #error "quarto/001 needs BLE: build for ESP32 (esp32:esp32:esp32)"
#endif

// NimBLE-Arduino works here ONLY on version 1.4.3, and 1.4.3 needs arduino-esp32
// core 2.0.17 (esp32:esp32@2.0.17). On core 3.x NimBLEDevice::init() hangs forever
// and the whole board stops. Run setup.sh to install the right versions.
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
#define MQTT_SOCKET_TIMEOUT_S 5      // bound a stalled connect/CONNACK (default 15 s)
#define OTA_URL "http://pi-desktop:9595/" CHIP "/" LOCALE "/" ID "/"
#define RESTART_INTERVAL_MS (30UL * 60 * 1000)
#define MQTT_RETRY_MS 5000
#define MAX_COMMAND_AGE_S 30
#define VALID_EPOCH 1700000000   // anything below means NTP has not synced yet
#define LOG_LEN 256
#define LOG_QUEUE_LEN 16         // BLE task logs waiting for loop to publish (dropped when full)
#define RESULT_QUEUE_LEN 4
#define BLE_TASK_STACK 8192

// ==== Topics (compile-time strings, no heap) ====
#define TOPIC_STATE "device/" LOCALE "/" ID   // last executed command (retained)
#define TOPIC_LOGS "logs/" LOCALE "/" ID
const char* const COMMAND_TOPICS[] = {"alexa/" LOCALE "/" ID, "alexa/" LOCALE "/all", "alexa/all"};

// ==== Triones BLE LED ====
#define LED_MAC "53:02:02:00:04:BB"
// NimBLE-Arduino 1.4.3 (needs core 2.0.17): same API and timings as the old trianon_led_alexa
// firmware, which connected reliably to this strip (2.x kept failing with error 574).
#define BLE_CONNECT_ATTEMPTS 10
#define BLE_CONNECT_TIMEOUT_S 5       // NimBLE 1.x: seconds (2.x takes ms)
#define BLE_RETRY_DELAY_MS 500
#define BLE_IDLE_RETRY_MS 15000       // BLE task: reconnect while idle, so a command finds the link up
#define MQTT_KEEPALIVE_S 90
// Units of 1.25 ms (interval) and 10 ms (supervision): fixed 8.75 ms interval, 2 s
// supervision timeout, as in the old firmware.
#define BLE_CONN_ITVL_MIN 7
#define BLE_CONN_ITVL_MAX 7
#define BLE_SUPERVISION_TIMEOUT 200
const NimBLEUUID LED_SERVICE("FFD5");
const NimBLEUUID LED_WRITE_CHAR("FFD9");
const uint8_t LED_ON_CMD[] = {0xCC, 0x23, 0x33};
const uint8_t LED_OFF_CMD[] = {0xCC, 0x24, 0x33};
// Color: 0x56 R G B 0x00 0xF0 0xAA

// A validated command, handed from loop to the BLE task and back with the result.
struct LedCommand {
  uint32_t createdAt;
  bool on;
  bool hasRgb;          // false keeps the strip's current color
  uint8_t rgb[3];
  int brightnessPct;    // scales the color (0-100)
  bool ok;              // set by the BLE task
  char msg[MQTT_BUFFER];  // original payload, stored in TOPIC_STATE on success
};

WiFiClient netClient;
PubSubClient mqtt(netClient);
NimBLEClient* ble = nullptr;       // BLE task only

TaskHandle_t loopTask = nullptr;
QueueHandle_t commandQueue;        // loop -> BLE, length 1: a newer command replaces a waiting one
QueueHandle_t resultQueue;         // BLE -> loop
QueueHandle_t logQueue;            // BLE -> loop

uint32_t lastExecuted = 0;      // createdAt of the last executed command (0 = none)
uint32_t lastQueued = 0;        // createdAt of the last command handed to BLE
unsigned long lastMqttAttempt = 0;

// Log to Serial and to MQTT (picked up by Grafana). From the BLE task the line is
// queued and published by loop.
void logf(const char* fmt, ...) {
  char buf[LOG_LEN];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  Serial.println(buf);
  if (xTaskGetCurrentTaskHandle() != loopTask) {
    xQueueSend(logQueue, buf, 0);
    return;
  }
  if (mqtt.connected()) mqtt.publish(TOPIC_LOGS, buf);
}

void restart(const char* reason) {
  logf("Restarting: %s", reason);
  delay(200);  // let Serial/MQTT flush
  ESP.restart();
}

// ==== BLE task ====

bool connectLedOnce() {
  // false: keep the discovered services, so a reconnect skips discovery.
  if (ble->connect(NimBLEAddress(LED_MAC), false)) {
    logf("BLE: connected to " LED_MAC);
    return true;
  }
  logf("BLE: connect failed, error %d", ble->getLastError());
  return false;
}

// The strip drops idle connections, so reconnect on demand.
bool connectLed() {
  if (ble->isConnected()) return true;
  for (int i = 1; i <= BLE_CONNECT_ATTEMPTS; i++) {
    if (connectLedOnce()) return true;
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

bool setLedCama(const LedCommand& cmd) {
  if (!writeLed(cmd.on ? LED_ON_CMD : LED_OFF_CMD, 3)) return false;
  logf("BLE: Led Cama %s", cmd.on ? "on" : "off");
  if (!cmd.on || !cmd.hasRgb) return true;

  int brightnessPct = constrain(cmd.brightnessPct, 0, 100);
  uint8_t r = cmd.rgb[0] * brightnessPct / 100;
  uint8_t g = cmd.rgb[1] * brightnessPct / 100;
  uint8_t b = cmd.rgb[2] * brightnessPct / 100;
  const uint8_t color[] = {0x56, r, g, b, 0x00, 0xF0, 0xAA};
  if (!writeLed(color, sizeof color)) return false;
  logf("BLE: Led Cama color RGB(%d,%d,%d) at %d%%", r, g, b, brightnessPct);
  return true;
}

// Owns NimBLE: blocking connects/writes here never stall MQTT in loop.
void bleTask(void*) {
  logf("BLE: starting");
  NimBLEDevice::setScanDuplicateCacheSize(10);
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // max TX power
  NimBLEDevice::setMTU(23);                // required by Triones
  ble = NimBLEDevice::createClient();
  ble->setConnectionParams(BLE_CONN_ITVL_MIN, BLE_CONN_ITVL_MAX, 0, BLE_SUPERVISION_TIMEOUT);
  ble->setConnectTimeout(BLE_CONNECT_TIMEOUT_S);
  logf("BLE: ready");

  static LedCommand cmd;  // static: too big for the task stack
  for (;;) {
    // Wait for a command; on timeout, keep the link up so the next command is fast.
    if (xQueueReceive(commandQueue, &cmd, pdMS_TO_TICKS(BLE_IDLE_RETRY_MS))) {
      cmd.ok = setLedCama(cmd);
      xQueueSend(resultQueue, &cmd, portMAX_DELAY);
    } else if (!ble->isConnected()) {
      connectLedOnce();
    }
  }
}

// ==== MQTT (loop task) ====

// Handles both the retained state topic and the command topics.
void onMessage(char* topic, byte* payload, unsigned int len) {
  static LedCommand cmd;  // static: too big for the loop stack
  // Copy: the payload lives in PubSubClient's buffer, which publish() reuses.
  len = min(len, (unsigned int)(sizeof cmd.msg - 1));
  memcpy(cmd.msg, payload, len);
  cmd.msg[len] = '\0';

  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, (const char*)cmd.msg)) {
    logf("Invalid JSON on %s: %s", topic, cmd.msg);
    return;
  }
  uint32_t createdAt = doc["createdAt"] | 0u;

  if (strcmp(topic, TOPIC_STATE) == 0) {
    if (createdAt > lastExecuted) lastExecuted = createdAt;
    logf("Last executed command: createdAt=%lu", (unsigned long)lastExecuted);
    return;
  }

  logf("Command on %s: %s", topic, cmd.msg);
  if (!createdAt) {
    logf("Ignored: missing createdAt");
    return;
  }
  long age = (long)(time(nullptr) - (time_t)createdAt);
  if (age > MAX_COMMAND_AGE_S) {
    logf("Ignored: expired (%lds old)", age);
    return;
  }
  // lastQueued: a retained command redelivered (MQTT reconnect) while BLE still runs it.
  if (createdAt <= max(lastExecuted, lastQueued)) {
    logf("Ignored: already executed");
    return;
  }
  if (strcmp(doc["type"] | "", "led") != 0 || strcmp(doc["deviceName"] | "", "Led Cama") != 0) {
    logf("Ignored: not for this device");
    return;
  }

  JsonArrayConst rgb = doc["rgb_color"];
  cmd.createdAt = createdAt;
  cmd.on = doc["state"] | false;
  cmd.hasRgb = !rgb.isNull();
  for (int i = 0; i < 3; i++) cmd.rgb[i] = rgb[i] | 0;
  cmd.brightnessPct = doc["brightness_pct"] | 100;
  xQueueOverwrite(commandQueue, &cmd);
  lastQueued = createdAt;
}

// Publish what the BLE task finished: its logs and the executed commands.
void drainBle() {
  char line[LOG_LEN];
  while (xQueueReceive(logQueue, line, 0)) {
    if (mqtt.connected()) mqtt.publish(TOPIC_LOGS, line);
  }

  static LedCommand done;
  while (xQueueReceive(resultQueue, &done, 0)) {
    if (!done.ok) {
      // Not stored, so the same command can be retried.
      if (done.createdAt == lastQueued) lastQueued = lastExecuted;
      continue;
    }
    if (done.createdAt <= lastExecuted) continue;
    lastExecuted = done.createdAt;
    if (!mqtt.publish(TOPIC_STATE, done.msg, true)) logf("Failed to publish state");
  }
}

// Single attempt, rate limited.
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
  loopTask = xTaskGetCurrentTaskHandle();  // setup and loop run in the same task
  commandQueue = xQueueCreate(1, sizeof(LedCommand));
  resultQueue = xQueueCreate(RESULT_QUEUE_LEN, sizeof(LedCommand));
  logQueue = xQueueCreate(LOG_QUEUE_LEN, LOG_LEN);

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
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
  mqtt.setCallback(onMessage);
  connectMqtt();

  checkOta();  // before BLE: the BLE stack takes RAM the update needs

  // Same priority and core as loop: BLE waits (connect, write) yield to it.
  xTaskCreatePinnedToCore(bleTask, "ble", BLE_TASK_STACK, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE);
}

void loop() {
  if (millis() >= RESTART_INTERVAL_MS) restart("scheduled (30 min)");
  if (WiFi.status() != WL_CONNECTED) restart("WiFi lost");
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();
  drainBle();
  delay(1);  // yield to the BLE task (same priority)
}
