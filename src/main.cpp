/**
 * Gateway Lab V1 — Point d'entrée principal
 *
 * Rôle de ce fichier : orchestration uniquement.
 * Toute la logique métier est dans src/modules/.
 * Pour ajouter une fonctionnalité, créez un nouveau module et appelez
 * son begin() ici, puis son loop() dans la boucle principale.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include "app_config.h"       // Paramètres globaux (port, timeouts, features)
#include "board_config.h"     // Brochage de la carte (GPIO, SPI, I2C...)
#include "utils/logger.h"     // Journalisation série (Log::i, Log::w, Log::e)
#include "modules/wifi_manager.h"      // Connexion WiFi multi-réseaux
#include "modules/ota_manager.h"       // Mise à jour firmware (réseau + web)
#include "modules/web_server.h"        // Serveur HTTP et interface web
#include "modules/network_scanner.h"   // Découverte des équipements LAN
#include "modules/device_store.h"      // Persistance LittleFS
#include "modules/device_history.h"    // Journal chronologique des evenements
#include "modules/time_sync.h"         // Synchronisation NTP (firstSeen/lastSeen)

void setup() {
    Serial.begin(115200);
    Log::i("Main", "=== %s v%s ===", PROJECT_NAME, PROJECT_VERSION);

    // Montage LittleFS — doit être fait avant la connexion WiFi
    deviceStore.begin();
    deviceHistory.begin();

    // Connexion WiFi — le callback est appelé une fois la connexion établie
    // (ou en cas d'échec après WIFI_CONNECT_TIMEOUT millisecondes)
    wifiMgr.begin([](bool connected) {
        if (!connected) return;   // Pas de WiFi = pas de services réseau

        // Synchronisation NTP - necessaire pour l'historique (firstSeen/lastSeen)
        timeSync.begin();

#ifdef ENABLE_OTA
        // Mise à jour OTA disponible sur le réseau local (ArduinoOTA)
        // et via la page /update du serveur web
        otaMgr.begin(MDNS_HOSTNAME);
#endif

        // Initialisation du scanner réseau (table ARP + tâche FreeRTOS)
        netScanner.begin();

#ifdef ENABLE_WEB_SERVER
        // Enregistrement du fournisseur de données scanner auprès du serveur web.
        // ScanProvider découple web_server de network_scanner : pas d'include croisé.
        webSrv.registerScanProvider({
            .isScanning  = [] { return netScanner.isScanRunning(); },
            .getJson     = [] { return netScanner.resultsToJson(); },
            .triggerScan = [] { netScanner.startScan(); },
            .getStats    = [] {
                auto s = netScanner.getStats();
                String j = "{\"known\":";
                j += s.known;
                j += ",\"online\":";
                j += s.online;
                j += ",\"offline\":";
                j += s.offline;
                j += "}";
                return j;
            },
            .setAlias        = [](const String& macOrIp, const String& alias) {
                return netScanner.setAlias(macOrIp, alias);
            },
            .getHistoryJson  = [] {
                JsonDocument doc;
                JsonArray arr = doc.to<JsonArray>();
                for (const auto& e : deviceHistory.load(100)) {
                    JsonObject obj = arr.add<JsonObject>();
                    obj["epoch"]    = e.epoch;
                    obj["mac"]      = e.mac;
                    obj["ip"]       = e.ip;
                    obj["label"]    = e.label;
                    obj["event"]    = e.event;
                    obj["field"]    = e.field;
                    obj["oldValue"] = e.oldValue;
                    obj["newValue"] = e.newValue;
                }
                String json;
                serializeJson(doc, json);
                return json;
            },
            .getBackupJson   = [] { return netScanner.backupToJson(); },
            .restoreFromJson = [](const String& json) { return netScanner.restoreFromJson(json); },
        });
        webSrv.begin(WEB_SERVER_PORT);
#endif
    });
}

void loop() {
    // Vérification et reconnexion WiFi automatique (debounce 30 s)
    wifiMgr.loop();

#ifdef ENABLE_OTA
    // Traitement des paquets ArduinoOTA entrants
    otaMgr.loop();
#endif

#ifdef ENABLE_WEB_SERVER
    // Traitement des requêtes HTTP en attente
    webSrv.loop();
#endif

    // NetworkScanner n'a pas de loop() : il tourne en tâche FreeRTOS sur Core 0
}
