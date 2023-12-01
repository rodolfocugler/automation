#include <Arduino.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Samsung.h>

#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include "fauxmoESP.h"

// Rename the credentials.sample.h file to credentials.h and
// edit it according to your router configuration
#include "credentials.h"

fauxmoESP fauxmo;

int turnLedTVOffCounter = 0;

// -----------------------------------------------------------------------------

#define SERIAL_BAUDRATE 115200

IRsend irsend(4);

#define ID_LED_TV "Led TV"
#define ID_CONTROL "Controle"

/* IR Codes*/
#include "necCodes.h"
#include "samsungCodes.h"

uint8_t redLed;
uint8_t greenLed;
uint8_t blueLed;

// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Wifi
// -----------------------------------------------------------------------------

void wifiSetup() {
  // Set WIFI module to STA mode
  WiFi.mode(WIFI_STA);

  // Connect
  Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Wait
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();
  // Connected!
  Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void setup() {
  // Init serial port and clean garbage
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();
  Serial.println();

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
  fauxmo.addDevice(ID_LED_TV);
  fauxmo.addDevice(ID_CONTROL);

  fauxmo.onSetState([](unsigned char device_id, const char* device_name, bool state, unsigned char value, unsigned int hue, unsigned int saturation, unsigned int ct) {
    // Callback when a command from Alexa is received.
    // You can use device_id or device_name to choose the element to perform an action onto (relay, LED,...)
    // State is a boolean (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
    // Just remember not to delay too much here, this is a callback, exit as soon as possible.
    // If you have to do something more involved here set a flag and process it in your main loop.

    Serial.printf("[MAIN] Device #%d (%s) state: %s value: %d hue: %u saturation: %u ct: %u\n", device_id, device_name, state ? "ON" : "OFF", value, hue, saturation, ct);

    char colormode[3];
    fauxmo.getColormode(device_id, colormode, 3);
    Serial.printf("Colormode: %s\n", colormode);

    redLed = fauxmo.getRed(device_id);
    greenLed = fauxmo.getGreen(device_id);
    blueLed = fauxmo.getBlue(device_id);

    Serial.printf("if(redLed == %d && greenLed == %d && blueLed == %d)\n", redLed, greenLed, blueLed);

    // Checking for device_id is simpler if you are certain about the order they are loaded and it does not change.
    // Otherwise comparing the device_name is safer.

    if (strcmp(device_name, ID_LED_TV) == 0) {
      triggerLedTv(state, redLed, greenLed, blueLed);
    } else if (strcmp(device_name, ID_CONTROL) == 0) {
      triggerControl(state, redLed, greenLed, blueLed, value);
    }
  });
}

void triggerControl(bool state, int redLed, int greenLed, int blueLed, int value) {
  if (value == 254) {
    irsend.sendSAMSUNG(SAMSUNG_ON, 32, 1);
    turnLedTVOffCounter = 500;
  } else if (value == 251) {
    irsend.sendSAMSUNG(SAMSUNG_OFF, 32, 1);
  } else if (value == 249) {
    irsend.sendSAMSUNG(SAMSUNG_NETFLIX, 32, 1);
  } else if (value == 246) {
    irsend.sendSAMSUNG(SAMSUNG_EXIT, 32, 1);
  } else if (value == 244) {
    irsend.sendSAMSUNG(SAMSUNG_MIDDLE, 32, 1);
  }
}

void triggerLedTv(bool state, int redLed, int greenLed, int blueLed) {
  if (state) {
    irsend.sendNEC(IR_ON, 32);
    if (redLed == 253 && greenLed == 0 && blueLed == 0) {
      irsend.sendNEC(IR_R, 32);
    } else if (redLed == 253 && greenLed == 164 && blueLed == 0) {
      irsend.sendNEC(IR_B1, 32);  // ORANGE TODO
    } else if (redLed == 252 && greenLed == 253 && blueLed == 0) {
      irsend.sendNEC(IR_B2, 32);  // YELLOW TODO
    } else if (redLed == 243 && greenLed == 21 && blueLed == 66) {
      irsend.sendNEC(IR_B3, 32);  // YELLOW TODO
    } else if (redLed == 0 && greenLed == 253 && blueLed == 0) {
      irsend.sendNEC(IR_G, 32);
    } else if (redLed == 0 && greenLed == 0 && blueLed == 253) {
      irsend.sendNEC(IR_B, 32);
    } else if (redLed == 255 && greenLed == 193 && blueLed == 141) {
      irsend.sendNEC(IR_W, 32);
    }
  } else {
    irsend.sendNEC(IR_OFF, 32);
  }
}

void loop() {

  // fauxmoESP uses an async TCP server but a sync UDP server
  // Therefore, we have to manually poll for UDP packets
  fauxmo.handle();

  // This is a sample code to output free heap every 5 seconds
  // This is a cheap way to detect memory leaks
  static unsigned long last = millis();
  if (millis() - last > 5000) {
    last = millis();
    Serial.printf("[MAIN] Free heap: %d bytes\n", ESP.getFreeHeap());
  }

  if (turnLedTVOffCounter == 1) {
    Serial.printf("[turnLedTVOffCounter] %d\n", turnLedTVOffCounter);
    turnLedTVOffCounter--;
    irsend.sendNEC(IR_OFF, 32);
  } else if (turnLedTVOffCounter > 0) {
    Serial.printf("[turnLedTVOffCounter] %d\n", turnLedTVOffCounter);
    turnLedTVOffCounter--;
  }

  // If your device state is changed by any other means (MQTT, physical button,...)
  // you can instruct the library to report the new state to Alexa on next request:
  // fauxmo.setState(ID_YELLOW, true, 255);
}