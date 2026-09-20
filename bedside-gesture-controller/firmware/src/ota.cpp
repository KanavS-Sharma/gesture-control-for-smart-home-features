// ============================================================
//  ota.cpp  —  OTA firmware update via HTTP web interface
//  Registers /ota (POST binary) and /update (web form)
// ============================================================

#include "ota.h"
#include "config.h"
#include <Arduino.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>

// Global singleton
OTAManager gOTA;

static const char OTA_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Bedside Controller — OTA Update</title>
  <style>
    body { font-family: sans-serif; background: #111; color: #eee;
           display: flex; flex-direction: column; align-items: center;
           justify-content: center; min-height: 100vh; margin: 0; }
    h1  { color: #1DB954; }
    form { background: #1e1e1e; padding: 2rem; border-radius: 12px; }
    input[type=file] { margin: 1rem 0; }
    button { background: #1DB954; color: #fff; border: none; padding: 0.8rem 2rem;
             border-radius: 8px; cursor: pointer; font-size: 1rem; }
    button:hover { background: #17a044; }
    #progress { margin-top: 1rem; font-size: 0.9rem; color: #aaa; }
  </style>
</head>
<body>
  <h1>🛏️ Firmware Update</h1>
  <form method="POST" action="/ota" enctype="multipart/form-data">
    <p>Select firmware binary (.bin):</p>
    <input type="file" name="firmware" accept=".bin" required>
    <br>
    <button type="submit">⬆️ Upload & Flash</button>
  </form>
  <div id="progress"></div>
</body>
</html>
)HTML";

OTAManager::OTAManager() {}

void OTAManager::begin(void* serverPtr) {
#if OTA_ENABLED
    AsyncWebServer* server = (AsyncWebServer*)serverPtr;

    // Serve OTA web page
    server->on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", OTA_PAGE);
    });

    // Handle firmware upload
    server->on("/ota", HTTP_POST,
        // onRequest — send response after upload
        [this](AsyncWebServerRequest* req) {
            bool success = !Update.hasError();
            AsyncWebServerResponse* res = req->beginResponse(
                200, "text/plain",
                success ? "Update successful! Rebooting..." : "Update FAILED!"
            );
            res->addHeader("Connection", "close");
            req->send(res);
            if (success) {
                delay(500);
                ESP.restart();
            }
            _updating = false;
        },
        // onUpload — handle incoming binary chunks
        [this](AsyncWebServerRequest* req, const String& filename,
               size_t index, uint8_t* data, size_t len, bool final) {
            if (!index) {
                Serial.printf("[OTA] Starting update: %s\n", filename.c_str());
                _updating = true;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] Update done: %u bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

    Serial.println("[OTA] OTA web endpoint registered at /update and /ota");
#endif
}
