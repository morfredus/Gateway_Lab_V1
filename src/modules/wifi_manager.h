/**
 * WiFiManager — Connexion WiFi multi-réseaux et reconnexion automatique
 *
 * Fonctionnement :
 *   1. Lit la liste des réseaux depuis include/secrets.h
 *   2. Tente de se connecter au réseau au signal le plus fort (WiFiMulti)
 *   3. Démarre mDNS pour un accès par nom (gateway-lab-v1.local)
 *   4. Surveille la connexion et se reconnecte si besoin (dans loop())
 *
 * Portabilité : dépend uniquement de secrets.h et app_config.h.
 * Pour adapter à un autre projet, modifier ces deux fichiers suffit.
 */

#pragma once
#include <Arduino.h>
#include <functional>

class WiFiManager {
public:
    // Type du callback appelé après la tentative de connexion initiale
    // connected = true si la connexion a réussi, false sinon
    using Callback = std::function<void(bool connected)>;

    // Connexion WiFi initiale — bloquante jusqu'à WIFI_CONNECT_TIMEOUT ms
    // Le callback cb est appelé une seule fois à l'issue de la tentative
    void begin(Callback cb = nullptr);

    // Surveillance de la connexion — appeler dans loop() sans interruption
    // Déclenche une reconnexion si le WiFi est perdu (debounce 30 s)
    void loop();

    bool    isConnected() const;  // true si WiFi actif
    String  ssid()        const;  // Nom du réseau connecté
    String  localIP()     const;  // Adresse IP locale (ex: "192.168.1.42")
    int8_t  rssi()        const;  // Force du signal en dBm (négatif, proche de 0 = fort)
    String  hostname()    const;  // Nom mDNS (depuis MDNS_HOSTNAME dans app_config.h)
};

// Instance globale — accessible depuis n'importe quel module via #include
extern WiFiManager wifiMgr;
