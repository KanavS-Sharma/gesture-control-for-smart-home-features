#pragma once
// ============================================================
//  smarthome.h  —  MQTT + Home Assistant integration
// ============================================================

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include "config.h"

// ── Control modes — cycled by UP/DOWN swipe ──────────────────
enum class ControlMode : uint8_t {
    SPOTIFY     = 0,   // 🎵 Green
    CURTAINS    = 1,   // 🪟 Amber
    SMART_LIGHT = 2,   // 💡 Purple
    LED_CONTROL = 3,   // 🔆 Cyan  — control ring brightness directly
    MODE_COUNT  = 4    // Always keep last
};

// Advance / retreat through mode ring
ControlMode nextMode(ControlMode m);
ControlMode prevMode(ControlMode m);

// Returns the idle LED color for this mode (as 24-bit hex)
uint32_t    modeColor(ControlMode m);
const char* modeName (ControlMode m);

class SmartHomeClient {
public:
    SmartHomeClient();

    void begin();
    void update();   // Call in loop — reconnect + MQTT loop

    // Presence / telemetry
    void publishPresence(bool present);
    void publishLux(float lux);
    void publishMode(ControlMode mode);
    void publishGesture(const char* gesture);
    void publishVolume(uint8_t pct);
    void publishCurtainPct(uint8_t pct);

    // ── Curtain control ──────────────────────────────────────
    void curtainFullOpen();
    void curtainFullClose();
    void curtainSetPercent(uint8_t pct);  // 0=closed, 100=open

    // ── Light control ────────────────────────────────────────
    void lightOn();
    void lightOff();
    void lightToggle();

    bool isConnected() const;

private:
    WiFiClient   _wifiClient;
    PubSubClient _mqtt;
    bool         _lightState = false;

    bool  _connect();
    static void _onMessage(char* topic, byte* payload, unsigned int length);
};

extern SmartHomeClient gSmartHome;
extern ControlMode     gControlMode;
