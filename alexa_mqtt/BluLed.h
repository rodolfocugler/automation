#ifndef BLU_LED_H
#define BLU_LED_H

#include <NimBLEDevice.h>

// Triones BLE LED UUIDs
const NimBLEUUID NOTIFY_SERVICE("FFD0");
const NimBLEUUID MAIN_SERVICE("FFD5");
const NimBLEUUID WRITE_CHAR("FFD9");

// BLE MAC address of your Triones LED
const char* LED_MAC = "53:02:02:00:04:BB";
NimBLEAddress bleAddr(LED_MAC, BLE_ADDR_PUBLIC);

class BluLed {
public:
  static void setup() {
    Serial.println("🔧 Initializing NimBLE Client...");
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // max TX power
    NimBLEDevice::setMTU(23);                // required by Triones
    while(!connectToLED(bleAddr)) {
      delay(2000);
    }
  }

  static void sendRGB(bool power, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    if (!connectToLED(bleAddr)) {
      Serial.println("❌ Failed to connect to LED.");
      return;
    }

    // Send ON/OFF command
    sendPowerCommand(power, bleAddr);

    // Scale RGB by brightness (0–255)
    float scale = static_cast<float>(brightness) / 255.0;
    uint8_t scaledR = static_cast<uint8_t>(r * scale);
    uint8_t scaledG = static_cast<uint8_t>(g * scale);
    uint8_t scaledB = static_cast<uint8_t>(b * scale);

    // RGB command: 0x56 R G B 0x00 0xF0 0xAA
    uint8_t payload[7] = {
      0x56, scaledR, scaledG, scaledB, 0x00, 0xF0, 0xAA
    };

    if (!writeToLED(bleAddr, payload, sizeof(payload))) {
      Serial.println("❌ Failed to send RGB data.");
    } else {
      Serial.println("✅ RGB data sent.");
    }
  }

private:
  static bool connectToLED(NimBLEAddress address) {
    NimBLEClient* client = NimBLEDevice::getClientByPeerAddress(address);

    if (client && client->isConnected()) {
      return true;
    }

    if (!client) {
      client = createClient();
    }

    Serial.print("🔌 Connecting to ");
    Serial.println(address.toString().c_str());

    for (int attempt = 0; attempt < 5 && !client->isConnected(); ++attempt) {
      Serial.print("Attempt #");
      Serial.println(attempt + 1);
      if (!client->connect(address)) {
        delay(500);
      }
    }

    if (!client->isConnected()) {
      Serial.println("❌ Connection failed.");
      NimBLEDevice::deleteClient(client);
      return false;
    }

    Serial.println("✅ Connected to LED.");
    return true;
  }

  static NimBLEClient* createClient() {
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setClientCallbacks(nullptr, false);
    client->setConnectionParams(6, 12, 0, 50);
    client->setConnectTimeout(1000);
    return client;
  }

  static bool writeToLED(NimBLEAddress address, const uint8_t* data, size_t length) {
    NimBLEClient* client = NimBLEDevice::getClientByPeerAddress(address);
    if (!client || !client->isConnected()) {
      Serial.println("❌ Client not connected.");
      return false;
    }

    NimBLERemoteService* service = client->getService(MAIN_SERVICE);
    if (!service) {
      Serial.println("❌ Main service not found.");
      return false;
    }

    NimBLERemoteCharacteristic* characteristic = service->getCharacteristic(WRITE_CHAR);
    if (!characteristic || !characteristic->canWrite()) {
      Serial.println("❌ Writable characteristic not found.");
      return false;
    }

    bool result = characteristic->writeValue(data, length, false);
    if (!result) {
      Serial.println("❌ Write failed.");
    }
    // client->disconnect();
    // NimBLEDevice::deleteClient(client);
    return result;
  }

  static void sendPowerCommand(bool on, NimBLEAddress address) {
    uint8_t command = on ? 0x23 : 0x24;
    uint8_t payload[] = { 0xCC, command, 0x33 };

    if (writeToLED(address, payload, sizeof(payload))) {
      Serial.println(on ? "✅ LED turned ON" : "✅ LED turned OFF");
    } else {
      Serial.println("❌ Failed to send power command.");
    }
  }
};

#endif
