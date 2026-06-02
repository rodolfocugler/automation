#ifndef LOG_H
#define LOG_H

#include <Arduino.h>
#include <PubSubClient.h>

class Log {
public:
  static void begin(unsigned long baudRate = 115200, PubSubClient* client = nullptr, const String& topic = "") {
    Serial.begin(baudRate);
    mqttClient = client;
    mqttTopic = topic;
  }

  // Equivalent to Log::print()
  template<typename T>
  static void print(const T& value) {
    Serial.print(value);
    appendToBuffer(String(value));
  }

  // Equivalent to Log::println()
  template<typename T>
  static void println(const T& value) {
    Serial.println(value);
    appendToBuffer(String(value) + "\n");
    sendBufferToMQTT();
  }

  // Overload for println() with no arguments
  static void println() {
    Serial.println();
    appendToBuffer("\n");
  }

  // Equivalent to Serial.printf()
  template<typename... Args>
  static void printf(const char* format, Args... args) {
    Serial.printf(format, args...);
  }

private:
  static inline PubSubClient* mqttClient = nullptr;
  static inline String mqttTopic = "";
  static inline String buffer = "";

  static void appendToBuffer(const String& text) {
    buffer += text;
  }

  static void sendBufferToMQTT() {
    if (mqttClient && mqttClient->connected() && mqttTopic.length() > 0) {
      mqttClient->publish(mqttTopic.c_str(), buffer.c_str());
    }
    buffer = "";
  }
};

#endif  // LOG_H
