/**
 * OtaManager — Mise à jour du firmware Over-The-Air (OTA)
 *
 * Deux méthodes de mise à jour sont proposées :
 *
 *   1. ArduinoOTA (réseau) — mise à jour depuis PlatformIO IDE en Wi-Fi.
 *      Commande : pio run --target upload --upload-port <hostname>.local
 *      Avantage : rapide, intégré à l'IDE, pas besoin de câble USB.
 *
 *   2. Web OTA (navigateur) — upload d'un fichier .bin via la page /wifi (Système).
 *      Avantage : accessible depuis n'importe quel navigateur sur le réseau.
 *      Le fichier .bin se trouve dans .pio/build/esp32s3_n16r8/firmware.bin
 *
 * Activé/désactivé via #define ENABLE_OTA dans include/app_config.h
 */

#pragma once
#include <WebServer.h>

class OtaManager {
public:
    // Configuration et démarrage d'ArduinoOTA avec le nom de la carte
    // Idempotent : les callbacks ne sont enregistrés qu'une seule fois ;
    // ArduinoOTA.begin() est rappelé à chaque reconnexion WiFi pour ré-ouvrir le port UDP.
    void begin(const char* hostname);

    // Traitement des paquets ArduinoOTA — appeler dans loop() à chaque itération
    void loop();

    // Enregistrement de la route HTTP pour la mise à jour web :
    //   POST /update — reçoit et installe le firmware .bin
    //   (le formulaire d'upload est intégré à la page /wifi — Système)
    void registerRoutes(WebServer& server);

private:
    bool _callbacksRegistered = false;
};

// Instance globale
extern OtaManager otaMgr;
