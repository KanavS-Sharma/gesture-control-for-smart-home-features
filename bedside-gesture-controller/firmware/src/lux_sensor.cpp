// ============================================================
//  lux_sensor.cpp  —  BH1750 I²C ambient light sensor driver
// ============================================================

#include "lux_sensor.h"
#include "config.h"
#include <Arduino.h>

BH1750Sensor::BH1750Sensor(TwoWire& wire) : _wire(wire) {}

bool BH1750Sensor::begin(BH1750Mode mode) {
    _mode = mode;

    // Power on
    _wire.beginTransmission(_addr);
    _wire.write(0x01);  // Power on command
    if (_wire.endTransmission() != 0) {
        Serial.println("[LUX] BH1750 not found!");
        return false;
    }

    delay(10);

    // Set measurement mode
    _wire.beginTransmission(_addr);
    _wire.write((uint8_t)_mode);
    _wire.endTransmission();

    delay(180);  // Wait for first measurement (120 ms high-res mode)

    Serial.println("[LUX] BH1750 initialized OK");
    return true;
}

float BH1750Sensor::readLux() {
    _wire.requestFrom(_addr, (uint8_t)2);

    if (_wire.available() < 2) {
        return -1.0f;   // Error
    }

    uint8_t high = _wire.read();
    uint8_t low  = _wire.read();
    uint16_t raw = ((uint16_t)high << 8) | low;

    // Conversion factor: divide by 1.2 (from BH1750 datasheet)
    float lux = raw / 1.2f;

    // For high-res mode 2 (0.5 lx resolution): divide by 2 again
    if (_mode == BH1750Mode::CONTINUOUS_HIGH_RES2 ||
        _mode == BH1750Mode::ONE_TIME_HIGH_RES) {
        lux /= 2.0f;
    }

    return lux;
}

uint8_t BH1750Sensor::luxToBrightness(float lux) {
    if (lux < 0) return LED_BRIGHTNESS_MIN;

    // Clamp
    if (lux <= LUX_DARK_THRESHOLD)   return LED_BRIGHTNESS_MIN;
    if (lux >= LUX_BRIGHT_THRESHOLD) return LED_BRIGHTNESS_MAX;

    // Linear interpolation between dark and bright thresholds
    float ratio = (lux - LUX_DARK_THRESHOLD) /
                  (float)(LUX_BRIGHT_THRESHOLD - LUX_DARK_THRESHOLD);

    uint8_t brightness = LED_BRIGHTNESS_MIN +
                         (uint8_t)(ratio * (LED_BRIGHTNESS_MAX - LED_BRIGHTNESS_MIN));
    return brightness;
}
