#pragma once
// ============================================================
//  radar.h  —  LD2410C mmWave presence radar driver
// ============================================================

#include <Arduino.h>

// LD2410C UART baud rate (factory default)
#define LD2410C_BAUD  256000

enum class PresenceState {
    ABSENT,
    MOVING,    // Motion target detected
    STATIC,    // Stationary target detected
    BOTH       // Both motion and stationary
};

struct LD2410Data {
    PresenceState state;
    uint8_t       movingTarget;   // 0–100 energy
    uint8_t       staticTarget;   // 0–100 energy
    uint16_t      detectionDist;  // cm
};

class LD2410C {
public:
    LD2410C(HardwareSerial& serial = Serial1);

    // Initialize — call before loop
    bool begin(int rxPin, int txPin, uint32_t baud = LD2410C_BAUD);

    // Poll for new data frame — returns true when fresh data available
    bool update();

    // Latest reading
    LD2410Data getData() const { return _data; }

    // Convenience
    bool isPresent() const;

    // Enter engineering / configuration mode (optional)
    bool sendCommand(const uint8_t* cmd, size_t len);

private:
    HardwareSerial& _serial;
    LD2410Data      _data;

    // Frame parser state machine
    uint8_t  _buf[64];
    uint8_t  _bufIdx = 0;
    bool     _parseFrame(uint8_t* frame, uint8_t len);
};
