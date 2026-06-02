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
      if (redLed == 253 && greenLed == 0 && blueLed == 0) {
        irsend.sendNEC(IR_R, 32);
      } else if (redLed == 253 && greenLed == 164 && blueLed == 0) {
        irsend.sendNEC(IR_B1, 32);
      } else if (redLed == 252 && greenLed == 253 && blueLed == 0) {
        irsend.sendNEC(IR_B2, 32);
      } else if (redLed == 243 && greenLed == 21 && blueLed == 66) {
        irsend.sendNEC(IR_B3, 32);
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
};

#endif  // LED_TV_H
