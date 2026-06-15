/**
 * Gateway Lab V1 — Point d'entrée principal
 *
 * Rôle de ce fichier : orchestration uniquement.
 * Toute la logique métier est dans src/modules/.
 * Pour ajouter une fonctionnalité, créez un nouveau module et appelez
 * son begin() ici, puis son loop() dans la boucle principale.
 */

#include <Arduino.h>
#include "app_config.h"       // Paramètres globaux (port, timeouts, features)
#include "board_config.h"     // Brochage de la carte (GPIO, SPI, I2C...)
#include "utils/logger.h"     // Journalisation série (Log::i, Log::w, Log::e)
#include "modules/wifi_manager.h"      // Connexion WiFi multi-réseaux
#include "modules/ota_manager.h"       // Mise à jour firmware (réseau + web)
#include "modules/web_server.h"        // Serveur HTTP et interface web
#include "modules/network_scanner.h"   // Découverte des équipements LAN

void setup() {
    Serial.begin(115200);
    Log::i("Main", "=== %s v%s ===", PROJECT_NAME, PROJECT_VERSION);

    // Connexion WiFi — le callback est appelé une fois la connexion établie
    // (ou en cas d'échec après WIFI_CONNECT_TIMEOUT millisecondes)
    wifiMgr.begin([](bool connected) {
        if (!connected) return;   // Pas de WiFi = pas de services réseau

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
