#include "wifi_manager.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include "app_config.h"
#include "secrets.h"
#include "../utils/logger.h"

static const char* TAG = "WiFi";
static WiFiMulti   _multi;
static constexpr unsigned long RECONNECT_DEBOUNCE_MS = 30000;
static unsigned long _lastReconnectAttempt = 0;

WiFiManager wifiMgr;

void WiFiManager::begin(Callback cb) {
    WiFi.mode(WIFI_STA);

    constexpr size_t N = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
    for (size_t i = 0; i < N; i++) {
        _multi.addAP(WIFI_NETWORKS[i][0], WIFI_NETWORKS[i][1]);
    }

    Log::i(TAG, "Connexion en cours...");
    unsigned long start = millis();
    while (_multi.run() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.isConnected()) {
        Log::i(TAG, "Connecté à \"%s\" — IP %s — RSSI %d dBm",
               WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());

#ifdef ENABLE_MDNS
        if (MDNS.begin(MDNS_HOSTNAME)) {
            Log::i(TAG, "mDNS: %s.local", MDNS_HOSTNAME);
        }
#endif
        if (cb) cb(true);
    } else {
        Log::w(TAG, "Échec de connexion WiFi");
        if (cb) cb(false);
    }
}

void WiFiManager::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= RECONNECT_DEBOUNCE_MS) {
            _lastReconnectAttempt = now;
            Log::w(TAG, "WiFi perdu, tentative de reconnexion...");
            _multi.run();
        }
    }
}

bool   WiFiManager::isConnected() const { return WiFi.isConnected(); }
String WiFiManager::ssid()        const { return WiFi.SSID(); }
String WiFiManager::localIP()     const { return WiFi.localIP().toString(); }
int8_t WiFiManager::rssi()        const { return (int8_t)WiFi.RSSI(); }
String WiFiManager::hostname()    const { return MDNS_HOSTNAME; }
