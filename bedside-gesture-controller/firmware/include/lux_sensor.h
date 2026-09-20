#pragma once
// ============================================================
//  lux_sensor.h  —  BH1750 ambient light sensor driver
// ============================================================

#include <Arduino.h>
#include <Wire.h>

// BH1750 I²C address (ADDR pin = GND → 0x23, ADDR pin = VCC → 0x5C)
#define BH1750_ADDR  0x23

// Measurement modes
enum class BH1750Mode : uint8_t {
    CONTINUOUS_HIGH_RES  = 0x10,   // 1 lx resolution, 120 ms
    CONTINUOUS_HIGH_RES2 = 0x11,   // 0.5 lx resolution, 120 ms
    CONTINUOUS_LOW_RES   = 0x13,   // 4 lx resolution, 16 ms
    ONE_TIME_HIGH_RES    = 0x20,
    ONE_TIME_LOW_RES     = 0x23,
};

class BH1750Sensor {
public:
    BH1750Sensor(TwoWire& wire = Wire);

    // Initialize — returns true on success
    bool begin(BH1750Mode mode = BH1750Mode::CONTINUOUS_HIGH_RES);

    // Read lux — blocks until measurement ready
    float readLux();

    // Map lux to LED brightness (LED_BRIGHTNESS_MIN – LED_BRIGHTNESS_MAX)
    uint8_t luxToBrightness(float lux);

private:
    TwoWire&    _wire;
    BH1750Mode  _mode;
    uint8_t     _addr = BH1750_ADDR;
};
