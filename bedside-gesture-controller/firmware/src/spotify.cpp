// ============================================================
//  spotify.cpp  —  Spotify Web API client
//  OAuth 2.0 Authorization Code flow
//  Tokens stored in NVS (survives reboot/OTA)
// ============================================================

#include "spotify.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <base64.h>

// Global singleton
SpotifyClient gSpotify;

// NVS namespace
static Preferences _prefs;

// Spotify API root CA certificate (Baltimore CyberTrust Root)
// This covers api.spotify.com and accounts.spotify.com
static const char* SPOTIFY_ROOT_CA = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDdzCCAl+gAwIBAgIEAgAAuTANBgkqhkiG9w0BAQUFADBaMQswCQYDVQQGEwJJ
RTESMBAGA1UEChMJQmFsdGltb3JlMRMwEQYDVQQLEwpDeWJlclRydXN0MSIwIAYD
VQQDExlCYWx0aW1vcmUgQ3liZXJUcnVzdCBSb290MB4XDTAwMDUxMjE4NDYwMFoX
DTI1MDUxMjIzNTkwMFowWjELMAkGA1UEBhMCSUUxEjAQBgNVBAoTCUJhbHRpbW9y
ZTETMBEGA1UECxMKQ3liZXJUcnVzdDEiMCAGA1UEAxMZQmFsdGltb3JlIEN5YmVy
VHJ1c3QgUm9vdDCCASIwDQYJKoIhvcNAQEBBQADggEPADCCAQoCggEBAKMEuyKr
mD1X6CZymrV51Cni4eiVgLGw41uOW0amPzbNoWuHa77WsfOO0On61sEB0OQpQ0Z5
0s7pRs0JnQRH49CcFYanLAhM0s7DqP2LMH4/pXJp5pEhpjJpEY7E4LByUHIu6bD
sCbqMSBzs5Wf1PFDvO4rJyAniDP4Q0EhwkMAdlD+6zy1CLWF9Y9emFI7DOFWUOU
JaazXfNJhOHVF+bOfq3zR5ItSb7V1FDo3fZFcq2rLm8wFhPgvKAEDCJHU4iCNwi
gHxhBF/dQa6XaEjfqCEmflFnkFkv00zyBqH4nGFGbSjS6f6D9hWQfIiCFgMCAwEA
AaNmMGQwHQYDVR0OBBYEFAuOJPANOKKULvlMeQiP4gSVQeqOMB8GA1UdIwQYMBaA
FAuOJPANOKKULvlMeQiP4gSVQeqOMBIGA1UdEwEB/wQIMAYBAf8CAQMwDgYDVR0P
AQH/BAQDAgGGMA0GCSqGSIb3DQEBBQUAA4IBAQBZ4JSSG7DaT5TinZo2Ri5sELz3
N4kMPiVpZJmxMNgLjBYGZmvfchMq2Q2qlCMvFAi6g3OXPOKMFyUjR2CpRGZ2oFE
lMSPn/xr15R5Xv5M9lpuYRhPq3gIRKk/lF4WcXgL9lVLAtiFMPZ6N00rnxHFhRp
J4TIQfDa0JNe9ysF5ZtpjZGTF0nOfKExo5EbRbJH0jRSXnuWMi4V4hxMEr7TlrY
O3HFDSmYv6IkzpMNtOUApzQmEfRLGnYcGCLrqCAHqsXSfOlqxr9+6aqpN6eW0oi8
Ef6j3KRbxJvpJnAJLrPPkOCGYrh+M2sH8rggKVT59B6MChG7VVVT
-----END CERTIFICATE-----
)EOF";

// ────────────────────────────────────────────────────────────

SpotifyClient::SpotifyClient() {}

void SpotifyClient::begin() {
    _client.setCACert(SPOTIFY_ROOT_CA);
    _client.setTimeout(10);

    if (_loadTokens()) {
        _state = SpotifyState::AUTHORIZED;
        Serial.println("[SPOTIFY] Loaded tokens from NVS");
    } else {
        _state = SpotifyState::NEEDS_AUTH;
        Serial.println("[SPOTIFY] No tokens found — authorization required");
    }
}

// ── Authorization ────────────────────────────────────────────

String SpotifyClient::getAuthURL() {
    String url = "https://accounts.spotify.com/authorize";
    url += "?client_id=";    url += SPOTIFY_CLIENT_ID;
    url += "&response_type=code";
    url += "&redirect_uri="; url += urlencode(SPOTIFY_REDIRECT_URI);
    url += "&scope=user-read-playback-state%20user-modify-playback-state%20user-read-currently-playing";
    url += "&show_dialog=true";
    return url;
}

