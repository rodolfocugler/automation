#ifndef LED_TV_H
#define LED_TV_H

#include <Arduino.h>
#include <IRsend.h>
#include "necCodes.h"

extern IRsend irsend;

class LedTv {
public:
  static void triggerLedTv(bool state, int redLed, int greenLed, int blueLed) {
    if (state) {
      irsend.sendNEC(IR_ON, 32);
      uint64_t code = getClosestIRCode(redLed, greenLed, blueLed);
      Serial.printf("Sending %llu to Led TV\n", code);
      irsend.sendNEC(code, 32);
    } else {
      irsend.sendNEC(IR_OFF, 32);
    }
  }

private:
  static uint64_t getClosestIRCode(int red, int green, int blue) {
    int minDistance = 256 * 256 * 3;
    uint64_t closestCode = 0;

    for (int i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
      int dr = red - colors[i].r;
      int dg = green - colors[i].g;
      int db = blue - colors[i].b;
      int distance = dr * dr + dg * dg + db * db;
      Serial.printf("Testing color %d -> RGB(%d,%d,%d) | distance = %d | code = %llu\n", i, colors[i].r, colors[i].g, colors[i].b, distance, colors[i].code);

      if (distance < minDistance) {
        minDistance = distance;
        closestCode = colors[i].code;
      }
    }

    return closestCode;
  }
};

#endif  // LED_TV_H
