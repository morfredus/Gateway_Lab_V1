/**
 * WebServerModule — Implémentation
 *
 * Bibliothèques utilisées :
 *   WebServer    — serveur HTTP intégré à l'Arduino ESP32
 *   ArduinoJson  — sérialisation JSON pour les réponses /api/*
 *   ESPmDNS      — résolution de noms (gateway-lab-v1.local)
 */

#include "web_server.h"
#include "ota_manager.h"         // Enregistrement des routes /update
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "app_config.h"          // MDNS_HOSTNAME, PROJECT_VERSION
#include "../../include/web_interface.h"   // INDEX_HTML (page d'accueil en PROGMEM)
#include "../utils/logger.h"

static const char* TAG = "WebSrv";

// Instance interne du serveur HTTP — non exposée hors de ce fichier
static WebServer _server(WEB_SERVER_PORT);

// Instance globale exportée
WebServerModule webSrv;

void WebServerModule::registerScanProvider(ScanProvider p) {
    _scan    = p;
    _hasScan = true;
}

void WebServerModule::begin(uint16_t port) {
    // Enregistrement de toutes les routes HTTP
    // HTTP_GET = le navigateur demande une ressource
    // HTTP_POST = le navigateur envoie des données (formulaire, upload...)
    _server.on("/",           HTTP_GET,  [this]() { _handleRoot(); });
    _server.on("/api/status", HTTP_GET,  [this]() { _handleApiStatus(); });
    _server.on("/api/devices",HTTP_GET,  [this]() { _handleApiDevices(); });
    _server.on("/api/scan",   HTTP_POST, [this]() { _handleApiScanTrigger(); });
    _server.onNotFound(       [this]()  { _handleNotFound(); });

    // Délégation des routes OTA à OtaManager (/update GET + POST)
    otaMgr.registerRoutes(_server);

    _server.begin();
    Log::i(TAG, "Serveur démarré sur le port %u", (unsigned)port);
}

void WebServerModule::loop() {
    // Traitement d'une requête HTTP en attente (non-bloquant)
    _server.handleClient();
}

// ---------------------------------------------------------------------------
// Handler : page d'accueil
// INDEX_HTML est stocké en flash (PROGMEM) pour économiser la RAM.
// send_P() lit directement depuis la flash sans copie en mémoire.
// ---------------------------------------------------------------------------
void WebServerModule::_handleRoot() {
    _server.send_P(200, "text/html", INDEX_HTML);
}

// ---------------------------------------------------------------------------
// Handler : état du système en JSON
// Exemple de réponse :
//   {"ssid":"Livebox","ip":"192.168.1.42","rssi":-55,"uptime":12345,
//    "version":"0.0.3","hostname":"gateway-lab-v1","scanning":false}
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiStatus() {
    JsonDocument doc;
    doc["ssid"]     = WiFi.SSID();
    doc["ip"]       = WiFi.localIP().toString();
    doc["rssi"]     = WiFi.RSSI();
    doc["uptime"]   = millis();               // Temps depuis le démarrage en ms
    doc["version"]  = PROJECT_VERSION;
    doc["hostname"] = MDNS_HOSTNAME;
    doc["scanning"] = (_hasScan && _scan.isScanning && _scan.isScanning());

    String json;
    serializeJson(doc, json);
    _server.sendHeader("Cache-Control", "no-cache");  // Pas de mise en cache navigateur
    _server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Handler : liste des équipements réseau
// Exemple de réponse :
//   {"scanning":false,"devices":[
//     {"ip":"192.168.1.1","mac":"F8:1A:67:AA:BB:CC","vendor":"Freebox","lastSeen":5234},
//     {"ip":"192.168.1.10","mac":"B8:27:EB:11:22:33","vendor":"Raspberry Pi","lastSeen":5891}
//   ]}
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiDevices() {
    String json = "{\"scanning\":";
    json += (_hasScan && _scan.isScanning && _scan.isScanning()) ? "true" : "false";
    json += ",\"devices\":";
    json += (_hasScan && _scan.getJson) ? _scan.getJson() : "[]";
    json += "}";

    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Handler : déclenchement d'un scan réseau
// Appelé par un POST depuis le bouton "Scanner" de l'interface web
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiScanTrigger() {
    if (_hasScan && _scan.triggerScan) {
        _scan.triggerScan();
        _server.send(200, "application/json", "{\"status\":\"started\"}");
    } else {
        _server.send(503, "application/json", "{\"error\":\"scanner unavailable\"}");
    }
}

// ---------------------------------------------------------------------------
// Handler : route inconnue (404)
// ---------------------------------------------------------------------------
void WebServerModule::_handleNotFound() {
    _server.send(404, "text/plain", "Not Found");
}
