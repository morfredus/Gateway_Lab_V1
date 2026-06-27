/**
 * OtaManager — Implémentation
 *
 * Bibliothèques utilisées :
 *   ArduinoOTA — mise à jour réseau depuis PlatformIO ou Arduino IDE
 *   Update     — écriture du firmware en flash (utilisée pour la web OTA)
 */

#include "ota_manager.h"
#include <ArduinoOTA.h>
#include <Update.h>
#include "app_config.h"         // ENABLE_OTA
#include "../utils/logger.h"
#include "boot_log.h"

static const char* TAG = "OTA";

// Instance globale exportée
OtaManager otaMgr;

void OtaManager::begin(const char* hostname) {
#ifdef ENABLE_OTA
    ArduinoOTA.setHostname(hostname);

    if (!_callbacksRegistered) {
        // Callbacks d'état — enregistrés une seule fois pour éviter l'empilement
        ArduinoOTA.onStart([]() {
            Log::i(TAG, "ArduinoOTA: réception du firmware...");
        });
        ArduinoOTA.onEnd([]() {
            Log::i(TAG, "ArduinoOTA: installation terminée, redémarrage...");
        });
        ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
            Log::d(TAG, "Progression : %u%%", p * 100 / t);
        });
        ArduinoOTA.onError([](ota_error_t err) {
            Log::e(TAG, "Erreur OTA [%u] — vérifiez la connexion réseau", (unsigned)err);
        });
        _callbacksRegistered = true;
    }

    // Rappelé à chaque reconnexion WiFi pour ré-ouvrir le port UDP et re-publier mDNS
    ArduinoOTA.begin();
    Log::i(TAG, "ArduinoOTA actif (nom réseau : %s)", hostname);
#endif
}

void OtaManager::loop() {
#ifdef ENABLE_OTA
    // Vérification des paquets OTA entrants — doit être appelé à chaque itération
    // de loop() pour ne pas rater le début d'une mise à jour
    ArduinoOTA.handle();
#endif
}

void OtaManager::registerRoutes(WebServer& server) {
    // Le formulaire d'upload est intégré à la page /wifi (Système) —
    // seule la réception du fichier reste gérée ici.
    // Réception du fichier firmware .bin uploadé depuis le navigateur
    // Deux handlers : le premier s'exécute à la fin, le second pendant l'upload
    server.on("/update", HTTP_POST,
        // Handler de fin : confirme le succès ou l'échec, puis redémarre
        [&server]() {
            server.sendHeader("Connection", "close");
            server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
#ifdef BOOT_LOG_ENABLED
            bootLog.service(true);   // Instantané heap/uptime à jour avant ce redémarrage volontaire
#endif
            delay(500);   // Délai pour que le navigateur reçoive la réponse
            ESP.restart();
        },
        // Handler de données : reçoit les chunks du firmware et les écrit en flash
        [&server]() {
            HTTPUpload& upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                Log::i(TAG, "Web OTA: début de réception — %s", upload.filename.c_str());
                // Démarrage de la mise à jour (taille inconnue = mise à jour en streaming)
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                // Écriture du chunk reçu en mémoire flash
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                // Finalisation et vérification de l'intégrité du firmware
                if (Update.end(true)) {
                    Log::i(TAG, "Web OTA: %u octets écrits — succès", upload.totalSize);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );
}
