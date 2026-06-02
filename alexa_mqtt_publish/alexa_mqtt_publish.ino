#include <Arduino.h>

#include <IRsend.h>
#include <ir_Samsung.h>

#include "fauxmoESP.h"
#include "LedTv.h"
#include "TvSamsung.h"

#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#ifdef ESP32
#include <IRRemoteESP32.h>
#include <WiFi.h>
#else
#include <IRremoteESP8266.h>
#include <ESP8266WiFi.h>
#endif

fauxmoESP fauxmo;

// -----------------------------------------------------------------------------

#define SERIAL_BAUDRATE 115200

#define ID_LED_BED "Led Cama"
#define ID_LED_TV "Led TV"
#define ID_CONTROL "Controle"

#define LED_BUILTIN 2

IRsend irsend(23);

int command = 0;
uint8_t redLed;
uint8_t greenLed;
uint8_t blueLed;

const char* mqtt_server = "pi-desktop";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

void callback(char* topic, byte* payload, unsigned int length) {
  digitalWrite(LED_BUILTIN, HIGH);
  payload[length] = '\0';
  String message = String((char*)payload);
  Serial.printf("[MQTT] Recebido em %s: %s\n", topic, message.c_str());

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("Erro ao fazer parse do JSON: ");
    Serial.println(error.c_str());
    return;
  }

  const char* device = doc["device"];
  bool state = doc["state"];
  int value = doc["value"];
  int red = doc["red"];
  int green = doc["green"];
  int blue = doc["blue"];

  if (strcmp(device, ID_LED_TV) == 0) {
    LedTv::triggerLedTv(state, redLed, greenLed, blueLed);
  } else if (strcmp(device, ID_CONTROL) == 0) {
    TvSamsung::triggerControl(state, redLed, greenLed, blueLed, value);
  }
  digitalWrite(LED_BUILTIN, LOW);
}


void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando ao MQTT...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      client.subscribe("alexa/sala/1");
      client.subscribe("alexa/sala/all");
      client.subscribe("alexa/all");
      Serial.println("conectado!");
    } else {
      Serial.print("falha, rc=");
      Serial.print(client.state());
      Serial.println(" tentando em 2 segundos...");
      delay(2000);
    }
  }
}

// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Wifi
// -----------------------------------------------------------------------------

void wifiSetup() {
  WiFiManager wifiManager;
  if (!wifiManager.autoConnect("ESP-Config", "senha123")) {
    Serial.println("fail to connect and timeout reached.");
    delay(3000);
    ESP.restart();
  }

  Serial.println("Connected to Wi-Fi!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  // Init serial port and clean garbage
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();
  Serial.println();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Wifi
  wifiSetup();

  irsend.begin();

  // By default, fauxmoESP creates it's own webserver on the defined port
  // The TCP port must be 80 for gen3 devices (default is 1901)
  // This has to be done before the call to enable()
  fauxmo.createServer(true);  // not needed, this is the default value
  fauxmo.setPort(80);         // This is required for gen3 devices

  // You have to call enable(true) once you have a WiFi connection
  // You can enable or disable the library at any moment
  // Disabling it will prevent the devices from being discovered and switched
  fauxmo.enable(true);

  // You can use different ways to invoke alexa to modify the devices state:
  // "Alexa, turn yellow lamp on"
  // "Alexa, turn on yellow lamp
  // "Alexa, set yellow lamp to fifty" (50 means 50% of brightness, note, this example does not use this functionality)

  // Add virtual devices
  fauxmo.addDevice(ID_LED_BED);
  fauxmo.addDevice(ID_LED_TV);
  fauxmo.addDevice(ID_CONTROL);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  fauxmo.onSetState([](unsigned char device_id, const char* device_name, bool state, unsigned char value, unsigned int hue, unsigned int saturation, unsigned int ct) {
    // Callback when a command from Alexa is received.
    // You can use device_id or device_name to choose the element to perform an action onto (relay, LED,...)
    // State is a boolean (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
    // Just remember not to delay too much here, this is a callback, exit as soon as possible.
    // If you have to do something more involved here set a flag and process it in your main loop.
    command = 1;
    Serial.printf("[MAIN] Device #%d (%s) state: %s value: %d hue: %u saturation: %u ct: %u\n", device_id, device_name, state ? "ON" : "OFF", value, hue, saturation, ct);

    char colormode[3];
    fauxmo.getColormode(device_id, colormode, 3);
    Serial.printf("Colormode: %s\n", colormode);

    redLed = fauxmo.getRed(device_id);
    greenLed = fauxmo.getGreen(device_id);
    blueLed = fauxmo.getBlue(device_id);

    Serial.printf("redLed = %d & greenLed = %d & blueLed = %d\n", redLed, greenLed, blueLed);

    // Checking for device_id is simpler if you are certain about the order they are loaded and it does not change.
    // Otherwise comparing the device_name is safer.
    StaticJsonDocument<256> doc;
    doc["device"] = device_name;
    doc["state"] = state;
    doc["value"] = value;
    doc["red"] = redLed;
    doc["green"] = greenLed;
    doc["blue"] = blueLed;
    char buffer[256];
    serializeJson(doc, buffer);

    String topic = "alexa/";
    if (strcmp(device_name, ID_LED_BED) == 0) {
      topic += "quarto/1";
    } else if (strcmp(device_name, ID_LED_TV) == 0) {
      topic += "sala/1";
      LedTv::triggerLedTv(state, redLed, greenLed, blueLed);
    } else if (strcmp(device_name, ID_CONTROL) == 0) {
      topic += "sala/1";
    TvSamsung::triggerControl(state, redLed, greenLed, blueLed, value);
    }

    if (client.connected()) {
      client.publish(topic.c_str(), buffer);
      Serial.printf("[MQTT] Publicado em %s: %s\n", topic.c_str(), buffer);
    } else {
      Serial.println("[MQTT] Erro: MQTT não conectado!");
    }
  });
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (WiFi.isConnected()) {
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
    ESP.restart();
  }

  // fauxmoESP uses an async TCP server but a sync UDP server
  // Therefore, we have to manually poll for UDP packets
  fauxmo.handle();

  // This is a sample code to output free heap every 5 seconds
  // This is a cheap way to detect memory leaks
  static unsigned long last = millis();
  if (millis() - last > 5000) {
    last = millis();
    Serial.printf("[MAIN] Free heap: %d bytes\n", ESP.getFreeHeap());

    if (command == 1) {
      digitalWrite(LED_BUILTIN, HIGH);
      command = 2;
    } else if (command == 2) {
      digitalWrite(LED_BUILTIN, LOW);
      command = 0;
    }
  }

  // If your device state is changed by any other means (MQTT, physical button,...)
  // you can instruct the library to report the new state to Alexa on next request:
  // fauxmo.setState(ID_YELLOW, true, 255);
}