#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>

#include "secrets.h"
#include "app_config.h"
#include "board_config.h"
#include "web_interface.h"      // INDEX_HTML  — généré par tools/minify_web.py
#include "web_interface_ota.h"  // OTA_PAGE    — généré par tools/minify_web.py

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WiFiMulti wifiMulti;
WebServer  server(WEB_SERVER_PORT);

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
static void setupWiFi() {
    WiFi.mode(WIFI_STA);
    constexpr size_t N = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
    for (size_t i = 0; i < N; i++) {
        wifiMulti.addAP(WIFI_NETWORKS[i][0], WIFI_NETWORKS[i][1]);
    }

    Serial.print("WiFi: connexion en cours");
    unsigned long start = millis();
    while (wifiMulti.run() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.isConnected()) {
        Serial.printf("WiFi: connecté à \"%s\"\n", WiFi.SSID().c_str());
        Serial.printf("WiFi: IP = %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("WiFi: RSSI = %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("WiFi: échec de connexion");
    }
}

// ---------------------------------------------------------------------------
// ArduinoOTA (mise à jour via PlatformIO / IDE réseau)
// ---------------------------------------------------------------------------
#ifdef ENABLE_OTA
static void setupArduinoOTA() {
    ArduinoOTA.setHostname(MDNS_HOSTNAME);
    ArduinoOTA.onStart([]()  { Serial.println("OTA: début"); });
    ArduinoOTA.onEnd([]()    { Serial.println("\nOTA: terminé"); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        Serial.printf("OTA: %u%%\r", p * 100 / t);
    });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("OTA: erreur [%u]\n", err);
    });
    ArduinoOTA.begin();
    Serial.println("OTA: ArduinoOTA actif");
}
#endif

// ---------------------------------------------------------------------------
// Routes web
// ---------------------------------------------------------------------------
static void handleRoot() {
    // Servir directement depuis PROGMEM — aucun SPIFFS requis
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handleApiStatus() {
    JsonDocument doc;
    doc["ssid"]     = WiFi.SSID();
    doc["ip"]       = WiFi.localIP().toString();
    doc["rssi"]     = WiFi.RSSI();
    doc["uptime"]   = millis();
    doc["version"]  = PROJECT_VERSION;
    doc["hostname"] = MDNS_HOSTNAME;

    String json;
    serializeJson(doc, json);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
}

static void handleOtaGet() {
    server.send_P(200, "text/html", OTA_PAGE);
}

static void handleOtaUploadDone() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(500);
    ESP.restart();
}

static void handleOtaUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("OTA Web: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("OTA Web: %u octets écrits\n", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    }
}

static void setupWebServer() {
    server.on("/",           HTTP_GET,  handleRoot);
    server.on("/api/status", HTTP_GET,  handleApiStatus);
    server.on("/update",     HTTP_GET,  handleOtaGet);
    server.on("/update",     HTTP_POST, handleOtaUploadDone, handleOtaUpload);

    server.begin();
    Serial.printf("Web: serveur démarré sur le port %d\n", WEB_SERVER_PORT);
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.printf("\n=== %s v%s ===\n", PROJECT_NAME, PROJECT_VERSION);

    setupWiFi();

#ifdef ENABLE_OTA
    if (WiFi.isConnected()) setupArduinoOTA();
#endif

#ifdef ENABLE_MDNS
    if (WiFi.isConnected()) {
        if (MDNS.begin(MDNS_HOSTNAME)) {
            Serial.printf("mDNS: %s.local actif\n", MDNS_HOSTNAME);
        }
    }
#endif

#ifdef ENABLE_WEB_SERVER
    if (WiFi.isConnected()) setupWebServer();
#endif
}

void loop() {
#ifdef ENABLE_OTA
    ArduinoOTA.handle();
#endif

    if (WiFi.status() != WL_CONNECTED) {
        wifiMulti.run();
    }

    server.handleClient();
}
