#include <ArduinoJson.h>
#include <WiFiManager.h>

#ifdef ESP8266
  #include <ESP8266WiFi.h>
  #include <ESP8266httpUpdate.h>
  #include <ESP8266HTTPClient.h>
  #include <WiFiClient.h>
  #define LED_PIN LED_BUILTIN
#else
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <HTTPUpdate.h>
  #define LED_PIN 2
#endif

// ================= OTA CONFIG =================
#define OTA_BASE_URL "http://pi-desktop:9595/esp32"
#define CURRENT_VERSION "20260602_214237"

// ================= GLOBAL =================
WiFiClient client;

unsigned long lastOTACheck = 0;
const unsigned long OTA_INTERVAL = 30000;

// ================= BLINK =================
void blinkTask() {
#ifdef ESP8266
  digitalWrite(LED_PIN, LOW);
  delay(300);
  digitalWrite(LED_PIN, HIGH);
  delay(300);
#else
  digitalWrite(LED_PIN, HIGH);
  delay(300);
  digitalWrite(LED_PIN, LOW);
  delay(300);
#endif
}

// ================= OTA CHECK =================
void checkOTA() {
  if (!WiFi.isConnected()) return;

  HTTPClient http;
  String url = String(OTA_BASE_URL) + "/latest.json";

#ifdef ESP8266
  http.begin(client, url);
#else
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

  String fwURL = String(OTA_BASE_URL) + "/" + file;

#ifdef ESP8266
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, fwURL);
#else
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

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);

  WiFiManager wm;
  wm.setTimeout(60);

  if (!wm.autoConnect("ESP-OTA")) {
    Serial.println("WiFi failed, restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("WiFi connected!");
  Serial.println(WiFi.localIP());
}

// ================= LOOP =================
void loop() {

  // ===== BLINK =====
  blinkTask();

  // ===== OTA CHECK =====
  if (millis() - lastOTACheck > OTA_INTERVAL) {
    lastOTACheck = millis();
    checkOTA();
  }
}