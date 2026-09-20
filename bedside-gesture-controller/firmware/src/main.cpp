// ============================================================
//  main.cpp  —  Smart Touchless Bedside Controller
//  ESP32-S3-WROOM-1-N16R8
//
//  Gesture Scheme v2:
//
//  SWIPE UP / DOWN   → Cycle through modes
//  CLOCKWISE         → Increase value (volume / curtain %)
//  COUNTER-CLOCKWISE → Decrease value (volume / curtain %)
//  SWIPE LEFT/RIGHT  → Mode-specific discrete action
//  FORWARD / WAVE    → Mode-specific confirm/toggle
//
//  Modes (cycled UP/DOWN, each has its own LED color):
//  ┌─────────────────┬──────────┬───────────────────────────────────────────┐
//  │ Mode            │ Color    │ Gestures                                  │
//  ├─────────────────┼──────────┼───────────────────────────────────────────┤
//  │ 🎵 SPOTIFY      │ Green    │ L/R = prev/next track                     │
//  │                 │          │ CW/CCW = vol ±5% (ring = level meter)     │
//  │                 │          │ Wave = play/pause                         │
//  ├─────────────────┼──────────┼───────────────────────────────────────────┤
//  │ 🪟 CURTAINS     │ Amber    │ L = fully close, R = fully open           │
//  │                 │          │ CW/CCW = position ±5% (ring = level meter)│
//  │                 │          │ Wave = stop movement                      │
//  ├─────────────────┼──────────┼───────────────────────────────────────────┤
//  │ 💡 SMART LIGHT  │ Purple   │ R = lights ON, L = lights OFF             │
//  │                 │          │ Wave = toggle                             │
//  ├─────────────────┼──────────┼───────────────────────────────────────────┤
//  │ 🔆 LED CONTROL  │ Cyan     │ CW = ring brighter, CCW = ring dimmer     │
//  │                 │          │ Wave = toggle auto-dim (ambient sensor)   │
//  └─────────────────┴──────────┴───────────────────────────────────────────┘
//
//  Ambient auto-dim: BH1750 lux → LED brightness (always active
//  unless overridden in LED_CONTROL mode)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "config.h"
#include "gestures.h"
#include "radar.h"
#include "lux_sensor.h"
#include "led_ring.h"
#include "spotify.h"
#include "smarthome.h"
#include "ota.h"

// ── Hardware objects ─────────────────────────────────────────
PAJ7620      gGestureSensor;
LD2410C      gRadar(Serial1);
BH1750Sensor gLuxSensor;

// ── Web server ────────────────────────────────────────────────
AsyncWebServer gWebServer(80);

// ── System state ──────────────────────────────────────────────
volatile bool          gPresent         = false;
volatile unsigned long gLastPresenceMs  = 0;
volatile bool          gDeviceSleeping  = false;

// Per-mode tracked values (persisted across gestures)
int8_t  gVolume      = 50;    // 0–100, Spotify volume
int8_t  gCurtainPct  = 50;    // 0–100, curtain position
uint8_t gManualBrightness = 128;  // Used in LED_CONTROL mode
bool    gAutoDim     = true;  // True = BH1750 controls brightness

// ── FreeRTOS ─────────────────────────────────────────────────
QueueHandle_t xGestureQueue;

// ── Prototypes ────────────────────────────────────────────────
void setupWifi();
void setupWebServer();
void applyModeSwitch(ControlMode newMode);
void handleGesture(Gesture g);

