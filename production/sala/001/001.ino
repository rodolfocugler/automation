/**
 * sala/001 - TV LED strip + Samsung TV (ESP32 / ESP8266)
 *
 * Listens for Alexa commands over MQTT and drives, via IR, the LED strip behind
 * the TV (on/off + color) and the Samsung TV (power, media, mute).
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

// ==== TV LED strip IR codes (NEC, 32 bit MSB first, 24-key RGB remote) ====
#define LED_ON 0xF7C03F
#define LED_OFF_CODE 0xF740BF

struct LedColor { uint32_t code; uint8_t r, g, b; };
const LedColor LED_COLORS[] = {
  {0xF720DF, 255, 0, 0},     // R
  {0xF7A05F, 0, 255, 0},     // G
  {0xF7609F, 0, 0, 255},     // B
  {0xF7E01F, 255, 255, 255}, // W
  {0xF710EF, 235, 40, 7},
  {0xF7906F, 27, 167, 70},
  {0xF750AF, 4, 126, 210},
  {0xF730CF, 235, 66, 11},
  {0xF7B04F, 33, 197, 200},
  {0xF7708F, 88, 29, 71},
  {0xF708F7, 244, 134, 23},  // orange
  {0xF78877, 2, 137, 151},
  {0xF748B7, 127, 35, 83},
  {0xF728D7, 254, 235, 50},  // yellow
  {0xF7A857, 8, 90, 99},     // cyan
  {0xF76897, 204, 50, 94},
};

// ==== Samsung TV IR codes (32 bit MSB first), keyed by Alexa "value" ====
struct TvKey { const char* value; uint32_t code; };
const TvKey TV_POWER[] = {{"On", 0xE0E09966}, {"Off", 0xE0E019E6}};
const TvKey TV_MEDIA[] = {
  {"Netflix", 0xE0E0CF30}, {"Youtube", 0xE0E00E8C}, {"Exit", 0xE0E0B44B},
  {"Ok", 0xE0E016E9}, {"Play", 0xE0E0E21D}, {"Pause", 0xE0E052AD},
};
#define TV_MUTE 0xE0E0F00F

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

// Nearest palette entry by squared RGB distance.
uint32_t closestColor(int r, int g, int b) {
  uint32_t best = LED_COLORS[0].code;
  long bestDist = LONG_MAX;
  for (const LedColor& c : LED_COLORS) {
    long d = (long)(r - c.r) * (r - c.r) + (long)(g - c.g) * (g - c.g) + (long)(b - c.b) * (b - c.b);
    if (d < bestDist) { bestDist = d; best = c.code; }
  }
  return best;
}

// rgb: null keeps the strip's current color.
void setLedTv(bool on, JsonArrayConst rgb) {
  if (!on) {
    IrSender.sendNECMSB(LED_OFF_CODE, 32);
    logf("IR: Led TV off");
    return;
  }
  IrSender.sendNECMSB(LED_ON, 32);
  logf("IR: Led TV on");
  if (rgb.isNull()) return;
  delay(40);
  uint32_t code = closestColor(rgb[0], rgb[1], rgb[2]);
  IrSender.sendNECMSB(code, 32);
  logf("IR: Led TV color RGB(%d,%d,%d) -> 0x%06lX", (int)rgb[0], (int)rgb[1], (int)rgb[2], (unsigned long)code);
}

// Sent twice, like the original remote/firmware (Samsung ignores the duplicate).
void sendTv(uint32_t code) {
  IrSender.sendSamsungMSB(code, 32);
  delay(50);
  IrSender.sendSamsungMSB(code, 32);
}

template <size_t N>
bool sendTvKey(const TvKey (&keys)[N], const char* value) {
  for (const TvKey& k : keys) {
    if (strcmp(k.value, value) == 0) {
      sendTv(k.code);
      return true;
    }
  }
  return false;
}

// Returns false if the action/value is unknown (nothing sent).
bool controlTv(const char* action, const char* value) {
  bool sent = false;
  if (strcmp(action, "setPowerState") == 0) sent = sendTvKey(TV_POWER, value);
  else if (strcmp(action, "mediaControl") == 0) sent = sendTvKey(TV_MEDIA, value);
  else if (strcmp(action, "setMute") == 0) { sendTv(TV_MUTE); sent = true; }
  logf(sent ? "IR: TV %s %s" : "Ignored: unknown TV command %s %s", action, value);
  return sent;
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

  const char* type = doc["type"] | "";
  if (strcmp(type, "led") == 0 && strcmp(doc["deviceName"] | "", "Led TV") == 0) {
    setLedTv(doc["state"] | false, doc["rgb_color"]);
  } else if (strcmp(type, "tv") == 0) {
    if (!controlTv(doc["action"] | "", doc["value"] | "")) return;
  } else {
    logf("Ignored: not for this device");
    return;
  }

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
  Serial.printf("\nBoot sala/001, firmware %s\n", CURRENT_VERSION);

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
