#pragma once
// ============================================================
//  spotify.h  —  Spotify Web API client (OAuth 2.0 PKCE)
// ============================================================

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"

// Spotify API endpoints
#define SPOTIFY_TOKEN_URL  "accounts.spotify.com"
#define SPOTIFY_API_URL    "api.spotify.com"

enum class SpotifyState {
    NEEDS_AUTH,
    AUTH_IN_PROGRESS,
    AUTHORIZED,
    ERROR
};

struct SpotifyPlaybackInfo {
    bool    isPlaying;
    int     volumePercent;       // 0–100
    char    trackName[128];
    char    artistName[128];
    char    deviceName[64];
    int     progressMs;
    int     durationMs;
};

class SpotifyClient {
public:
    SpotifyClient();

    // Call once in setup() — loads tokens from NVS if available
    void begin();

    // Returns the authorization URL for the user to visit
    String getAuthURL();

    // Call this with the code from the redirect URI callback
    bool exchangeCode(const String& code);

    // Refresh access token (auto-called when needed)
    bool refreshToken();

    // Playback controls — return true on success
    bool play();
    bool pause();
    bool togglePlayPause();
    bool nextTrack();
    bool previousTrack();
    bool setVolume(int percent);
    bool adjustVolume(int delta);   // +/- delta from current

    // Get current playback state
    bool getPlaybackState(SpotifyPlaybackInfo& info);

    // Auth state
    SpotifyState getState() const { return _state; }
    bool isAuthorized() const     { return _state == SpotifyState::AUTHORIZED; }

    // Called periodically to check token expiry
    void maintainToken();

private:
    SpotifyState _state          = SpotifyState::NEEDS_AUTH;
    String       _accessToken;
    String       _refreshToken;
    unsigned long _tokenExpiryMs = 0;
    int          _currentVolume  = 50;

    WiFiClientSecure _client;

    // Internal HTTP helpers
    String  _postRequest(const char* host, const char* path,
                         const String& body, const String& contentType,
                         bool useBasicAuth = false);
    String  _getRequest (const char* host, const char* path);
    bool    _putRequest (const char* host, const char* path,
                         const String& body = "");

    // NVS persistence
    void _saveTokens();
    bool _loadTokens();
};

// Global singleton
extern SpotifyClient gSpotify;
