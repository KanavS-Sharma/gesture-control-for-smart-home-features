#pragma once
// ============================================================
//  led_ring.h  —  WS2812B 16-pixel ring animations
// ============================================================

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"

enum class LedAnimation {
    NONE_ANIM = -1,     // Sentinel
    OFF,
    BREATHE,            // Slow color breathe — idle for each mode
    RAINBOW_SPIN,       // Boot animation
    CHASE_CW,           // Clockwise pixel chase
    CHASE_CCW,          // Counter-clockwise pixel chase
    FLASH,              // Quick flashes (play/pause)
    LEVEL_METER,        // N out of 16 LEDs lit (volume/curtain%)
    MODE_SWITCH,        // Brief color wipe when switching modes
    ERROR_BLINK,        // Fast red blink
    BOOT,               // Rainbow boot
    SOLID,              // Solid color (used briefly)
};

class LEDRing {
public:
    LEDRing();

    // Initialize FastLED
    void begin();

    // Set idle animation + color (persists until changed)
    void setAnimation(LedAnimation anim, CRGB color = CRGB::White);

    // Set brightness (0–255) — call from lux task
    void setBrightness(uint8_t b);
    uint8_t getBrightness() const { return _brightness; }

    // Show a level meter: percentage 0–100 → N LEDs lit
    // Color = accent color for lit pixels
    // Dims the rest instead of blanking them (looks premium)
    void showLevelMeter(uint8_t pct, CRGB color);

    // One-shot gesture feedback (plays then returns to idle)
    void triggerFeedback(LedAnimation anim, CRGB color, uint32_t durationMs = 700);

    // Call every ~20 ms in LED task
    void update();

    // Mode switch wipe animation
    void modeSwitch(CRGB newColor);

    void off();

    CRGB leds[LED_COUNT];

private:
    LedAnimation _idleAnim      = LedAnimation::OFF;
    CRGB         _idleColor     = CRGB::Black;

    LedAnimation _feedbackAnim  = LedAnimation::NONE_ANIM;
    CRGB         _feedbackColor = CRGB::White;
    uint32_t     _feedbackStart = 0;
    uint32_t     _feedbackDur   = 0;

    // Level meter state (shown persistently while rotating)
    bool     _showingLevel  = false;
    uint8_t  _levelPct      = 0;
    CRGB     _levelColor    = CRGB::White;
    uint32_t _levelExpiry   = 0;    // Auto-clears after 2s of no rotation

    uint8_t  _brightness    = 128;
    uint32_t _step          = 0;    // Animation tick counter

    // Renderers
    void _renderBreathe(CRGB color);
    void _renderChase(bool cw, CRGB color);
    void _renderFlash(CRGB color);
    void _renderLevelMeter(uint8_t pct, CRGB color);
    void _renderErrorBlink();
    void _renderRainbow();
};

extern LEDRing gLEDRing;