void gestureTask(void* pv);
void radarTask  (void* pv);
void luxTask    (void* pv);
void ledTask    (void* pv);
void actionTask (void* pv);

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(LOG_BAUD_RATE);
    delay(200);
    Serial.println("\n\n🛏️  Bedside Controller v2 — Booting...");

    // LED ring first — instant visual boot feedback
    gLEDRing.begin();
    gLEDRing.setAnimation(LedAnimation::BOOT);

    // I²C
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);

    // Sensors
    if (!gGestureSensor.begin())
        Serial.println("[MAIN] ⚠ PAJ7620 not found!");
    gGestureSensor.setInterruptPin(PIN_PAJ7620_INT);

    gRadar.begin(PIN_UART1_RX, PIN_UART1_TX, LD2410C_BAUD);

    if (!gLuxSensor.begin())
        Serial.println("[MAIN] ⚠ BH1750 not found!");

    // Wi-Fi
    setupWifi();

    // Spotify
    gSpotify.begin();

    // MQTT
    gSmartHome.begin();

    // Web server
    setupWebServer();

    // FreeRTOS queue — holds up to 10 pending gestures
    xGestureQueue = xQueueCreate(10, sizeof(Gesture));

    // Tasks — split across both cores
    xTaskCreatePinnedToCore(gestureTask, "Gesture", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(radarTask,   "Radar",   4096, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(luxTask,     "Lux",     2048, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(ledTask,     "LED",     4096, nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(actionTask,  "Action",  8192, nullptr, 3, nullptr, 0);

    Serial.println("[MAIN] All systems go! ✅");
    Serial.println("[MAIN] Default mode: 🎵 SPOTIFY (green)");
}

void loop() {
    gSmartHome.update();
    gSpotify.maintainToken();
    delay(10);
}

// ============================================================
//  Wi-Fi Setup
// ============================================================
void setupWifi() {
    Serial.printf("[WIFI] Connecting to %s...\n", WIFI_SSID);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
        if (millis() - t0 > 15000) {
            Serial.println("\n[WIFI] Timeout! Running offline.");
            gLEDRing.triggerFeedback(LedAnimation::ERROR_BLINK, CRGB::Red, 2000);
            return;
        }
    }
    Serial.printf("\n[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());

    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[WIFI] mDNS: http://%s.local/\n", HOSTNAME);
    }
}

// ============================================================
//  Web Server
// ============================================================
void setupWebServer() {
    // Dashboard
    gWebServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        String html;
        html.reserve(2048);
        html = F("<!DOCTYPE html><html><head>"
                 "<meta charset=UTF-8>"
                 "<meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<title>Bedside Controller</title>"
                 "<style>"
                 "body{font-family:'Segoe UI',sans-serif;background:#0d0d0d;color:#eee;"
                 "display:flex;flex-direction:column;align-items:center;padding:1rem;margin:0}"
                 "h1{color:#1DB954;margin:.5rem 0}"
                 ".card{background:#1a1a1a;border-radius:14px;padding:1.2rem 1.5rem;"
                 "margin:.5rem 0;width:100%;max-width:460px;border:1px solid #2a2a2a}"
                 "h2{color:#aaa;font-size:.95rem;text-transform:uppercase;letter-spacing:.08em;margin:0 0 .8rem}"
                 "p{margin:.3rem 0;font-size:.9rem;color:#ccc}"
                 ".ok{color:#1DB954}.err{color:#e74c3c}"
                 "a.btn{display:block;text-align:center;text-decoration:none;padding:.75rem;"
                 "border-radius:10px;margin-top:.7rem;font-weight:600}"
                 ".green{background:#1DB954;color:#fff}.blue{background:#2c7be5;color:#fff}"
                 ".red{background:#c0392b;color:#fff}"
                 "a.btn:hover{opacity:.85}"
                 "</style></head><body>");
        html += F("<h1>🛏️ Bedside Controller</h1>");

        // Status
        html += F("<div class='card'><h2>Status</h2>");
        html += "<p>Wi-Fi: <span class='ok'>" + WiFi.localIP().toString() + "</span></p>";
        html += F("<p>Spotify: <span class='");
        html += gSpotify.isAuthorized() ? F("ok'>✅ Authorized") : F("err'>❌ Not authorized");
        html += F("</span></p><p>MQTT: <span class='");
        html += gSmartHome.isConnected() ? F("ok'>✅ Connected") : F("err'>❌ Disconnected");
        html += F("</span></p><p>Mode: <span class='ok'>");
        html += modeName(gControlMode);
        html += F("</span></p><p>Volume: <span class='ok'>");
        html += String(gVolume) + "%";
        html += F("</span></p><p>Curtain: <span class='ok'>");
        html += String(gCurtainPct) + "%";
        html += F("</span></p></div>");

        // Spotify auth
        if (!gSpotify.isAuthorized()) {
            html += F("<div class='card'><h2>Spotify Auth</h2>"
                      "<a class='btn green' href='/auth'>🔗 Authorize Spotify</a></div>");
        }

        // OTA
        html += F("<div class='card'><h2>Firmware</h2>"
                  "<a class='btn blue' href='/update'>⬆️ OTA Update</a></div>");

        html += F("</body></html>");
        req->send(200, "text/html", html);
    });

    gWebServer.on("/auth", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect(gSpotify.getAuthURL());
    });

    gWebServer.on("/callback", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (req->hasParam("code")) {
            if (gSpotify.exchangeCode(req->getParam("code")->value())) {
                gLEDRing.triggerFeedback(LedAnimation::FLASH,
                    CRGB(0x1D, 0xB9, 0x54), 2000);
                req->send(200, "text/html",
                    "<html><body style='font-family:sans-serif;background:#0d0d0d;color:#eee;"
                    "display:flex;align-items:center;justify-content:center;min-height:100vh'>"
                    "<div style='text-align:center'><h1 style='color:#1DB954'>✅ Spotify Connected!</h1>"
                    "<p><a href='/' style='color:#1DB954'>Back to dashboard</a></p></div></body></html>");
            } else {
                req->send(500, "text/html", "<h1>Token exchange failed.</h1>");
            }
        } else {
            req->send(400, "text/html", "<h1>Authorization denied.</h1>");
        }
    });

    // JSON API
    gWebServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["wifi_ip"]      = WiFi.localIP().toString();
        doc["spotify_ok"]   = gSpotify.isAuthorized();
        doc["mqtt_ok"]      = gSmartHome.isConnected();
        doc["mode"]         = modeName(gControlMode);
        doc["volume"]       = gVolume;
        doc["curtain_pct"]  = gCurtainPct;
        doc["present"]      = gPresent;
        doc["auto_dim"]     = gAutoDim;
        doc["brightness"]   = gLEDRing.getBrightness();
        doc["free_heap"]    = ESP.getFreeHeap();
        doc["uptime_s"]     = millis() / 1000;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    gOTA.begin(&gWebServer);

    gWebServer.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });

    gWebServer.begin();
    Serial.println("[WEB] Server started on :80");
}

