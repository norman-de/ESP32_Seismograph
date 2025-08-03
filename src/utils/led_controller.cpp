#include "led_controller.h"

LEDController::LEDController() {
    blinking = false;
    blinkStartTime = 0;
    blinkColor = 0;
    blinkCount = 0;
    currentBlinks = 0;
    lastBlinkTime = 0;
    blinkState = false;
}

void LEDController::begin() {
    FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(&led, 1);
    FastLED.setBrightness(50); // Set to 50% brightness
    off();
}

void LEDController::setColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!blinking) {
        led = CRGB(r, g, b);
        FastLED.show();
    }
}

void LEDController::setColor(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    setColor(r, g, b);
}

void LEDController::blink(uint32_t color, int count) {
    blinking = true;
    blinkColor = color;
    blinkCount = count;
    currentBlinks = 0;
    lastBlinkTime = millis();
    blinkState = true;
    
    // Start with the blink color
    setColor(color);
}

void LEDController::update() {
    if (!blinking) return;
    
    unsigned long currentTime = millis();
    
    if (currentTime - lastBlinkTime >= 250) { // 250ms blink interval
        lastBlinkTime = currentTime;
        blinkState = !blinkState;
        
        if (blinkState) {
            setColor(blinkColor);
        } else {
            off();
            currentBlinks++;
        }
        
        if (currentBlinks >= blinkCount) {
            blinking = false;
            // Return to off state after blinking
            off();
        }
    }
}

void LEDController::off() {
    led = CRGB::Black;
    FastLED.show();
}
