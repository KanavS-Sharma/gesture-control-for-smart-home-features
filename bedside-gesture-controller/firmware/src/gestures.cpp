// ============================================================
//  gestures.cpp  —  PAJ7620U2 I²C gesture sensor driver
//  Full register-level implementation
// ============================================================

#include "gestures.h"
#include "config.h"
#include <Arduino.h>

// ── Register map (PAJ7620U2 datasheet §5) ───────────────────
static const uint8_t PAJ7620_BANK_SEL_REG  = 0xEF;
static const uint8_t PAJ7620_GESTURE_OUT_L = 0x43;
static const uint8_t PAJ7620_GESTURE_OUT_H = 0x44;
static const uint8_t PAJ7620_INT_FLAG_L    = 0x4B;
static const uint8_t PAJ7620_INT_FLAG_H    = 0x4C;

// Initialization sequence (bank 0 → bank 1 → back to 0)
// Extracted from official Pixart reference code
static const uint8_t PAJ7620_INIT[][2] = {
    // {reg, value}
    {0xEF, 0x00}, // Select bank 0
    {0x37, 0x07}, {0x38, 0x17}, {0x39, 0x06}, {0x42, 0x01},
    {0x46, 0x2D}, {0x47, 0x0F}, {0x48, 0x3C}, {0x49, 0x00},
    {0x4A, 0x1E}, {0x4C, 0x20}, {0x51, 0x10}, {0x5E, 0x10},
    {0x60, 0x27}, {0x80, 0x42}, {0x81, 0x44}, {0x82, 0x04},
    {0x8B, 0x01}, {0x90, 0x06}, {0x95, 0x0A}, {0x96, 0x0C},
    {0x97, 0x05}, {0x9A, 0x14}, {0x9C, 0x3F}, {0xA5, 0x19},
    {0xCC, 0x19}, {0xCD, 0x0B}, {0xCE, 0x13}, {0xCF, 0x64},
    {0xD0, 0x21},
    {0xEF, 0x01}, // Select bank 1
    {0x02, 0x0F}, {0x03, 0x10}, {0x04, 0x02}, {0x25, 0x01},
    {0x27, 0x39}, {0x28, 0x7F}, {0x29, 0x08}, {0x3E, 0xFF},
    {0x5E, 0x3D}, {0x65, 0x96}, {0x67, 0x97}, {0x69, 0xCD},
    {0x6A, 0x01}, {0x6D, 0x2C}, {0x6E, 0x01}, {0x72, 0x01},
    {0x73, 0x35}, {0x74, 0x00}, {0x77, 0x54},
    {0xEF, 0x00}, // Back to bank 0
};
static const uint8_t PAJ7620_INIT_COUNT =
    sizeof(PAJ7620_INIT) / sizeof(PAJ7620_INIT[0]);

// ────────────────────────────────────────────────────────────

PAJ7620::PAJ7620(TwoWire& wire) : _wire(wire) {}

bool PAJ7620::begin() {
    _wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    delay(5);

    // Verify device is present
    _wire.beginTransmission(PAJ7620_ADDR);
    if (_wire.endTransmission() != 0) {
        Serial.println("[GESTURE] PAJ7620 not found at 0x73!");
        return false;
    }

    // Run initialization sequence
    if (!_init()) {
        Serial.println("[GESTURE] PAJ7620 init failed!");
        return false;
    }

    Serial.println("[GESTURE] PAJ7620U2 initialized OK");
    return true;
}

bool PAJ7620::_init() {
    for (uint8_t i = 0; i < PAJ7620_INIT_COUNT; i++) {
        if (!_writeReg(PAJ7620_INIT[i][0], PAJ7620_INIT[i][1])) {
            return false;
        }
        delayMicroseconds(100);
    }
    // Ensure we end in bank 0
    _selectBank(0);
    delay(10);
    return true;
}

void PAJ7620::setInterruptPin(int pin) {
    _intPin = pin;
    pinMode(_intPin, INPUT_PULLUP);
}

bool PAJ7620::isGestureReady() {
    if (_intPin < 0) return true;   // No interrupt pin — always poll
    return digitalRead(_intPin) == LOW;
}

Gesture PAJ7620::readGesture() {
    _selectBank(0);

    // Clear interrupt flags first
    _readReg(PAJ7620_INT_FLAG_L);
    _readReg(PAJ7620_INT_FLAG_H);

    // Read gesture output registers
    uint8_t gesL = _readReg(PAJ7620_GESTURE_OUT_L);
    uint8_t gesH = _readReg(PAJ7620_GESTURE_OUT_H);
    uint16_t ges = ((uint16_t)gesH << 8) | gesL;

    if (ges == 0) return Gesture::NONE;

    // Priority decode (only return one gesture at a time)
    if (ges & (uint16_t)Gesture::FORWARD)           return Gesture::FORWARD;
    if (ges & (uint16_t)Gesture::CLOCKWISE)         return Gesture::CLOCKWISE;
    if (ges & (uint16_t)Gesture::COUNTER_CLOCKWISE) return Gesture::COUNTER_CLOCKWISE;
    if (ges & (uint16_t)Gesture::WAVE)              return Gesture::WAVE;
    if (ges & (uint16_t)Gesture::RIGHT)             return Gesture::RIGHT;
    if (ges & (uint16_t)Gesture::LEFT)              return Gesture::LEFT;
    if (ges & (uint16_t)Gesture::UP)                return Gesture::UP;
    if (ges & (uint16_t)Gesture::DOWN)              return Gesture::DOWN;
    if (ges & (uint16_t)Gesture::BACKWARD)          return Gesture::BACKWARD;

    return Gesture::NONE;
}

bool PAJ7620::_selectBank(uint8_t bank) {
    return _writeReg(PAJ7620_BANK_SEL_REG, bank);
}

bool PAJ7620::_writeReg(uint8_t reg, uint8_t val) {
    _wire.beginTransmission(PAJ7620_ADDR);
    _wire.write(reg);
    _wire.write(val);
    return _wire.endTransmission() == 0;
}

uint8_t PAJ7620::_readReg(uint8_t reg) {
    _wire.beginTransmission(PAJ7620_ADDR);
    _wire.write(reg);
    _wire.endTransmission(false);
    _wire.requestFrom((uint8_t)PAJ7620_ADDR, (uint8_t)1);
    if (_wire.available()) return _wire.read();
    return 0;
}

const char* gestureToString(Gesture g) {
    switch (g) {
        case Gesture::RIGHT:             return "RIGHT";
        case Gesture::LEFT:              return "LEFT";
        case Gesture::UP:                return "UP";
        case Gesture::DOWN:              return "DOWN";
        case Gesture::FORWARD:           return "FORWARD (Wave)";
        case Gesture::BACKWARD:          return "BACKWARD";
        case Gesture::CLOCKWISE:         return "CLOCKWISE";
        case Gesture::COUNTER_CLOCKWISE: return "COUNTER_CLOCKWISE";
        case Gesture::WAVE:              return "WAVE";
        default:                         return "NONE";
    }
}