// ============================================================
//  MODE SWITCH  —  animate ring then set idle color
// ============================================================
void applyModeSwitch(ControlMode newMode) {
    gControlMode = newMode;

    // Build CRGB from packed hex
    uint32_t c = modeColor(newMode);
    CRGB color((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);

    // Sweeping wipe then breathe
    gLEDRing.modeSwitch(color);  // Blocks ~290 ms for the visual sweep

    gSmartHome.publishMode(newMode);
    Serial.printf("[MODE] Switched → %s\n", modeName(newMode));
}

// ============================================================
//  GESTURE HANDLER — the heart of the system
// ============================================================
void handleGesture(Gesture g) {
    if (gDeviceSleeping) return;
    Serial.printf("[GESTURE] %s  |  Mode: %s\n",
                  gestureToString(g), modeName(gControlMode));

    // ── Global: UP/DOWN swipe = cycle modes ──────────────────
    if (g == Gesture::UP) {
        applyModeSwitch(nextMode(gControlMode));
        return;
    }
    if (g == Gesture::DOWN) {
        applyModeSwitch(prevMode(gControlMode));
        return;
    }

    // ── Build mode color for level meter feedback ─────────────
    uint32_t mc = modeColor(gControlMode);
    CRGB mColor((mc >> 16) & 0xFF, (mc >> 8) & 0xFF, mc & 0xFF);

    // ════════════════════════════════════════════════════════
    //  🎵 SPOTIFY MODE
    // ════════════════════════════════════════════════════════
    if (gControlMode == ControlMode::SPOTIFY) {

        if (!gSpotify.isAuthorized()) {
            Serial.println("[SPOTIFY] Not authorized — visit /auth");
            gLEDRing.triggerFeedback(LedAnimation::ERROR_BLINK, CRGB::Red, 1000);
            return;
        }

        switch (g) {
            // ── Play / Pause ─────────────────────────────────
            case Gesture::FORWARD:
            case Gesture::WAVE:
                if (gSpotify.togglePlayPause()) {
                    gLEDRing.triggerFeedback(LedAnimation::FLASH, CRGB::White, 600);
                    gSmartHome.publishGesture("toggle");
                }
                break;

            // ── Next / Prev track ────────────────────────────
            case Gesture::RIGHT:
                if (gSpotify.nextTrack()) {
                    gLEDRing.triggerFeedback(LedAnimation::CHASE_CW,
                        CRGB(0x1D, 0xB9, 0x54), 700);
                    gSmartHome.publishGesture("next");
                }
                break;

            case Gesture::LEFT:
                if (gSpotify.previousTrack()) {
                    gLEDRing.triggerFeedback(LedAnimation::CHASE_CCW,
                        CRGB(0x1D, 0xB9, 0x54), 700);
                    gSmartHome.publishGesture("prev");
                }
                break;

            // ── Volume — BMW-style rotation ──────────────────
            // CW = louder, CCW = quieter
            // Ring shows current volume as level meter
            case Gesture::CLOCKWISE:
                gVolume = constrain(gVolume + SPOTIFY_ROTATION_STEP, 0, 100);
                gSpotify.setVolume(gVolume);
                gLEDRing.showLevelMeter(gVolume, CRGB(0x1D, 0xB9, 0x54));
                gSmartHome.publishVolume(gVolume);
                Serial.printf("[SPOTIFY] Volume → %d%%\n", gVolume);
                break;

            case Gesture::COUNTER_CLOCKWISE:
                gVolume = constrain(gVolume - SPOTIFY_ROTATION_STEP, 0, 100);
                gSpotify.setVolume(gVolume);
                gLEDRing.showLevelMeter(gVolume, CRGB(0x1D, 0xB9, 0x54));
                gSmartHome.publishVolume(gVolume);
                Serial.printf("[SPOTIFY] Volume → %d%%\n", gVolume);
                break;

            default: break;
        }
    }

    // ════════════════════════════════════════════════════════
    //  🪟 CURTAINS MODE
    // ════════════════════════════════════════════════════════
    else if (gControlMode == ControlMode::CURTAINS) {
        CRGB amber(0xFF, 0xAA, 0x00);

        switch (g) {
            // ── Full open / close ────────────────────────────
            case Gesture::RIGHT:
                gCurtainPct = 100;
                gSmartHome.curtainFullOpen();
                gLEDRing.showLevelMeter(100, amber);
                gSmartHome.publishGesture("curtain_open_full");
                gSmartHome.publishCurtainPct(100);
                break;

            case Gesture::LEFT:
                gCurtainPct = 0;
                gSmartHome.curtainFullClose();
                gLEDRing.showLevelMeter(0, amber);
                gSmartHome.publishGesture("curtain_close_full");
                gSmartHome.publishCurtainPct(0);
                break;

            // ── Precise % — rotation ─────────────────────────
            // CW = open more, CCW = close more
            // Ring shows current position as level meter
            case Gesture::CLOCKWISE:
                gCurtainPct = constrain(gCurtainPct + CURTAIN_ROTATION_STEP, 0, 100);
                gSmartHome.curtainSetPercent(gCurtainPct);
                gLEDRing.showLevelMeter(gCurtainPct, amber);
                gSmartHome.publishCurtainPct(gCurtainPct);
                Serial.printf("[CURTAIN] Position → %d%%\n", gCurtainPct);
                break;

            case Gesture::COUNTER_CLOCKWISE:
                gCurtainPct = constrain(gCurtainPct - CURTAIN_ROTATION_STEP, 0, 100);
                gSmartHome.curtainSetPercent(gCurtainPct);
                gLEDRing.showLevelMeter(gCurtainPct, amber);
                gSmartHome.publishCurtainPct(gCurtainPct);
                Serial.printf("[CURTAIN] Position → %d%%\n", gCurtainPct);
                break;

            // ── Wave = stop movement ─────────────────────────
            case Gesture::FORWARD:
            case Gesture::WAVE:
                // Publish stop command (HA automation can handle this)
                gSmartHome.publishGesture("curtain_stop");
                gLEDRing.triggerFeedback(LedAnimation::FLASH, amber, 400);
                break;

            default: break;
        }
    }

    // ════════════════════════════════════════════════════════
    //  💡 SMART LIGHT MODE
    // ════════════════════════════════════════════════════════
    else if (gControlMode == ControlMode::SMART_LIGHT) {
        CRGB purple(0xAA, 0x44, 0xFF);

        switch (g) {
            case Gesture::RIGHT:
                gSmartHome.lightOn();
                // Quickly fill ring to signal "on"
                gLEDRing.triggerFeedback(LedAnimation::FLASH, purple, 500);
                gSmartHome.publishGesture("light_on");
                break;

            case Gesture::LEFT:
                gSmartHome.lightOff();
                gLEDRing.triggerFeedback(LedAnimation::FLASH, CRGB::Black, 300);
                gSmartHome.publishGesture("light_off");
                break;

            case Gesture::FORWARD:
            case Gesture::WAVE:
                gSmartHome.lightToggle();
                gLEDRing.triggerFeedback(LedAnimation::FLASH, purple, 500);
                gSmartHome.publishGesture("light_toggle");
                break;

            default: break;
        }
    }

    // ════════════════════════════════════════════════════════
    //  🔆 LED CONTROL MODE
    //  Manually control ring brightness OR toggle auto-dim
    // ════════════════════════════════════════════════════════
    else if (gControlMode == ControlMode::LED_CONTROL) {
        CRGB cyan(0x00, 0xCC, 0xFF);

        switch (g) {
            // CW = brighter
            case Gesture::CLOCKWISE:
                gAutoDim = false;   // Manual override
                gManualBrightness = constrain(
                    gManualBrightness + LED_BRIGHTNESS_STEP,
                    LED_BRIGHTNESS_MIN, LED_BRIGHTNESS_MAX);
                gLEDRing.setBrightness(gManualBrightness);
                // Show brightness as level meter
                gLEDRing.showLevelMeter(
                    map(gManualBrightness, LED_BRIGHTNESS_MIN, LED_BRIGHTNESS_MAX, 0, 100),
                    cyan);
                Serial.printf("[LED CTRL] Brightness → %d\n", gManualBrightness);
                break;

            // CCW = dimmer
            case Gesture::COUNTER_CLOCKWISE:
                gAutoDim = false;
                gManualBrightness = constrain(
                    gManualBrightness - LED_BRIGHTNESS_STEP,
                    LED_BRIGHTNESS_MIN, LED_BRIGHTNESS_MAX);
                gLEDRing.setBrightness(gManualBrightness);
                gLEDRing.showLevelMeter(
                    map(gManualBrightness, LED_BRIGHTNESS_MIN, LED_BRIGHTNESS_MAX, 0, 100),
                    cyan);
                Serial.printf("[LED CTRL] Brightness → %d\n", gManualBrightness);
                break;

            // Wave = toggle auto-dim on/off
            case Gesture::FORWARD:
            case Gesture::WAVE:
                gAutoDim = !gAutoDim;
                Serial.printf("[LED CTRL] Auto-dim: %s\n", gAutoDim ? "ON" : "OFF");
                gLEDRing.triggerFeedback(LedAnimation::FLASH,
                    gAutoDim ? CRGB::Green : CRGB::Orange, 600);
                break;

            default: break;
        }
    }
}

// ============================================================
//  FREERTOS TASKS
// ============================================================

// ── Gesture polling — Core 1, highest priority ───────────────
void gestureTask(void* pv) {
    (void)pv;
    Serial.println("[TASK] gestureTask started");

    static Gesture   lastGesture = Gesture::NONE;
    static uint32_t  debounceMs  = 0;
    const  uint32_t  DEBOUNCE    = 400;  // ms

    for (;;) {
        if (!gDeviceSleeping && gGestureSensor.isGestureReady()) {
            Gesture g = gGestureSensor.readGesture();

            if (g != Gesture::NONE) {
                uint32_t now = millis();
                if (g != lastGesture || (now - debounceMs) > DEBOUNCE) {
                    lastGesture = g;
                    debounceMs  = now;
                    xQueueSend(xGestureQueue, &g, 0);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Radar presence — Core 1, medium priority ─────────────────
void radarTask(void* pv) {
    (void)pv;
    Serial.println("[TASK] radarTask started");

    for (;;) {
        if (gRadar.update()) {
            bool detected = gRadar.isPresent();

            if (detected != gPresent) {
                gPresent = detected;
                gSmartHome.publishPresence(gPresent);
                Serial.printf("[RADAR] %s  dist: %d cm\n",
                    detected ? "PRESENT" : "ABSENT",
                    gRadar.getData().detectionDist);
            }

            if (detected) {
                gLastPresenceMs = millis();

                if (gDeviceSleeping) {
                    Serial.println("[RADAR] Wake up!");
                    gDeviceSleeping = false;
                    // Restore mode color
                    uint32_t mc = modeColor(gControlMode);
                    CRGB c((mc>>16)&0xFF, (mc>>8)&0xFF, mc&0xFF);
                    gLEDRing.setAnimation(LedAnimation::BREATHE, c);
                }
            } else {
                if (!gDeviceSleeping &&
                    (millis() - gLastPresenceMs) > SLEEP_TIMEOUT_MS) {
                    Serial.println("[RADAR] Sleep mode");
                    gDeviceSleeping = true;
                    gLEDRing.off();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(RADAR_POLL_INTERVAL_MS));
    }
}

// ── Ambient light — Core 0, low priority ─────────────────────
// Auto-dim: maps lux → LED brightness whenever gAutoDim is true
void luxTask(void* pv) {
    (void)pv;
    Serial.println("[TASK] luxTask started");
    uint8_t publishCounter = 0;

    for (;;) {
        if (!gDeviceSleeping) {
            float lux = gLuxSensor.readLux();

            if (lux >= 0.0f && gAutoDim) {
                uint8_t b = gLuxSensor.luxToBrightness(lux);
                gLEDRing.setBrightness(b);
            }

            // Publish lux every 10 s (10 × 1000 ms ticks)
            if (++publishCounter >= 10) {
                publishCounter = 0;
                if (lux >= 0.0f) {
                    gSmartHome.publishLux(lux);
                    Serial.printf("[LUX] %.1f lx  brightness %d  auto-dim %s\n",
                        lux, gLEDRing.getBrightness(), gAutoDim ? "ON" : "OFF");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ── LED animation — Core 0, medium priority ───────────────────
void ledTask(void* pv) {
    (void)pv;
    Serial.println("[TASK] ledTask started");

    // Boot animation for 3 s
    uint32_t bootEnd = millis() + 3000;
    while (millis() < bootEnd) {
        gLEDRing.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Default idle: Spotify green breathe
    gLEDRing.setAnimation(LedAnimation::BREATHE, CRGB(0x1D, 0xB9, 0x54));

    for (;;) {
        gLEDRing.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ── Action processing — Core 0, medium priority ──────────────
void actionTask(void* pv) {
    (void)pv;
    Serial.println("[TASK] actionTask started");
    Gesture g;

    for (;;) {
        if (xQueueReceive(xGestureQueue, &g, portMAX_DELAY) == pdTRUE) {
            handleGesture(g);
        }
    }
}
