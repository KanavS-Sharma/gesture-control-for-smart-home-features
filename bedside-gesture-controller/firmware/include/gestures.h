#pragma once
// ============================================================
//  gestures.h  —  PAJ7620U2 gesture sensor driver
// ============================================================

#include <Arduino.h>
#include <Wire.h>

// PAJ7620U2 I²C address
#define PAJ7620_ADDR  0x73

// Gesture result codes (from PAJ7620 datasheet)
enum class Gesture : uint16_t {
    NONE             = 0x0000,
    RIGHT            = 0x0001,
    LEFT             = 0x0002,
    UP               = 0x0004,
    DOWN             = 0x0008,
    FORWARD          = 0x0010,  // Wave / push toward sensor
    BACKWARD         = 0x0020,
    CLOCKWISE        = 0x0040,
    COUNTER_CLOCKWISE= 0x0080,
    WAVE             = 0x0100,
};

// Human-readable names for logging
const char* gestureToString(Gesture g);

class PAJ7620 {
public:
    PAJ7620(TwoWire& wire = Wire);

    // Initialize sensor — returns true on success
    bool begin();

    // Read current gesture — call frequently or use interrupt
    Gesture readGesture();

    // Set interrupt pin (optional)
    void setInterruptPin(int pin);

    // Check if gesture is ready via interrupt pin
    bool isGestureReady();

private:
    TwoWire& _wire;
    int      _intPin = -1;

    bool     _writeReg(uint8_t reg, uint8_t val);
    uint8_t  _readReg(uint8_t reg);
    bool     _selectBank(uint8_t bank);
    bool     _init();
};
