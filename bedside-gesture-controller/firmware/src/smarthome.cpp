// ============================================================
//  smarthome.cpp  —  MQTT + Home Assistant integration
// ============================================================

#include "smarthome.h"
#include "config.h"
#include "led_ring.h"
#include <Arduino.h>
#include <ArduinoJson.h>

// ── Global state ──────────────────────────────────────────────
SmartHomeClient gSmartHome;
ControlMode     gControlMode = ControlMode::SPOTIFY;

// ── Mode helpers ──────────────────────────────────────────────
ControlMode nextMode(ControlMode m) {
    return (ControlMode)(((uint8_t)m + 1) % (uint8_t)ControlMode::MODE_COUNT);
}
ControlMode prevMode(ControlMode m) {
    return (ControlMode)(((uint8_t)m + (uint8_t)ControlMode::MODE_COUNT - 1)
                         % (uint8_t)ControlMode::MODE_COUNT);
}

uint32_t modeColor(ControlMode m) {
    switch (m) {
        case ControlMode::SPOTIFY:     return MODE_COLOR_SPOTIFY;
        case ControlMode::CURTAINS:    return MODE_COLOR_CURTAINS;
        case ControlMode::SMART_LIGHT: return MODE_COLOR_LIGHT;
        case ControlMode::LED_CONTROL: return MODE_COLOR_LED_CTRL;
        default: return 0xFFFFFF;
    }
}

const char* modeName(ControlMode m) {
    switch (m) {
        case ControlMode::SPOTIFY:     return "spotify";
        case ControlMode::CURTAINS:    return "curtains";
        case ControlMode::SMART_LIGHT: return "smart_light";
        case ControlMode::LED_CONTROL: return "led_control";
        default: return "unknown";
    }
}

// ── MQTT ──────────────────────────────────────────────────────
SmartHomeClient::SmartHomeClient() : _mqtt(_wifiClient) {}

void SmartHomeClient::begin() {
#if MQTT_ENABLED
    _mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    _mqtt.setCallback(_onMessage);
    _mqtt.setKeepAlive(60);
    _mqtt.setBufferSize(512);
    _connect();
    Serial.println("[MQTT] SmartHome client initialized");
#endif
}

void SmartHomeClient::update() {
#if MQTT_ENABLED
    if (!_mqtt.connected()) {
        static unsigned long lastRetry = 0;
        if (millis() - lastRetry > 5000) {
            lastRetry = millis();
            _connect();
        }
    }
    _mqtt.loop();
#endif
}

bool SmartHomeClient::_connect() {
    Serial.printf("[MQTT] Connecting to %s:%d...\n", MQTT_BROKER, MQTT_PORT);
    bool ok = _mqtt.connect(
        MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
        MQTT_TOPIC_PRESENCE, 1, true, "absent"
    );
    if (ok) {
        Serial.println("[MQTT] Connected!");
        _mqtt.subscribe(MQTT_TOPIC_LED_SET);
        _mqtt.subscribe(MQTT_TOPIC_REBOOT);
        _mqtt.publish(MQTT_TOPIC_PRESENCE, "present", true);
    } else {
        Serial.printf("[MQTT] Failed, rc=%d\n", _mqtt.state());
    }
    return ok;
}

void SmartHomeClient::_onMessage(char* topic, byte* payload, unsigned int length) {
    String topicStr(topic);
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

    if (topicStr == MQTT_TOPIC_REBOOT) {
        Serial.println("[MQTT] Reboot commanded");
        delay(500); ESP.restart();
    }

    if (topicStr == MQTT_TOPIC_LED_SET) {
        JsonDocument doc;
        if (deserializeJson(doc, msg) == DeserializationError::Ok) {
            if (doc["brightness"].is<int>())
                gLEDRing.setBrightness(doc["brightness"].as<uint8_t>());
        }
    }
}

// ── Telemetry ─────────────────────────────────────────────────
void SmartHomeClient::publishPresence(bool present) {
#if MQTT_ENABLED
    if (!isConnected()) return;
    _mqtt.publish(MQTT_TOPIC_PRESENCE, present ? "present" : "absent", true);
#endif
}

void SmartHomeClient::publishLux(float lux) {
#if MQTT_ENABLED
    if (!isConnected()) return;
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f", lux);
    _mqtt.publish(MQTT_TOPIC_LUX, buf);
#endif
}

void SmartHomeClient::publishMode(ControlMode mode) {
#if MQTT_ENABLED
    if (!isConnected()) return;
    _mqtt.publish(MQTT_TOPIC_MODE, modeName(mode));
#endif
}

void SmartHomeClient::publishGesture(const char* gesture) {
#if MQTT_ENABLED
    if (!isConnected()) return;
    _mqtt.publish(MQTT_TOPIC_GESTURE, gesture);
#endif
}

void SmartHomeClient::publishVolume(uint8_t pct) {
#if MQTT_ENABLED
    if (!isConnected()) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", pct);
    _mqtt.publish(MQTT_TOPIC_VOLUME, buf);
#endif
}

void SmartHomeClient::publishCurtainPct(uint8_t pct) {
#if MQTT_ENABLED
    if (!isConnected()) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", pct);
    _mqtt.publish(MQTT_TOPIC_CURTAIN_PCT, buf);
#endif
}

// ── Curtain control ───────────────────────────────────────────
void SmartHomeClient::curtainFullOpen() {
#if MQTT_ENABLED
    _mqtt.publish(HA_CURTAIN_OPEN_FULL, "OPEN");
#endif
    Serial.println("[SMARTHOME] Curtain → FULLY OPEN");
}

void SmartHomeClient::curtainFullClose() {
#if MQTT_ENABLED
    _mqtt.publish(HA_CURTAIN_CLOSE_FULL, "CLOSE");
#endif
    Serial.println("[SMARTHOME] Curtain → FULLY CLOSE");
}

void SmartHomeClient::curtainSetPercent(uint8_t pct) {
    pct = constrain(pct, 0, 100);
#if MQTT_ENABLED
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", pct);
    _mqtt.publish(HA_CURTAIN_SET_PCT, buf);
#endif
    Serial.printf("[SMARTHOME] Curtain → %d%%\n", pct);
}

// ── Light control ─────────────────────────────────────────────
void SmartHomeClient::lightOn() {
    _lightState = true;
#if MQTT_ENABLED
    _mqtt.publish(HA_LIGHT_ON, "1");
#endif
    Serial.println("[SMARTHOME] Light → ON");
}

void SmartHomeClient::lightOff() {
    _lightState = false;
#if MQTT_ENABLED
    _mqtt.publish(HA_LIGHT_OFF, "1");
#endif
    Serial.println("[SMARTHOME] Light → OFF");
}

void SmartHomeClient::lightToggle() {
    _lightState ? lightOff() : lightOn();
}

bool SmartHomeClient::isConnected() const {
#if MQTT_ENABLED
    return _mqtt.connected();
#else
    return false;
#endif
}
