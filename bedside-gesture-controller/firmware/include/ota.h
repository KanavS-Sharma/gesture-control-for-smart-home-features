#pragma once
// ============================================================
//  ota.h  —  OTA firmware update handler
// ============================================================

#include <Arduino.h>

class OTAManager {
public:
    OTAManager();

    // Register OTA routes on the async web server
    void begin(void* server);  // AsyncWebServer*

    // Check if update is in progress
    bool isUpdating() const { return _updating; }

private:
    bool _updating = false;
};

// Global singleton
extern OTAManager gOTA;
