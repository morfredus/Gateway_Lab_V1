#include "web_server.h"
#include "ota_manager.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "app_config.h"
#include "../../include/web_interface.h"
#include "../utils/logger.h"

static const char* TAG = "WebSrv";
static WebServer   _server(WEB_SERVER_PORT);

WebServerModule webSrv;

void WebServerModule::registerScanProvider(ScanProvider p) {
    _scan    = p;
    _hasScan = true;
}

void WebServerModule::begin(uint16_t port) {
    _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
    _server.on("/api/status",  HTTP_GET,  [this]() { _handleApiStatus(); });
    _server.on("/api/devices", HTTP_GET,  [this]() { _handleApiDevices(); });
    _server.on("/api/scan",    HTTP_POST, [this]() { _handleApiScanTrigger(); });
    _server.onNotFound([this]() { _handleNotFound(); });

    otaMgr.registerRoutes(_server);

    _server.begin();
    Log::i(TAG, "Serveur démarré sur le port %u", (unsigned)port);
}

void WebServerModule::loop() {
    _server.handleClient();
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
void WebServerModule::_handleRoot() {
    _server.send_P(200, "text/html", INDEX_HTML);
}

void WebServerModule::_handleApiStatus() {
    JsonDocument doc;
    doc["ssid"]     = WiFi.SSID();
    doc["ip"]       = WiFi.localIP().toString();
    doc["rssi"]     = WiFi.RSSI();
    doc["uptime"]   = millis();
    doc["version"]  = PROJECT_VERSION;
    doc["hostname"] = MDNS_HOSTNAME;
    doc["scanning"] = (_hasScan && _scan.isScanning && _scan.isScanning());

    String json;
    serializeJson(doc, json);
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

void WebServerModule::_handleApiDevices() {
    String json = "{\"scanning\":";
    json += (_hasScan && _scan.isScanning && _scan.isScanning()) ? "true" : "false";
    json += ",\"devices\":";
    json += (_hasScan && _scan.getJson) ? _scan.getJson() : "[]";
    json += "}";

    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

void WebServerModule::_handleApiScanTrigger() {
    if (_hasScan && _scan.triggerScan) {
        _scan.triggerScan();
        _server.send(200, "application/json", "{\"status\":\"started\"}");
    } else {
        _server.send(503, "application/json", "{\"error\":\"scanner unavailable\"}");
    }
}

void WebServerModule::_handleNotFound() {
    _server.send(404, "text/plain", "Not Found");
}