bool SpotifyClient::exchangeCode(const String& code) {
    String body = "grant_type=authorization_code";
    body += "&code=" + code;
    body += "&redirect_uri=" + String(SPOTIFY_REDIRECT_URI);

    String response = _postRequest(SPOTIFY_TOKEN_URL, "/api/token",
                                   body, "application/x-www-form-urlencoded",
                                   true);  // useBasicAuth = true

    if (response.isEmpty()) {
        Serial.println("[SPOTIFY] Token exchange failed — empty response");
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) {
        Serial.println("[SPOTIFY] Token exchange — JSON parse error");
        return false;
    }

    if (!doc["access_token"].is<const char*>()) {
        Serial.println("[SPOTIFY] Token exchange — no access_token in response");
        Serial.println(response);
        return false;
    }

    _accessToken  = doc["access_token"].as<String>();
    _refreshToken = doc["refresh_token"].as<String>();
    _tokenExpiryMs = millis() + (doc["expires_in"].as<int>() * 1000UL) - 60000UL;

    _saveTokens();
    _state = SpotifyState::AUTHORIZED;
    Serial.println("[SPOTIFY] Authorization successful!");
    return true;
}

bool SpotifyClient::refreshToken() {
    String body = "grant_type=refresh_token";
    body += "&refresh_token=" + _refreshToken;

    String response = _postRequest(SPOTIFY_TOKEN_URL, "/api/token",
                                   body, "application/x-www-form-urlencoded",
                                   true);

    if (response.isEmpty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) return false;

    if (!doc["access_token"].is<const char*>()) return false;

    _accessToken   = doc["access_token"].as<String>();
    _tokenExpiryMs = millis() + (doc["expires_in"].as<int>() * 1000UL) - 60000UL;

    // Refresh token may or may not be rotated
    if (doc["refresh_token"].is<const char*>()) {
        _refreshToken = doc["refresh_token"].as<String>();
    }

    _saveTokens();
    Serial.println("[SPOTIFY] Token refreshed");
    return true;
}

void SpotifyClient::maintainToken() {
    if (_state != SpotifyState::AUTHORIZED) return;
    if (millis() >= _tokenExpiryMs) {
        Serial.println("[SPOTIFY] Token expired — refreshing...");
        if (!refreshToken()) {
            Serial.println("[SPOTIFY] Token refresh failed!");
            _state = SpotifyState::ERROR;
        }
    }
}

// ── Playback Controls ─────────────────────────────────────────

bool SpotifyClient::play() {
    return _putRequest(SPOTIFY_API_URL, "/v1/me/player/play");
}

bool SpotifyClient::pause() {
    return _putRequest(SPOTIFY_API_URL, "/v1/me/player/pause");
}

bool SpotifyClient::togglePlayPause() {
    SpotifyPlaybackInfo info;
    if (getPlaybackState(info)) {
        return info.isPlaying ? pause() : play();
    }
    // If can't get state, just try play
    return play();
}

bool SpotifyClient::nextTrack() {
    // POST /v1/me/player/next
    _client.connect(SPOTIFY_API_URL, 443);
    String req = "POST /v1/me/player/next HTTP/1.1\r\n";
    req += "Host: " + String(SPOTIFY_API_URL) + "\r\n";
    req += "Authorization: Bearer " + _accessToken + "\r\n";
    req += "Content-Length: 0\r\n";
    req += "Connection: close\r\n\r\n";
    _client.print(req);

    // Read response code
    String statusLine = _client.readStringUntil('\n');
    _client.stop();
    return statusLine.indexOf("204") >= 0;
}

bool SpotifyClient::previousTrack() {
    _client.connect(SPOTIFY_API_URL, 443);
    String req = "POST /v1/me/player/previous HTTP/1.1\r\n";
    req += "Host: " + String(SPOTIFY_API_URL) + "\r\n";
    req += "Authorization: Bearer " + _accessToken + "\r\n";
    req += "Content-Length: 0\r\n";
    req += "Connection: close\r\n\r\n";
    _client.print(req);
    String statusLine = _client.readStringUntil('\n');
    _client.stop();
    return statusLine.indexOf("204") >= 0;
}

bool SpotifyClient::setVolume(int percent) {
    percent = constrain(percent, 0, 100);
    _currentVolume = percent;

    _client.connect(SPOTIFY_API_URL, 443);
    String path = "/v1/me/player/volume?volume_percent=" + String(percent);
    String req  = "PUT " + path + " HTTP/1.1\r\n";
    req += "Host: " + String(SPOTIFY_API_URL) + "\r\n";
    req += "Authorization: Bearer " + _accessToken + "\r\n";
    req += "Content-Length: 0\r\n";
    req += "Connection: close\r\n\r\n";
    _client.print(req);
    String statusLine = _client.readStringUntil('\n');
    _client.stop();
    return statusLine.indexOf("204") >= 0;
}

