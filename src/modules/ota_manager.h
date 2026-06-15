/**
 * OtaManager — Mise à jour du firmware Over-The-Air (OTA)
 *
 * Deux méthodes de mise à jour sont proposées :
 *
 *   1. ArduinoOTA (réseau) — mise à jour depuis PlatformIO IDE en Wi-Fi.
 *      Commande : pio run --target upload --upload-port <hostname>.local
 *      Avantage : rapide, intégré à l'IDE, pas besoin de câble USB.
 *
 *   2. Web OTA (navigateur) — upload d'un fichier .bin via la page /update.
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
    void begin(const char* hostname);

    // Traitement des paquets ArduinoOTA — appeler dans loop() à chaque itération
    void loop();

    // Enregistrement des routes HTTP pour la mise à jour web :
    //   GET  /update — affiche le formulaire d'upload
    //   POST /update — reçoit et installe le firmware .bin
    void registerRoutes(WebServer& server);
};

// Instance globale
extern OtaManager otaMgr;
