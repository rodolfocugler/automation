#ifndef TV_SAMSUNG_H
#define TV_SAMSUNG_H

#include <Arduino.h>
#include <IRsend.h>
#include "samsungCodes.h"

#define POWER_ACTION "setPowerState"
#define MEDIA_CONTROL_ACTION "mediaControl"
#define MUTE_ACTION "setMute"
#define VOLUME_CONTROL_ACTION "setVolume"


#define ON_COMMAND "On"
#define OFF_COMMAND "Off"
#define NETFLIX_COMMAND "Netflix"
#define YOUTUBE_COMMAND "Youtube"
#define EXIT_COMMAND "Exit"
#define OK_COMMAND "Ok"
#define PAUSE_COMMAND "Pause"
#define PLAY_COMMAND "Play"

int turnLedTVOffCounter = 0;

extern IRsend irsend;

class TvSamsung {
public:
  static void triggerControl(const char* action, const char* value) {
    if (strcmp(action, POWER_ACTION) == 0) {
      if (strcmp(value, ON_COMMAND) == 0) {
        Serial.println("Sending SAMSUNG_ON");
        irsend.sendSAMSUNG(SAMSUNG_ON, 32, 1);
      } else if (strcmp(value, OFF_COMMAND) == 0) {
        Serial.println("Sending SAMSUNG_OFF");
        irsend.sendSAMSUNG(SAMSUNG_OFF, 32, 1);
      }
    } else if (strcmp(action, MEDIA_CONTROL_ACTION) == 0) {
      if (strcmp(value, NETFLIX_COMMAND) == 0) {
        Serial.println("Sending SAMSUNG_NETFLIX");
        irsend.sendSAMSUNG(SAMSUNG_NETFLIX, 32, 1);
      } else if (strcmp(value, EXIT_COMMAND) == 0) {
        Serial.println("Sending SAMSUNG_EXIT");
        irsend.sendSAMSUNG(SAMSUNG_EXIT, 32, 1);
      } else if (strcmp(value, OK_COMMAND) == 0) {
        Serial.println("Sending SAMSUNG_MIDDLE");
        irsend.sendSAMSUNG(SAMSUNG_MIDDLE, 32, 1);
      } else if (strcmp(value, PLAY_COMMAND) == 0) {
        Serial.println("Sending SAMSUNG_PLAY");
        irsend.sendSAMSUNG(SAMSUNG_PLAY, 32, 1);
      } else if (strcmp(value, PAUSE_COMMAND) == 0) {
        Serial.println("Sending SAMSUNG_PAUSE");
        irsend.sendSAMSUNG(SAMSUNG_PAUSE, 32, 1);
      } else if (strcmp(value, YOUTUBE_COMMAND) == 0) {
        Serial.println("Sending SAMSUNG_YOUTUBE");
        irsend.sendSAMSUNG(SAMSUNG_YOUTUBE, 32, 1);
      }
    } else if (strcmp(action, MUTE_ACTION) == 0) {
      Serial.println("Sending SAMSUNG_MUTE");
      irsend.sendSAMSUNG(SAMSUNG_MUTE, 32, 1);
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
