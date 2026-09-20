// ============================================================
//  led_ring.cpp  —  WS2812B 16-pixel ring animations
//  Redesigned: level meter, ambient auto-dim, per-mode colors
// ============================================================

#include "led_ring.h"
#include "config.h"
#include <Arduino.h>

LEDRing gLEDRing;

LEDRing::LEDRing() {}

void LEDRing::begin() {
    FastLED.addLeds<WS2812B, PIN_WS2812B_DATA, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(_brightness);
    FastLED.clear();
    FastLED.show();
    Serial.println("[LED] WS2812B ring initialized");
}

void LEDRing::setBrightness(uint8_t b) {
    _brightness = b;
    FastLED.setBrightness(_brightness);
}

void LEDRing::setAnimation(LedAnimation anim, CRGB color) {
    _idleAnim  = anim;
    _idleColor = color;
    _step      = 0;
}

void LEDRing::off() {
    _idleAnim  = LedAnimation::OFF;
    _idleColor = CRGB::Black;
    FastLED.clear();
    FastLED.show();
}

// ── Level Meter (volume / curtain %) ──────────────────────────
// Call this every time a rotation event updates the value.
// Ring auto-reverts to idle after 2 seconds of no updates.
void LEDRing::showLevelMeter(uint8_t pct, CRGB color) {
    _showingLevel = true;
    _levelPct     = constrain(pct, 0, 100);
    _levelColor   = color;
    _levelExpiry  = millis() + 2000;   // Hold for 2 s after last rotation
}

// ── One-shot feedback ─────────────────────────────────────────
void LEDRing::triggerFeedback(LedAnimation anim, CRGB color, uint32_t durationMs) {
    _feedbackAnim  = anim;
    _feedbackColor = color;
    _feedbackStart = millis();
    _feedbackDur   = durationMs;
}

// ── Mode switch wipe ──────────────────────────────────────────
void LEDRing::modeSwitch(CRGB newColor) {
    // Instant wipe fill to new color, then idle takes over
    for (int i = 0; i < LED_COUNT; i++) {
        leds[i] = newColor;
        FastLED.show();
        delay(18);   // Small sweep delay — 18 ms × 16 = ~290 ms total
    }
    setAnimation(LedAnimation::BREATHE, newColor);
}

// ── Main update — called every 20 ms ─────────────────────────
void LEDRing::update() {
    _step++;

    // ── Level meter auto-clear ───────────────────────────────
    if (_showingLevel && millis() >= _levelExpiry) {
        _showingLevel = false;
    }

    // ── Active feedback animation ────────────────────────────
    bool feedbackActive = (_feedbackAnim != LedAnimation::NONE_ANIM) &&
                          (millis() - _feedbackStart < _feedbackDur);

    if (feedbackActive) {
        switch (_feedbackAnim) {
            case LedAnimation::CHASE_CW:
                _renderChase(true, _feedbackColor);  break;
            case LedAnimation::CHASE_CCW:
                _renderChase(false, _feedbackColor); break;
            case LedAnimation::FLASH:
                _renderFlash(_feedbackColor);        break;
            case LedAnimation::ERROR_BLINK:
                _renderErrorBlink();                 break;
            default:
                break;
        }
        FastLED.show();
        return;
    } else if (_feedbackAnim != LedAnimation::NONE_ANIM) {
        _feedbackAnim = LedAnimation::NONE_ANIM;  // Expired — clear
        _step = 0;
    }

    // ── Level meter (takes priority over idle when active) ────
    if (_showingLevel) {
        _renderLevelMeter(_levelPct, _levelColor);
        FastLED.show();
        return;
    }

    // ── Idle animation ────────────────────────────────────────
    switch (_idleAnim) {
        case LedAnimation::OFF:
            FastLED.clear();
            break;

        case LedAnimation::BREATHE:
            _renderBreathe(_idleColor);
            break;

        case LedAnimation::RAINBOW_SPIN:
        case LedAnimation::BOOT:
            _renderRainbow();
            break;

        case LedAnimation::SOLID:
            fill_solid(leds, LED_COUNT, _idleColor);
            break;

        default:
            break;
    }

    FastLED.show();
}

// ── Animation renderers ──────────────────────────────────────

void LEDRing::_renderBreathe(CRGB color) {
    // Period ~4 s at 20 ms update = 200 ticks
    float phase = (float)(_step % 200) / 200.0f;
    // sin goes −1→+1, map to 0.05→1.0 so it never fully blacks out
    float bright = 0.05f + 0.95f * ((sinf(phase * TWO_PI - HALF_PI) + 1.0f) / 2.0f);

    CRGB c;
    c.r = (uint8_t)(color.r * bright);
    c.g = (uint8_t)(color.g * bright);
    c.b = (uint8_t)(color.b * bright);
    fill_solid(leds, LED_COUNT, c);
}

void LEDRing::_renderChase(bool cw, CRGB color) {
    // 4-pixel comet tail
    fill_solid(leds, LED_COUNT, CRGB::Black);
    uint8_t pos = _step % LED_COUNT;
    if (!cw) pos = LED_COUNT - 1 - pos;

    for (int t = 0; t < 4; t++) {
        int idx = cw
            ? (int)(pos - t + LED_COUNT) % LED_COUNT
            : (int)(pos + t)             % LED_COUNT;
        uint8_t fade = 255 - t * 55;
        leds[idx] = CRGB(
            (color.r * fade) >> 8,
            (color.g * fade) >> 8,
            (color.b * fade) >> 8
        );
    }
}

void LEDRing::_renderFlash(CRGB color) {
    // 3 short flashes over duration
    uint32_t elapsed = millis() - _feedbackStart;
    uint8_t  phase   = (elapsed / 120) % 2;  // Toggle every 120 ms
    fill_solid(leds, LED_COUNT, phase == 0 ? color : CRGB::Black);
}

// ── LEVEL METER — the key visual feature ─────────────────────
// pct 0–100 → lights up N out of 16 pixels.
// Lit pixels = mode accent color.
// Unlit pixels = very dim version of the same color (not black —
//               gives a "track" effect like a physical volume knob ring).
void LEDRing::_renderLevelMeter(uint8_t pct, CRGB color) {
    // How many LEDs to light fully
    uint8_t litCount = (uint8_t)((pct / 100.0f) * LED_COUNT + 0.5f);
    litCount = constrain(litCount, 0, LED_COUNT);

    for (int i = 0; i < LED_COUNT; i++) {
        if (i < litCount) {
            // Fully lit — use accent color
            leds[i] = color;
        } else {
            // Dim "track" — 6% brightness of accent color
            leds[i] = CRGB(color.r >> 4, color.g >> 4, color.b >> 4);
        }
    }
}

void LEDRing::_renderErrorBlink() {
    uint32_t elapsed = millis() - _feedbackStart;
    fill_solid(leds, LED_COUNT, (elapsed / 150) % 2 == 0 ? CRGB::Red : CRGB::Black);
}

void LEDRing::_renderRainbow() {
    uint8_t hue = _step * 2;
    for (int i = 0; i < LED_COUNT; i++) {
        leds[i] = CHSV(hue + (i * 256 / LED_COUNT), 255, 255);
    }
}
