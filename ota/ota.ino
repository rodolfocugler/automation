/*
 * OTA bootstrap: flash this over USB to any ESP32/ESP8266 and it replaces itself
 * with the latest production firmware for LOCALE/ID from the Pi.
 *
 * Set LOCALE and ID below (or pass -DLOCALE="\"quarto\"" -DID="\"001\"" at compile time).
 * WiFi is configured through the same "ESP-Config" portal as production, so the
 * saved credentials carry over after the flash.
 */
#include <WiFiManager.h>
#include <ArduinoJson.h>

#ifdef ESP32
  #include <HTTPClient.h>
  #include <HTTPUpdate.h>
  #define CHIP "esp32"
  #define otaUpdater httpUpdate
#else
  #include <ESP8266HTTPClient.h>
  #include <ESP8266httpUpdate.h>
  #define CHIP "esp8266"
  #define otaUpdater ESPhttpUpdate
#endif

// ==== Device identity ====
#ifndef LOCALE
  #define LOCALE "sala"   // "sala" or "quarto"
#endif
#ifndef ID
  #define ID "002"        // "001", "002", ...
#endif

#define OTA_URL "http://pi-desktop:9595/" CHIP "/" LOCALE "/" ID "/"
#define RETRY_MS 30000

// Fetch latest.json and flash its firmware unconditionally (reboots on success).
void flashLatest() {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, OTA_URL "latest.json");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("OTA: %slatest.json HTTP %d\n", OTA_URL, code);
    http.end();
    return;
  }
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    Serial.println("OTA: invalid latest.json");
    return;
  }

  Serial.printf("OTA: flashing %s\n", doc["version"] | "?");
  if (otaUpdater.update(client, String(OTA_URL) + (doc["file"] | "firmware.bin")) == HTTP_UPDATE_FAILED) {
    Serial.printf("OTA: failed: %s\n", otaUpdater.getLastErrorString().c_str());
  }
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\nOTA bootstrap for " CHIP "/" LOCALE "/" ID "\n");

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("ESP-Config", "senha123")) {
    Serial.println("WiFi not configured, restarting");
    ESP.restart();
  }
}

void loop() {
  flashLatest();
  delay(RETRY_MS);
}
