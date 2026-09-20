# gesture-control-for-smart-home-features
gesture control for smart home features


This is a great redesign — much more intuitive. Let me map it all out clearly before rewriting the code.

New scheme:

Swipe UP/DOWN → cycle modes (each has its own color)

Modes: Spotify → Curtains → Smart Light → LED Brightness → (loops)

Within modes: Left/Right for discrete actions, Rotate for percentage (shown on ring), Wave for play/pause

Swipe UP   ↑  →  Next mode  (Spotify → Curtains → Smart Light → LED Brightness → Spotify...)
Swipe DOWN ↓  →  Prev mode

Each mode switch = sweeping LED wipe + new idle breathe color

🎵 SPOTIFY  (green breathe)
├── Swipe LEFT  ←   Previous track   →  blue CCW comet chase
├── Swipe RIGHT →   Next track       →  blue CW comet chase
├── Rotate CW   ↻   Volume +5%       →  LEVEL METER on ring (green)
├── Rotate CCW  ↺   Volume -5%       →  LEVEL METER on ring (green)
└── Wave / Fwd  ✋   Play / Pause     →  3× white flash

🪟 CURTAINS  (amber breathe)
├── Swipe RIGHT →   Fully OPEN       →  full ring amber
├── Swipe LEFT  ←   Fully CLOSE      →  empty ring
├── Rotate CW   ↻   Open +5%         →  LEVEL METER on ring (amber)
├── Rotate CCW  ↺   Close -5%        →  LEVEL METER on ring (amber)
└── Wave        ✋   Stop movement    →  quick amber flash

💡 SMART LIGHT  (purple breathe)
├── Swipe RIGHT →   Lights ON        →  purple flash
├── Swipe LEFT  ←   Lights OFF       →  quick dim flash
└── Wave        ✋   Toggle           →  purple flash

🔆 LED CONTROL  (cyan breathe)
├── Rotate CW   ↻   Ring brighter    →  LEVEL METER (cyan)
├── Rotate CCW  ↺   Ring dimmer      →  LEVEL METER (cyan)
└── Wave        ✋   Toggle auto-dim  →  green=auto ON / orange=manual

The Level Meter 💡
Just like the ring on a BMW iDrive volume knob — as you rotate:

50% volume = 8 of 16 LEDs fully lit, 8 dimly lit (track)
100% curtain open = all 16 LEDs lit
0% = all LEDs show dim track only
The meter auto-hides after 2 seconds of no rotation, returning to the idle breathe.

Ambient Auto-Dim
During the day (bright room) → ring at full brightness
At night / dark room → ring fades to near-zero, won't disturb sleep
Override → switch to LED Control mode, rotate to set manually, wave to toggle auto-dim back on



File	Purpose
platformio.ini   PlatformIO build config for ESP32-S3 N16R8
partitions.csv    Dual OTA + SPIFFS partition table
include/config.h   ⚙️ Edit this — WiFi, Spotify keys, MQTT, GPIO pins
src/main.cpp    Full orchestrator — FreeRTOS tasks, web server, gesture router
src/gestures.cpp  PAJ7620U2 register-level I²C driver
src/radar.cpp  LD2410C binary UART frame parser
src/lux_sensor.cpp  BH1750 I²C driver with adaptive LED brightness
src/led_ring.cpp  FastLED animation engine (10 animations)
src/spotify.cpp  Full Spotify OAuth 2.0 + HTTPS API client
src/smarthome.cpp  MQTT client with Home Assistant LWT, publish/subscribe
src/ota.cpp  OTA web UI + binary upload endpoint
home_assistant_config.yaml  Full MQTT sensors + automation templates
build_guide.md  Complete runway: BOM, wiring, power, OAuth steps, troubleshooting



Quick Start
Edit include/config.h — fill in your WiFi credentials, Spotify Client ID/Secret, and MQTT broker IP
Flash with pio run --target upload
Open browser → http://bedside-controller.local/ → click Authorize Spotify
Use gestures! — Swipe/wave to control music, rotate CCW to switch to Smart Home mode
🎛️ The Dual-Mode System
Clockwise rotation → Spotify mode (green LED breathe)
Counter-clockwise rotation → Smart Home mode (purple LED breathe)
Same physical gestures control totally different things depending on mode
