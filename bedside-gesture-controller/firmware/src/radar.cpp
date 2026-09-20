// ============================================================
//  radar.cpp  —  LD2410C mmWave radar presence sensor driver
//  Protocol: UART 256000 baud, binary frame format
// ============================================================

#include "radar.h"
#include "config.h"
#include <Arduino.h>

// ── LD2410C Frame format ─────────────────────────────────────
// Preamble: F4 F3 F2 F1
// Data length: 2 bytes LE
// Data type: 02 (basic), 01 (engineering)
// Data: variable
// Tail: F8 F7 F6 F5

static const uint8_t FRAME_HEADER[] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t FRAME_TAIL[]   = {0xF8, 0xF7, 0xF6, 0xF5};

// Enable engineering mode command
static const uint8_t CMD_OPEN_ENG[]  = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x62, 0x00, 0x04, 0x03, 0x02, 0x01};
static const uint8_t CMD_CLOSE_ENG[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x63, 0x00, 0x04, 0x03, 0x02, 0x01};

// ────────────────────────────────────────────────────────────

LD2410C::LD2410C(HardwareSerial& serial) : _serial(serial) {
    _data.state        = PresenceState::ABSENT;
    _data.movingTarget = 0;
    _data.staticTarget = 0;
    _data.detectionDist = 0;
}

bool LD2410C::begin(int rxPin, int txPin, uint32_t baud) {
    _serial.begin(baud, SERIAL_8N1, rxPin, txPin);
    delay(100);
    // Flush any stale data
    while (_serial.available()) _serial.read();
    Serial.println("[RADAR] LD2410C UART initialized");
    return true;
}

bool LD2410C::update() {
    // Read all available bytes into rolling buffer
    while (_serial.available()) {
        uint8_t b = _serial.read();
        _buf[_bufIdx++] = b;

        // Buffer overflow guard
        if (_bufIdx >= sizeof(_buf)) {
            _bufIdx = 0;
        }

        // Check for frame header at start of buffer
        if (_bufIdx >= 4) {
            // Look for header
            if (_buf[0] == 0xF4 && _buf[1] == 0xF3 &&
                _buf[2] == 0xF2 && _buf[3] == 0xF1) {

                // Need at least header(4) + len(2) + datatype(1) + min_data + tail(4)
                if (_bufIdx >= 10) {
                    uint16_t dataLen = _buf[4] | (_buf[5] << 8);
                    uint16_t totalLen = 4 + 2 + dataLen + 4;

                    if (_bufIdx >= totalLen) {
                        // Check tail
                        if (_buf[totalLen - 4] == 0xF8 &&
                            _buf[totalLen - 3] == 0xF7 &&
                            _buf[totalLen - 2] == 0xF6 &&
                            _buf[totalLen - 1] == 0xF5) {

                            bool parsed = _parseFrame(_buf, totalLen);
                            // Shift remaining bytes
                            uint8_t remaining = _bufIdx - totalLen;
                            memmove(_buf, _buf + totalLen, remaining);
                            _bufIdx = remaining;
                            if (parsed) return true;
                        }
                    }
                }
            } else {
                // Not a valid header start — shift by 1
                memmove(_buf, _buf + 1, _bufIdx - 1);
                _bufIdx--;
            }
        }
    }
    return false;
}

bool LD2410C::_parseFrame(uint8_t* frame, uint8_t len) {
    // frame[6] = data type: 0x02 = basic report
    if (frame[6] != 0x02) return false;

    // frame[7] = head: 0xAA
    if (frame[7] != 0xAA) return false;

    // frame[8] = target state:
    //   0x00 = no target
    //   0x01 = moving target
    //   0x02 = static target
    //   0x03 = both
    uint8_t targetState = frame[8];

    // Moving target energy: frame[9..10] = distance cm (LE), frame[11] = energy
    uint16_t movingDist   = frame[9]  | (frame[10] << 8);
    uint8_t  movingEnergy = frame[11];

    // Static target energy: frame[12..13] = distance, frame[14] = energy
    uint16_t staticDist   = frame[12] | (frame[13] << 8);
    uint8_t  staticEnergy = frame[14];

    // Detection distance: frame[15..16]
    uint16_t detDist = frame[15] | (frame[16] << 8);

    switch (targetState) {
        case 0x00: _data.state = PresenceState::ABSENT;  break;
        case 0x01: _data.state = PresenceState::MOVING;  break;
        case 0x02: _data.state = PresenceState::STATIC;  break;
        case 0x03: _data.state = PresenceState::BOTH;    break;
        default:   _data.state = PresenceState::ABSENT;  break;
    }

    _data.movingTarget   = movingEnergy;
    _data.staticTarget   = staticEnergy;
    _data.detectionDist  = detDist;

    return true;
}

bool LD2410C::isPresent() const {
    return _data.state != PresenceState::ABSENT;
}

bool LD2410C::sendCommand(const uint8_t* cmd, size_t len) {
    _serial.write(cmd, len);
    delay(50);
    return true;
}
