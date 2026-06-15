/**
 * WiFiManager — Implémentation
 *
 * Bibliothèques utilisées :
 *   WiFiMulti  — gestion de plusieurs réseaux, connexion au meilleur signal
 *   ESPmDNS    — résolution de noms sur le réseau local (gateway-lab-v1.local)
 */

#include "wifi_manager.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include "app_config.h"    // WIFI_CONNECT_TIMEOUT, MDNS_HOSTNAME, ENABLE_MDNS
#include "secrets.h"       // WIFI_NETWORKS[][2] — liste des réseaux WiFi
#include "../utils/logger.h"

static const char* TAG = "WiFi";

// Instance interne de WiFiMulti — non exposée hors de ce fichier
static WiFiMulti _multi;

// Délai minimum entre deux tentatives de reconnexion automatique
// Évite de spammer les paquets DHCP si le réseau est instable
static constexpr unsigned long RECONNECT_DEBOUNCE_MS = 30000;
static unsigned long _lastReconnectAttempt = 0;

// Callback conservé pour le rappeler lors d'une reconnexion automatique
static WiFiManager::Callback _storedCb;

// État précédent : permet de détecter la transition déconnecté → connecté
static bool _wasConnected = false;

// Instance globale exportée
WiFiManager wifiMgr;

static void _startMdns() {
#ifdef ENABLE_MDNS
    if (MDNS.begin(MDNS_HOSTNAME)) {
        Log::i(TAG, "mDNS actif : http://%s.local", MDNS_HOSTNAME);
    }
#endif
}

void WiFiManager::begin(Callback cb) {
    _storedCb = cb;
    WiFi.mode(WIFI_STA);   // Mode station (client) — pas point d'accès

    // Enregistrement de tous les réseaux définis dans secrets.h
    // WiFiMulti choisira automatiquement celui au signal le plus fort
    constexpr size_t N = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
    for (size_t i = 0; i < N; i++) {
        _multi.addAP(WIFI_NETWORKS[i][0], WIFI_NETWORKS[i][1]);
    }

    Log::i(TAG, "Connexion en cours...");
    unsigned long start = millis();

    // Attente de la connexion jusqu'au timeout configuré
    while (_multi.run() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        Serial.print('.');  // Indicateur visuel de progression dans le moniteur série
    }
    Serial.println();

    if (WiFi.isConnected()) {
        Log::i(TAG, "Connecté à \"%s\" — IP %s — RSSI %d dBm",
               WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
        _startMdns();
        _wasConnected = true;
        if (cb) cb(true);   // Succès : démarrage des services réseau
    } else {
        Log::w(TAG, "Échec de connexion après %lu ms", WIFI_CONNECT_TIMEOUT);
        _wasConnected = false;
        if (cb) cb(false);  // Échec : les services réseau ne seront pas démarrés
    }
}

void WiFiManager::loop() {
    bool connected = WiFi.isConnected();

    if (!connected) {
        unsigned long now = millis();
        // Tentative de reconnexion seulement si le délai de debounce est écoulé
        if (now - _lastReconnectAttempt >= RECONNECT_DEBOUNCE_MS) {
            _lastReconnectAttempt = now;
            Log::w(TAG, "WiFi perdu — tentative de reconnexion...");
            _multi.run();   // Retourne immédiatement, la connexion est async
        }
    } else if (!_wasConnected) {
        // Transition déconnecté → connecté : relancer mDNS et tous les services
        Log::i(TAG, "WiFi rétabli sur \"%s\" — IP %s",
               WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        _startMdns();
        if (_storedCb) _storedCb(true);
    }

    _wasConnected = connected;
}

bool   WiFiManager::isConnected() const { return WiFi.isConnected(); }
String WiFiManager::ssid()        const { return WiFi.SSID(); }
String WiFiManager::localIP()     const { return WiFi.localIP().toString(); }
int8_t WiFiManager::rssi()        const { return (int8_t)WiFi.RSSI(); }
String WiFiManager::hostname()    const { return MDNS_HOSTNAME; }
