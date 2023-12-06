#include <Arduino.h>

#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include "fauxmoESP.h"
#include "nimbletriones.h"
#include <NimBLEDevice.h>

// Triones service specifics
const NimBLEUUID NOTIFY_SERVICE("FFD0");
const NimBLEUUID NOTIFY_CHAR("FFD4");
const NimBLEUUID MAIN_SERVICE("FFD5");
const NimBLEUUID WRITE_CHAR("FFD9");
const std::string mac = "53:02:02:00:04:BB";

// Rename the credentials.sample.h file to credentials.h and
// edit it according to your router configuration
#include "credentials.h"

fauxmoESP fauxmo;

// -----------------------------------------------------------------------------

#define SERIAL_BAUDRATE 115200


#define ID_LED_BED "Led Cama"

uint8_t redLed;
uint8_t greenLed;
uint8_t blueLed;
static BLEClientCallbacks clientCB;

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

bool do_write(NimBLEAddress bleAddr, const uint8_t* payload, size_t length) {
  NimBLEClient* pClient = nullptr;

  if (NimBLEDevice::getClientListSize()) {
    pClient = NimBLEDevice::getClientByPeerAddress(bleAddr);
    if (pClient) {
      if (!pClient->isConnected()) {
        if (!pClient->connect(bleAddr, false)) {
          Serial.println("Tried to reconnect to known device, but failed");
        }
      }
    } else {
      pClient = NimBLEDevice::getDisconnectedClient();
    }
  }

  // If we get here we are either connected OR we need to try and reuse an old client before creating a new client
  if (!pClient) {
    // There wasn't an old one to reuse, create a new one
    if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
      Serial.println("Max connections reached. No more available.");
      return false;
    }
    pClient = NimBLEDevice::createClient();
    pClient->setConnectionParams(7, 7, 0, 200);
    //  Testing shows that this does seem to allow initial connections and reconnections more readily. Even though the tolerances are much lower
    //  it does seem to work.  It will retry at RSSI -90, but will connect usually within the first 5 tries.
    pClient->setClientCallbacks(&clientCB, false);
    pClient->setConnectTimeout(5);
    pClient->connect(bleAddr, false);
  }

  if (!pClient->isConnected()) {
    int i = 0;
    while ((i <= 10) && (!pClient->isConnected())) {
      if (!pClient->connect(bleAddr, false)) {
        Serial.print("Failed to connect. In retry loop: ");
        Serial.println(i);
        delay(500);
        i++;
      }
    };
    if (!pClient->isConnected()) {
      Serial.println("Retrying failed.  Sorry");
      NimBLEDevice::deleteClient(pClient);
      return false;
    }
  }

  Serial.print("Connected to device: ");
  Serial.println(pClient->getPeerAddress().toString().c_str());

  NimBLERemoteService* pSvc = nullptr;
  NimBLERemoteCharacteristic* pChr = nullptr;
  NimBLERemoteService* nSvc = nullptr;
  NimBLERemoteCharacteristic* nChr = nullptr;

  nSvc = pClient->getService(NOTIFY_SERVICE);

  if (!nSvc) {
    Serial.println("Failed to get nsvc");
    return false;
  }

  if (!pClient) {
    Serial.println("Client has gone away");
    return false;
  }
  pSvc = pClient->getService(MAIN_SERVICE);
  if (pSvc) {
    pChr = pSvc->getCharacteristic(WRITE_CHAR);
    if (pChr->canWrite()) {
      if (pChr->writeValue(payload, length, false)) {
        Serial.println("Write complete");
      };
    } else Serial.println("WRITE: Couldnt write");
  } else {
    Serial.println("Failed to write data.");
  }
  return true;
}

void setup() {
  // Init serial port and clean garbage
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();
  Serial.println();

  // Wifi
  wifiSetup();

  // Setup Nimble
  Serial.println("Starting NimBLE Client");
  NimBLEDevice::setScanDuplicateCacheSize(10);
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // max power
  NimBLEDevice::setMTU(23);                // This is what the lights ask for, so set up front. I think this made it more reliable to connect first time.

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

    if (strcmp(device_name, ID_LED_BED) == 0) {
      triggerLedCommand(state, redLed, greenLed, blueLed, value);
    }
  });
}

void triggerLedCommand(bool state, int redLed, int greenLed, int blueLed, int value) {
  powerSet(state);
  uint8_t rgbArray[3] = { redLed, greenLed, blueLed };

  if ((rgbArray[0] == 0) && (rgbArray[1] == 0) && (rgbArray[2] == 0)) {
    // Everything was zero, so presto changeo...
    rgbArray[0] = 255;
    rgbArray[1] = 255;
    rgbArray[2] = 255;
  }

  //                          r   g   b   w  (cheaper lights don't have white leds, so leave as zero)
  uint8_t payload[7] = { 0x56, 00, 00, 00, 00, 0xF0, 0xAA };

  // Things are going to get strange in here.  Alexa sends the RGB values tweaked to account for the brightness.
  float scale_factor = float(value) / 256.0;
  for (int i = 0; i < 3; i++) {
    rgbArray[i] = int(rgbArray[i] * scale_factor);
    if (rgbArray[i] > 255) rgbArray[i] = 255;
  };

  for (int i = 0; i < 3; i++) {
    // Sometimes Alexa can send 256 which overflows a uint8_t
    if (rgbArray[i] > 255) rgbArray[i] = 255;
    payload[i + 1] = uint8_t(rgbArray[i]);
  };

  do_write(NimBLEAddress(mac), payload, sizeof(payload));
}

void powerSet(bool turnOn) {
  NimBLEAddress addr = NimBLEAddress(mac);
  uint8_t power = (turnOn == true ? 0x23 : 0x24);
  uint8_t payload[] = { 0xCC, power, 0x33 };
  do_write(addr, payload, sizeof(payload));
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

  // If your device state is changed by any other means (MQTT, physical button,...)
  // you can instruct the library to report the new state to Alexa on next request:
  // fauxmo.setState(ID_YELLOW, true, 255);
}