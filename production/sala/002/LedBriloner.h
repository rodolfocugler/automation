//https://github.com/Lucaslhm/Flipper-IRDB/blob/main/LED_Lighting/Briloner/Briloner_leuchten.ir
#ifndef LED_BRILONER_H
#define LED_BRILONER_H

#include <Arduino.h>
#include <IRremote.hpp>
#include "Log.h"

class LedBriloner {
public:
  static void triggerLight(bool state, int redLed, int greenLed, int blueLed, int brightness) {
    IrSender.sendNEC(0x0, 0x45, 0);
    delay(40);
    IrSender.sendNECRepeat();
    Log::printf("Sending code to briloner\n");
  }
};

#endif  // LED_BRILONER_H
