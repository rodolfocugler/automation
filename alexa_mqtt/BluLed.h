// DrinkeiroService.h
#ifndef BLU_LED_H
#define BLU_LED_H

#include "nimbletriones.h"
#include <NimBLEDevice.h>

// Triones service specifics
const NimBLEUUID NOTIFY_SERVICE("FFD0");
const NimBLEUUID NOTIFY_CHAR("FFD4");
const NimBLEUUID MAIN_SERVICE("FFD5");
const NimBLEUUID WRITE_CHAR("FFD9");
const std::string mac = "53:02:02:00:04:BB";

static BLEClientCallbacks clientCB;

class BluLed {
public:
  static void setup() {
    // Setup Nimble
    Serial.println("Starting NimBLE Client");
    NimBLEDevice::setScanDuplicateCacheSize(10);
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // max power
    NimBLEDevice::setMTU(23);                // This is what the lights ask for, so set up front. I think this made it more reliable to connect first time.
  }

  static void triggerLedCommand(bool state, int redLed, int greenLed, int blueLed, int value) {
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

private:
  static void powerSet(bool turnOn) {
    NimBLEAddress addr = NimBLEAddress(mac);
    uint8_t power = (turnOn == true ? 0x23 : 0x24);
    uint8_t payload[] = { 0xCC, power, 0x33 };
    do_write(addr, payload, sizeof(payload));
  }

  static bool do_write(NimBLEAddress bleAddr, const uint8_t* payload, size_t length) {
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
          pClient->disconnect();
          NimBLEDevice::deleteClient(pClient);
        };
      } else Serial.println("WRITE: Couldnt write");
    } else {
      Serial.println("Failed to write data.");
    }
    return true;
  }
};

#endif
