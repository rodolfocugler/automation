#ifndef TV_SAMSUNG_H
#define TV_SAMSUNG_H

#include <Arduino.h>
#include <IRsend.h>
#include "samsungCodes.h"

int turnLedTVOffCounter = 0;

extern IRsend irsend;

class TvSamsung {
public:
  static void triggerControl(bool state, int redLed, int greenLed, int blueLed, int value) {
    if (value == 254) {
      irsend.sendSAMSUNG(SAMSUNG_ON, 32, 1);
      // turnLedTVOffCounter = 500;
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

  static void controlLedTv() {
    if (turnLedTVOffCounter == 1) {
      Serial.printf("[turnLedTVOffCounter] %d\n", turnLedTVOffCounter);
      turnLedTVOffCounter--;
      irsend.sendNEC(IR_OFF, 32);
    } else if (turnLedTVOffCounter > 0) {
      Serial.printf("[turnLedTVOffCounter] %d\n", turnLedTVOffCounter);
      turnLedTVOffCounter--;
    }
  }
};

#endif  // TV_SAMSUNG_H
