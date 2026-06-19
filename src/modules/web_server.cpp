/**
 * WebServerModule — Implémentation
 *
 * Bibliothèques utilisées :
 *   WebServer    — serveur HTTP intégré à l'Arduino ESP32
 *   ArduinoJson  — sérialisation JSON pour les réponses /api/*
 *   ESPmDNS      — résolution de noms (gateway-lab-v1.local)
 */

#include "web_server.h"
#include "ota_manager.h"         // Enregistrement de la route POST /update
#include "wifi_manager.h"        // Gestion des reseaux WiFi enregistres (NVS)
#include "status_led.h"          // Luminosite NeoPixel - reglable depuis /wifi (Systeme)
#include "system_health.h"       // Etat memoire / mode degrade + redemarrage manuel
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "app_config.h"          // MDNS_HOSTNAME, PROJECT_VERSION
#include "../../include/web_interface.h"      // INDEX_HTML (page d'accueil en PROGMEM)
#include "../../include/web_interface_scan.h" // SCAN_PAGE (page équipements en PROGMEM)
#include "../../include/web_interface_history.h" // HISTORY_PAGE (vue chronologique en PROGMEM)
#include "../../include/web_interface_wifi.h" // WIFI_PAGE (page Systeme en PROGMEM)
#include "../../include/web_interface_topology.h" // TOPOLOGY_PAGE (topologie reseau en PROGMEM)
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
    if (_started) return;  // Le socket TCP persiste après reconnexion WiFi
    _started = true;
    // Enregistrement de toutes les routes HTTP
    // HTTP_GET = le navigateur demande une ressource
    // HTTP_POST = le navigateur envoie des données (formulaire, upload...)
    _server.on("/",           HTTP_GET,  [this]() { _handleRoot(); });
    _server.on("/scan",       HTTP_GET,  [this]() {
        if (_hasScan && _scan.acknowledgeNewDevices) _scan.acknowledgeNewDevices();
        _server.send_P(200, "text/html", SCAN_PAGE);
    });
    _server.on("/history",    HTTP_GET,  [this]() { _server.send_P(200, "text/html", HISTORY_PAGE); });
    _server.on("/topology",   HTTP_GET,  [this]() { _server.send_P(200, "text/html", TOPOLOGY_PAGE); });
    _server.on("/api/status", HTTP_GET,  [this]() { _handleApiStatus(); });
    _server.on("/api/devices",HTTP_GET,  [this]() { _handleApiDevices(); });
    _server.on("/api/scan",   HTTP_POST, [this]() { _handleApiScanTrigger(); });
    _server.on("/api/alias",  HTTP_POST, [this]() { _handleApiSetAlias(); });
    _server.on("/api/devices/reset", HTTP_POST, [this]() { _handleApiDevicesReset(); });
    _server.on("/api/devices/rescan", HTTP_POST, [this]() { _handleApiDeviceRescan(); });
    _server.on("/api/devices/rescan/status", HTTP_GET, [this]() { _handleApiDeviceRescanStatus(); });
    _server.on("/api/history",HTTP_GET,  [this]() { _handleApiHistory(); });
    _server.on("/api/history",HTTP_DELETE, [this]() { _handleApiHistoryClear(); });
    _server.on("/api/backup", HTTP_GET,  [this]() { _handleApiBackup(); });
    _server.on("/api/restore",HTTP_POST, [this]() { _handleApiRestore(); });
    _server.on("/api/devices/export.csv", HTTP_GET, [this]() { _handleApiDevicesExportCsv(); });
    _server.on("/api/favorite", HTTP_POST,   [this]() { _handleApiSetFavorite(); });
    _server.on("/api/notes",    HTTP_POST,   [this]() { _handleApiAddNote(); });
    _server.on("/api/notes",    HTTP_DELETE, [this]() { _handleApiDeleteNote(); });
    _server.on("/api/diagnostics", HTTP_GET, [this]() { _handleApiDiagnostics(); });
    _server.on("/wifi",       HTTP_GET,  [this]() { _server.send_P(200, "text/html", WIFI_PAGE); });
    _server.on("/api/wifi",   HTTP_GET,  [this]() { _handleApiWifiGet(); });
    _server.on("/api/wifi",   HTTP_POST, [this]() { _handleApiWifiPost(); });
    _server.on("/api/wifi",   HTTP_DELETE, [this]() { _handleApiWifiDelete(); });
    _server.on("/api/system/backup",  HTTP_GET,  [this]() { _handleApiSystemBackup(); });
    _server.on("/api/system/restore", HTTP_POST, [this]() { _handleApiSystemRestore(); });
    _server.on("/api/system/health",  HTTP_GET,  [this]() {
        String j = "{\"degraded\":";
        j += systemHealth.isDegraded() ? "true" : "false";
        j += ",\"reason\":\"" + systemHealth.reason() + "\",\"freeHeap\":";
        j += ESP.getFreeHeap();
        j += "}";
        _server.sendHeader("Cache-Control", "no-cache");
        _server.send(200, "application/json", j);
    });
    _server.on("/api/system/restart", HTTP_POST, [this]() {
        _server.send(200, "application/json", "{\"ok\":true}");
        systemHealth.restartNow();
    });
    _server.on("/api/led/brightness", HTTP_GET,  [this]() {
        String j = "{\"brightness\":";
        j += statusLed.getBrightness();
        j += "}";
        _server.send(200, "application/json", j);
    });
    _server.on("/api/led/brightness", HTTP_POST, [this]() {
        if (!_server.hasArg("value")) { _server.send(400, "application/json", "{\"error\":\"value manquant\"}"); return; }
        int v = _server.arg("value").toInt();
        if (v < 0 || v > 100) { _server.send(400, "application/json", "{\"error\":\"valeur hors plage (0-100)\"}"); return; }
        statusLed.setBrightness((uint8_t)v);
        _server.send(200, "application/json", "{\"status\":\"ok\"}");
    });
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
    if (_hasScan && _scan.getStats) {
        json += ",\"stats\":";
        json += _scan.getStats();
    }
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
// Handler : definit l'alias utilisateur d'un equipement
// Corps attendu (form-urlencoded ou JSON simple) : mac (ou ip) + alias
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiSetAlias() {
    if (!_hasScan || !_scan.setAlias) {
        _server.send(503, "application/json", "{\"error\":\"non disponible\"}");
        return;
    }

    String key   = _server.arg("mac");
    if (key.isEmpty()) key = _server.arg("ip");
    String alias = _server.arg("alias");

    if (key.isEmpty()) {
        _server.send(400, "application/json", "{\"error\":\"mac ou ip requis\"}");
        return;
    }

    bool ok = _scan.setAlias(key, alias);
    _server.send(ok ? 200 : 404, "application/json",
                 ok ? "{\"status\":\"ok\"}" : "{\"error\":\"equipement introuvable\"}");
}

