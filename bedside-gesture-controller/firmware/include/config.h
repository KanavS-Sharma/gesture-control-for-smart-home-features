#pragma once
// ============================================================
//  config.h  —  All user-configurable settings
//  Edit this file before flashing!
// ============================================================

// ── Wi-Fi ───────────────────────────────────────────────────
#define WIFI_SSID        "YourWiFiSSID"
#define WIFI_PASSWORD    "YourWiFiPassword"
#define HOSTNAME         "bedside-controller"

// ── Spotify ─────────────────────────────────────────────────
#define SPOTIFY_CLIENT_ID      "your_client_id_here"
#define SPOTIFY_CLIENT_SECRET  "your_client_secret_here"
#define SPOTIFY_REDIRECT_URI   "http://bedside-controller.local/callback"

// Volume step per rotation gesture (1–20)
// Each CW/CCW rotation event changes volume by this many %
#define SPOTIFY_ROTATION_STEP   5

// ── MQTT / Home Assistant ───────────────────────────────────
#define MQTT_ENABLED         true
#define MQTT_BROKER          "192.168.1.100"
#define MQTT_PORT            1883
#define MQTT_USER            "mqttuser"
#define MQTT_PASSWORD        "mqttpassword"
#define MQTT_CLIENT_ID       "bedside_controller"

// MQTT topics — incoming state reports
#define MQTT_TOPIC_GESTURE       "bedside/gesture"
#define MQTT_TOPIC_MODE          "bedside/mode"
#define MQTT_TOPIC_PRESENCE      "bedside/presence"
#define MQTT_TOPIC_LUX           "bedside/lux"
#define MQTT_TOPIC_VOLUME        "bedside/volume"
#define MQTT_TOPIC_CURTAIN_PCT   "bedside/curtain/pct"

// MQTT topics — commands subscribed
#define MQTT_TOPIC_LED_SET       "bedside/led/set"
#define MQTT_TOPIC_REBOOT        "bedside/reboot"

// ── Smart Home MQTT publish ──────────────────────────────────
#define HA_CURTAIN_OPEN_FULL   "home/bedroom/curtain/open"      // Full open
#define HA_CURTAIN_CLOSE_FULL  "home/bedroom/curtain/close"     // Full close
#define HA_CURTAIN_SET_PCT     "home/bedroom/curtain/set_pct"   // Payload: "0"–"100"
#define HA_LIGHT_ON            "home/bedroom/light/on"
#define HA_LIGHT_OFF           "home/bedroom/light/off"
#define HA_LIGHT_BRIGHTER      "home/bedroom/light/brighter"
#define HA_LIGHT_DIMMER        "home/bedroom/light/dimmer"

// Curtain step per rotation gesture (1–20%)
#define CURTAIN_ROTATION_STEP  5

// ── GPIO Pins ────────────────────────────────────────────────
#define PIN_I2C_SDA        8
#define PIN_I2C_SCL        9
#define PIN_UART1_RX       17
#define PIN_UART1_TX       18
#define PIN_WS2812B_DATA   48
#define PIN_PAJ7620_INT    21

// ── LED Ring ─────────────────────────────────────────────────
#define LED_COUNT           16
#define LED_BRIGHTNESS_MAX  220     // Never full 255 — saves power
#define LED_BRIGHTNESS_MIN  3       // Barely visible — just alive

// ── Mode Colors (CRGB hex) ────────────────────────────────────
// These define the idle breathe color for each mode
#define MODE_COLOR_SPOTIFY    0x1DB954   // Spotify green
#define MODE_COLOR_CURTAINS   0xFFAA00   // Warm amber
#define MODE_COLOR_LIGHT      0xAA44FF   // Purple
#define MODE_COLOR_LED_CTRL   0x00CCFF   // Cyan

// ── Ambient auto-dim ─────────────────────────────────────────
// Below this lux level → fade ring to minimum brightness
#define LUX_DARK_THRESHOLD     5     // Very dark room
#define LUX_BRIGHT_THRESHOLD   500   // Normal room light

// ── Presence / Sleep ─────────────────────────────────────────
#define SLEEP_TIMEOUT_MS         (5UL * 60UL * 1000UL)  // 5 min
#define RADAR_POLL_INTERVAL_MS   100

// ── LED brightness manual control step ───────────────────────
#define LED_BRIGHTNESS_STEP   15   // Per rotation in LED_CTRL mode

// ── OTA ──────────────────────────────────────────────────────
#define OTA_ENABLED   true
#define OTA_PASSWORD  "bedside_ota_pass"

// ── Debug ────────────────────────────────────────────────────
#define DEBUG_SERIAL   true
#define LOG_BAUD_RATE  115200
