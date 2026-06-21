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
#include "modules/wifi_manager.h"      // Connexion WiFi multi-réseaux, NVS et portail de configuration
#include "modules/ota_manager.h"       // Mise à jour firmware (réseau + web)
#include "modules/web_server.h"        // Serveur HTTP et interface web
#include "modules/network_scanner.h"   // Découverte des équipements LAN
#include "modules/device_store.h"      // Persistance LittleFS
#include "modules/device_history.h"    // Journal chronologique des evenements
#include "modules/time_sync.h"         // Synchronisation NTP (firstSeen/lastSeen)
#include "modules/status_led.h"        // Pilotage de la NeoPixel d'etat
#include "modules/boot_button.h"       // Gestes du bouton BOOT (court/maintien 3s)
#include "modules/system_health.h"     // Garde-fou heap — mode degrade (pas de redemarrage auto)
#ifdef BOOT_LOG_ENABLED
#include "modules/boot_log.h"          // [DEBOGAGE TEMPORAIRE] Journal de redemarrage — voir boot_log.h
#endif

// Suit les transitions du scan en cours pour piloter la LED (Scanning -> Ready)
static bool _ledScanInProgress = false;

void setup() {
    Serial.begin(115200);

#ifdef BOOT_LOG_ENABLED
    // Doit s'executer avant le premier Log::* pour persister correctement
    // la raison du reset precedent et son journal capture
    bootLog.begin();
    bootLog.setDevicesCountProvider([] { return (uint32_t)netScanner.getStats().known; });
#endif

    Log::i("Main", "=== %s v%s ===", PROJECT_NAME, PROJECT_VERSION);

    systemHealth.begin();

    // Montage LittleFS — doit être fait avant la connexion WiFi
    deviceStore.begin();
    deviceHistory.begin();

    // NeoPixel d'etat — initialisee tot pour afficher le pulse bleu de demarrage
    // pendant toute la phase de connexion WiFi
    statusLed.begin();
    statusLed.setState(LedState::Boot);

    // Scanner reseau — le mutex doit exister avant que le bouton BOOT (qui
    // peut declencher un scan) ne soit actif ; la tache de scan elle-meme
    // n'est lancee qu'a la demande (startScan())
    netScanner.begin();

    // Bouton BOOT — appui court (scan) / maintien 3s (sauvegarde)
    bootButton.begin({
        .onShortPress = [] {
#ifdef BOOT_LOG_ENABLED
            bootLog.setLastTask("Scan reseau (bouton BOOT)");
#endif
            netScanner.startScan();
        },
        .onHold       = [] {
            statusLed.setState(LedState::Saving, 1500);
#ifdef BOOT_LOG_ENABLED
            bootLog.setLastTask("Sauvegarde JSON (bouton BOOT)");
#endif
            netScanner.saveNow();
        },
    });

    // Connexion WiFi — le callback est appelé une fois la connexion établie
    // (ou en cas d'échec après WIFI_CONNECT_TIMEOUT millisecondes)
    wifiMgr.begin([](bool connected) {
        if (!connected) {
            // Echec de connexion -> portail de configuration WiFi actif
            statusLed.setState(LedState::WifiPortal);
            return;
        }

        statusLed.setState(LedState::Ready);

        // Synchronisation NTP - necessaire pour l'historique (firstSeen/lastSeen)
        timeSync.begin();

#ifdef ENABLE_OTA
        // Mise à jour OTA disponible sur le réseau local (ArduinoOTA)
        // et via la page /update du serveur web
        otaMgr.begin(MDNS_HOSTNAME);
#endif

        // Scan automatique a la connexion WiFi
#ifdef BOOT_LOG_ENABLED
        bootLog.setLastTask("Scan reseau (connexion WiFi)");
#endif
        netScanner.startScan();

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
            .resetDevices    = [](bool keepAlias, bool keepManufacturer) {
                return netScanner.resetDevices(keepAlias, keepManufacturer);
            },
            .rescanDevice    = [](const String& ip, bool deep) {
                return netScanner.rescanDevice(ip, deep);
            },
            .getRescanStatusJson = [] {
                return netScanner.rescanStatusToJson();
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
            .clearHistory    = [] { deviceHistory.clear(); },
            .getBackupJson   = [] { return netScanner.backupToJson(); },
            .restoreFromJson = [](const String& json) { return netScanner.restoreFromJson(json); },
            .getDevicesCsv   = [] { return netScanner.devicesToCsv(); },
            .setFavorite     = [](const String& macOrIp, bool favorite) {
                return netScanner.setFavorite(macOrIp, favorite);
            },
            .addNote         = [](const String& macOrIp, const String& text) {
                return netScanner.addNote(macOrIp, text);
            },
            .deleteNote      = [](const String& macOrIp, uint32_t ts) {
                return netScanner.deleteNote(macOrIp, ts);
            },
            .getDiagnosticsJson = [] { return netScanner.diagnosticsToJson(); },
            .acknowledgeNewDevices = [] {
                netScanner.acknowledgeNewDevices();
                // Ne pas écraser un statut d'incident (ex: mode dégradé) en cours.
                if (statusLed.state() == LedState::NewDevice && !systemHealth.isDegraded()) {
                    statusLed.setState(LedState::Ready);
                }
            },
            .setMobility     = [](const String& macOrIp, const String& mode) {
                return netScanner.setMobility(macOrIp, mode);
            },
            .getNetworkHealthJson = [] { return netScanner.networkHealthToJson(); },
            .getMonitorInterval   = [] { return netScanner.getMonitorInterval(); },
            .setMonitorInterval   = [](int minutes) { netScanner.setMonitorInterval(minutes); },
            .getMonitorEnabled    = [] { return netScanner.getMonitorEnabled(); },
            .setMonitorEnabled    = [](bool enabled) { netScanner.setMonitorEnabled(enabled); },
        });
        webSrv.begin(WEB_SERVER_PORT);
#endif
    });
}

void loop() {
#ifdef BOOT_LOG_ENABLED
    // Heartbeat + instantane periodique (RuntimeStats/WiFi) — voir boot_log.h
    bootLog.service();
#endif

    // Garde-fou mémoire — bascule en mode dégradé si le heap devient critique
    // (voir modules/system_health.h) ; aucun redémarrage automatique.
    systemHealth.loop();

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

    // Surveillance continue (v1.0.0) — sweep ARP leger + drainage des rescans
    // differes, cadence interne geree par serviceMonitor() lui-meme
    netScanner.serviceMonitor();

    // NetworkScanner n'a pas de loop() : il tourne en tâche FreeRTOS sur Core 0
    // — on suit ses transitions ici pour piloter la LED d'etat
    bool scanning = netScanner.isScanRunning();
    if (scanning && !_ledScanInProgress) {
        statusLed.setState(LedState::Scanning);
    } else if (!scanning && _ledScanInProgress) {
        if (netScanner.hasNewDevices()) {
            statusLed.setState(LedState::NewDevice);   // Reste en NewDevice jusqu'a l'acquittement
        } else {
            statusLed.setState(LedState::Ready);
        }
    }
    _ledScanInProgress = scanning;

    // Anime la NeoPixel et lit le bouton BOOT (non bloquant)
    statusLed.loop();
    bootButton.loop();
}