// ---------------------------------------------------------------------------
// Handler : RAZ des equipements connus pour repartir sur une base vide
// Parametres (form-urlencoded) : keepAlias=1, keepManufacturer=1 (optionnels)
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiDevicesReset() {
    if (!_hasScan || !_scan.resetDevices) {
        _server.send(503, "application/json", "{\"error\":\"non disponible\"}");
        return;
    }

    bool keepAlias        = _server.arg("keepAlias")        == "1";
    bool keepManufacturer = _server.arg("keepManufacturer") == "1";

    int removed = _scan.resetDevices(keepAlias, keepManufacturer);

    String json = "{\"status\":\"ok\",\"removed\":";
    json += removed;
    json += "}";
    _server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Handler : rafraichissement cible d'un seul equipement (sans scan complet)
// Parametres (form-urlencoded) : ip, mode ("quick" par defaut, ou "deep")
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiDeviceRescan() {
    if (!_hasScan || !_scan.rescanDevice) {
        _server.send(503, "application/json", "{\"error\":\"non disponible\"}");
        return;
    }

    String ip = _server.arg("ip");
    if (ip.isEmpty()) {
        _server.send(400, "application/json", "{\"error\":\"ip requise\"}");
        return;
    }
    bool deep = (_server.arg("mode") == "deep");

    bool started = _scan.rescanDevice(ip, deep);
    _server.send(started ? 200 : 409, "application/json",
                 started ? "{\"status\":\"started\"}" : "{\"error\":\"equipement inconnu ou scan en cours\"}");
}

// ---------------------------------------------------------------------------
// Handler : avancement de la passe precise en cours (polling cote UI)
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiDeviceRescanStatus() {
    String json = (_hasScan && _scan.getRescanStatusJson)
        ? _scan.getRescanStatusJson()
        : "{\"running\":false,\"ok\":false,\"ip\":\"\",\"step\":\"\",\"percent\":0}";
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Handler : journal chronologique des evenements (nouveaux/changements)
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiHistory() {
    String json = (_hasScan && _scan.getHistoryJson) ? _scan.getHistoryJson() : "[]";
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Handler : vide le journal chronologique
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiHistoryClear() {
    if (!_hasScan || !_scan.clearHistory) {
        _server.send(503, "application/json", "{\"error\":\"non disponible\"}");
        return;
    }
    _scan.clearHistory();
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ---------------------------------------------------------------------------
// Handler : telechargement de la sauvegarde complete (devices + historique)
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiBackup() {
    if (!_hasScan || !_scan.getBackupJson) {
        _server.send(503, "application/json; charset=utf-8", "{\"error\":\"non disponible\"}");
        return;
    }
    String json = _scan.getBackupJson();
    _server.sendHeader("Content-Disposition", "attachment; filename=\"gateway-lab-backup.json\"");
    _server.send(200, "application/json; charset=utf-8", json);
}

// ---------------------------------------------------------------------------
// Handler : telechargement de l'inventaire au format CSV (tableur, scripts externes)
// Le BOM UTF-8 (EF BB BF) en tete de fichier permet a Excel de reconnaitre
// l'encodage et d'afficher correctement les caracteres accentues.
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiDevicesExportCsv() {
    if (!_hasScan || !_scan.getDevicesCsv) {
        _server.send(503, "text/plain; charset=utf-8", "non disponible");
        return;
    }
    String csv = "\xEF\xBB\xBF" + _scan.getDevicesCsv();
    _server.sendHeader("Content-Disposition", "attachment; filename=\"gateway-lab-devices.csv\"");
    _server.send(200, "text/csv; charset=utf-8", csv);
}

// ---------------------------------------------------------------------------
// Handler : restauration depuis une sauvegarde JSON (corps brut de la requete)
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiRestore() {
    if (!_hasScan || !_scan.restoreFromJson) {
        _server.send(503, "application/json; charset=utf-8", "{\"error\":\"non disponible\"}");
        return;
    }

    String body = _server.arg("plain");
    if (body.isEmpty()) {
        _server.send(400, "application/json; charset=utf-8", "{\"error\":\"corps JSON requis\"}");
        return;
    }

    bool ok = _scan.restoreFromJson(body);
    _server.send(ok ? 200 : 400, "application/json; charset=utf-8",
                 ok ? "{\"status\":\"ok\"}" : "{\"error\":\"JSON invalide\"}");
}

// ---------------------------------------------------------------------------
// Handler : sauvegarde des parametres de fonctionnement du projet (distincte
// de /api/backup, qui sauvegarde l'inventaire des equipements) — reseaux
// WiFi enregistres (SSID + mot de passe), luminosite NeoPixel, nom mDNS
// (informatif : fixe a la compilation via MDNS_HOSTNAME, non restaurable).
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiSystemBackup() {
    JsonDocument doc;
    doc["version"]       = PROJECT_VERSION;
    doc["mdnsHostname"]  = wifiMgr.hostname();
    doc["ledBrightness"] = statusLed.getBrightness();
    JsonArray arr = doc["wifiNetworks"].to<JsonArray>();
    for (const auto& c : wifiMgr.savedNetworks()) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"]     = c.ssid;
        o["password"] = c.password;
    }

    String json;
    serializeJson(doc, json);
    _server.sendHeader("Content-Disposition", "attachment; filename=\"gateway-lab-settings.json\"");
    _server.send(200, "application/json; charset=utf-8", json);
}

// ---------------------------------------------------------------------------
// Handler : restauration des parametres de fonctionnement depuis une
// sauvegarde generee par /api/system/backup. Le nom mDNS n'est pas restaure
// (fixe a la compilation). Les reseaux WiFi sont ajoutes/mis a jour, jamais
// supprimes automatiquement (pour ne pas perdre l'acces si le fichier est
// incomplet ou perime).
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiSystemRestore() {
    String body = _server.arg("plain");
    if (body.isEmpty()) {
        _server.send(400, "application/json; charset=utf-8", "{\"error\":\"corps JSON requis\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        _server.send(400, "application/json; charset=utf-8", "{\"error\":\"JSON invalide\"}");
        return;
    }

    int brightness = doc["ledBrightness"] | -1;
    if (brightness >= 0) statusLed.setBrightness(brightness);

    int restored = 0;
    JsonArray arr = doc["wifiNetworks"].as<JsonArray>();
    for (JsonObject o : arr) {
        String ssid     = o["ssid"]     | "";
        String password = o["password"] | "";
        if (ssid.isEmpty()) continue;
        if (wifiMgr.addNetwork(ssid, password)) restored++;
    }

    String resp = "{\"status\":\"ok\",\"networksRestored\":" + String(restored) + "}";
    _server.send(200, "application/json; charset=utf-8", resp);
}

// ---------------------------------------------------------------------------
// Handler : marque/demarque un equipement comme favori
// Parametres (form-urlencoded) : mac (ou ip) + favorite (1/0)
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiSetFavorite() {
    if (!_hasScan || !_scan.setFavorite) {
        _server.send(503, "application/json", "{\"error\":\"non disponible\"}");
        return;
    }

    String key = _server.arg("mac");
    if (key.isEmpty()) key = _server.arg("ip");
    bool favorite = _server.arg("favorite") == "1";

    if (key.isEmpty()) {
        _server.send(400, "application/json", "{\"error\":\"mac ou ip requis\"}");
        return;
    }

    bool ok = _scan.setFavorite(key, favorite);
    _server.send(ok ? 200 : 404, "application/json",
                 ok ? "{\"status\":\"ok\"}" : "{\"error\":\"equipement introuvable\"}");
}

// ---------------------------------------------------------------------------
// Handler : ajoute une note datee a un equipement (inventaire utilisateur)
// Parametres (form-urlencoded) : mac (ou ip) + text
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiAddNote() {
    if (!_hasScan || !_scan.addNote) {
        _server.send(503, "application/json", "{\"error\":\"non disponible\"}");
        return;
    }

    String key  = _server.arg("mac");
    if (key.isEmpty()) key = _server.arg("ip");
    String text = _server.arg("text");

    if (key.isEmpty() || text.isEmpty()) {
        _server.send(400, "application/json", "{\"error\":\"mac/ip et text requis\"}");
        return;
    }

    bool ok = _scan.addNote(key, text);
    _server.send(ok ? 200 : 404, "application/json",
                 ok ? "{\"status\":\"ok\"}" : "{\"error\":\"equipement introuvable\"}");
}

// ---------------------------------------------------------------------------
// Handler : supprime une note d'un equipement par son timestamp
// Parametres (form-urlencoded) : mac (ou ip) + ts
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiDeleteNote() {
    if (!_hasScan || !_scan.deleteNote) {
        _server.send(503, "application/json", "{\"error\":\"non disponible\"}");
        return;
    }

    String key = _server.arg("mac");
    if (key.isEmpty()) key = _server.arg("ip");
    uint32_t ts = (uint32_t) _server.arg("ts").toInt();

    if (key.isEmpty()) {
        _server.send(400, "application/json", "{\"error\":\"mac ou ip requis\"}");
        return;
    }

    bool ok = _scan.deleteNote(key, ts);
    _server.send(ok ? 200 : 404, "application/json",
                 ok ? "{\"status\":\"ok\"}" : "{\"error\":\"note introuvable\"}");
}

// ---------------------------------------------------------------------------
// Handler : heap/PSRAM libres, espace LittleFS utilise, temps de scan moyens
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiDiagnostics() {
    String json = (_hasScan && _scan.getDiagnosticsJson)
        ? _scan.getDiagnosticsJson()
        : "{\"error\":\"non disponible\"}";
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Handler : etat WiFi + liste des reseaux enregistres (sans mots de passe)
// Exemple de reponse :
//   {"connected":true,"ssid":"Livebox","ip":"192.168.1.42","rssi":-55,
//    "networks":[{"ssid":"Livebox"},{"ssid":"Atelier"}]}
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiWifiGet() {
    JsonDocument doc;
    doc["connected"] = wifiMgr.isConnected();
    doc["ssid"]       = wifiMgr.ssid();
    doc["ip"]         = wifiMgr.localIP();
    doc["rssi"]       = wifiMgr.rssi();
    JsonArray arr = doc["networks"].to<JsonArray>();
    for (const auto& c : wifiMgr.savedNetworks()) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = c.ssid;   // le mot de passe n'est jamais renvoye au navigateur
    }

    String json;
    serializeJson(doc, json);
    _server.sendHeader("Cache-Control", "no-cache");
    _server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Handler : ajoute ou met a jour un reseau enregistre
// Parametres attendus (form-urlencoded) : ssid + password
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiWifiPost() {
    String ssid     = _server.arg("ssid");
    String password = _server.arg("password");

    if (ssid.isEmpty()) {
        _server.send(400, "application/json", "{\"error\":\"ssid requis\"}");
        return;
    }

    bool ok = wifiMgr.addNetwork(ssid, password);
    _server.send(ok ? 200 : 500, "application/json",
                 ok ? "{\"status\":\"ok\"}" : "{\"error\":\"echec de l'enregistrement\"}");
}

// ---------------------------------------------------------------------------
// Handler : supprime un reseau enregistre
// Parametre attendu : ssid
// ---------------------------------------------------------------------------
void WebServerModule::_handleApiWifiDelete() {
    String ssid = _server.arg("ssid");
    if (ssid.isEmpty()) {
        _server.send(400, "application/json", "{\"error\":\"ssid requis\"}");
        return;
    }

    bool ok = wifiMgr.removeNetwork(ssid);
    _server.send(ok ? 200 : 404, "application/json",
                 ok ? "{\"status\":\"ok\"}" : "{\"error\":\"reseau introuvable\"}");
}

// ---------------------------------------------------------------------------
// Handler : route inconnue (404)
// ---------------------------------------------------------------------------
void WebServerModule::_handleNotFound() {
    _server.send(404, "text/plain", "Not Found");
}