bool SpotifyClient::adjustVolume(int delta) {
    // First get current volume if we don't have it
    if (_currentVolume < 0) {
        SpotifyPlaybackInfo info;
        if (getPlaybackState(info)) {
            _currentVolume = info.volumePercent;
        }
    }
    int newVol = constrain(_currentVolume + delta, 0, 100);
    return setVolume(newVol);
}

bool SpotifyClient::getPlaybackState(SpotifyPlaybackInfo& info) {
    String response = _getRequest(SPOTIFY_API_URL, "/v1/me/player");
    if (response.isEmpty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) return false;

    info.isPlaying     = doc["is_playing"].as<bool>();
    info.volumePercent = doc["device"]["volume_percent"].as<int>();
    info.progressMs    = doc["progress_ms"].as<int>();

    if (doc["item"]["name"].is<const char*>()) {
        strlcpy(info.trackName,  doc["item"]["name"].as<const char*>(),  sizeof(info.trackName));
        strlcpy(info.artistName, doc["item"]["artists"][0]["name"].as<const char*>(), sizeof(info.artistName));
        info.durationMs = doc["item"]["duration_ms"].as<int>();
    }
    if (doc["device"]["name"].is<const char*>()) {
        strlcpy(info.deviceName, doc["device"]["name"].as<const char*>(), sizeof(info.deviceName));
    }

    _currentVolume = info.volumePercent;
    return true;
}

// ── HTTP helpers ──────────────────────────────────────────────

String SpotifyClient::_postRequest(const char* host, const char* path,
                                   const String& body,
                                   const String& contentType,
                                   bool useBasicAuth) {
    if (!_client.connect(host, 443)) {
        Serial.printf("[SPOTIFY] Connect failed to %s\n", host);
        return "";
    }

    String authHeader;
    if (useBasicAuth) {
        String credentials = String(SPOTIFY_CLIENT_ID) + ":" + SPOTIFY_CLIENT_SECRET;
        authHeader = "Authorization: Basic " + base64::encode(credentials) + "\r\n";
    } else {
        authHeader = "Authorization: Bearer " + _accessToken + "\r\n";
    }

    String req = "POST " + String(path) + " HTTP/1.1\r\n";
    req += "Host: " + String(host) + "\r\n";
    req += authHeader;
    req += "Content-Type: " + contentType + "\r\n";
    req += "Content-Length: " + String(body.length()) + "\r\n";
    req += "Connection: close\r\n\r\n";
    req += body;

    _client.print(req);

    // Read past headers
    while (_client.connected()) {
        String line = _client.readStringUntil('\n');
        if (line == "\r") break;
    }

    String response = _client.readString();
    _client.stop();
    return response;
}

String SpotifyClient::_getRequest(const char* host, const char* path) {
    if (!_client.connect(host, 443)) return "";

    String req = "GET " + String(path) + " HTTP/1.1\r\n";
    req += "Host: " + String(host) + "\r\n";
    req += "Authorization: Bearer " + _accessToken + "\r\n";
    req += "Connection: close\r\n\r\n";
    _client.print(req);

    while (_client.connected()) {
        String line = _client.readStringUntil('\n');
        if (line == "\r") break;
    }
    String response = _client.readString();
    _client.stop();
    return response;
}

bool SpotifyClient::_putRequest(const char* host, const char* path,
                                const String& body) {
    if (!_client.connect(host, 443)) return false;

    String req = "PUT " + String(path) + " HTTP/1.1\r\n";
    req += "Host: " + String(host) + "\r\n";
    req += "Authorization: Bearer " + _accessToken + "\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Content-Length: " + String(body.length()) + "\r\n";
    req += "Connection: close\r\n\r\n";
    if (body.length() > 0) req += body;
    _client.print(req);

    String statusLine = _client.readStringUntil('\n');
    _client.stop();
    return statusLine.indexOf("204") >= 0 || statusLine.indexOf("200") >= 0;
}

// ── NVS persistence ──────────────────────────────────────────

void SpotifyClient::_saveTokens() {
    _prefs.begin("spotify", false);
    _prefs.putString("access",  _accessToken);
    _prefs.putString("refresh", _refreshToken);
    _prefs.putULong("expiry",   _tokenExpiryMs);
    _prefs.end();
}

bool SpotifyClient::_loadTokens() {
    _prefs.begin("spotify", true);  // Read-only
    _accessToken   = _prefs.getString("access",  "");
    _refreshToken  = _prefs.getString("refresh", "");
    _tokenExpiryMs = _prefs.getULong("expiry",   0);
    _prefs.end();

    if (_accessToken.isEmpty() || _refreshToken.isEmpty()) return false;

    // If token has expired, refresh it immediately
    if (millis() >= _tokenExpiryMs) {
        Serial.println("[SPOTIFY] Stored token expired — refreshing on boot");
        return refreshToken();
    }
    return true;
}
