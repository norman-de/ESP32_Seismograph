#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"

class LEDController {
private:
    CRGB led;
    bool blinking;
    unsigned long blinkStartTime;
    uint32_t blinkColor;
    int blinkCount;
    int currentBlinks;
    unsigned long lastBlinkTime;
    bool blinkState;

public:
    LEDController();
    void begin();
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void setColor(uint32_t color);
    void blink(uint32_t color, int count);
    void update();
    void off();
};

#endif // LED_CONTROLLER_H
