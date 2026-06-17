/**
 * WiFiManager — Connexion WiFi multi-réseaux, NVS et portail de configuration
 *
 * Hiérarchie de configuration (priorité décroissante) :
 *   1. Réseaux enregistrés en NVS (Preferences, namespace "wifi")
 *   2. DEFAULT_WIFI_SSID / DEFAULT_WIFI_PASSWORD dans include/secrets.h (dev)
 *   3. Portail de configuration (point d'accès "GatewayLab-Setup")
 *
 * Fonctionnement :
 *   - begin() tente la connexion selon la hiérarchie ci-dessus.
 *   - Si aucun réseau ne répond, un point d'accès + portail captif sont
 *     démarrés : l'utilisateur saisit son WiFi via navigateur, les
 *     identifiants sont enregistrés en NVS, puis l'ESP32 redémarre.
 *   - loop() surveille la connexion (mode normal) ou sert le portail
 *     (mode point d'accès) — appeler sans interruption.
 *
 * Portabilité : dépend uniquement de secrets.h (optionnel) et app_config.h.
 */

#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>

// Un réseau WiFi enregistré (SSID + mot de passe en clair en NVS)
struct WifiCredential {
    String ssid;
    String password;
};

class WiFiManager {
public:
    // Type du callback appelé après la tentative de connexion initiale
    // connected = true si la connexion a réussi, false sinon
    using Callback = std::function<void(bool connected)>;

    // Connexion WiFi initiale — bloquante jusqu'à WIFI_CONNECT_TIMEOUT ms
    // par réseau connu. Si aucun réseau ne répond, démarre le portail de
    // configuration : cb(false) est alors appelé une seule fois, puis
    // begin() ne revient pas tant que l'utilisateur n'a pas configuré le WiFi
    // (l'ESP32 redémarre automatiquement après enregistrement).
    void begin(Callback cb = nullptr);

    // Surveillance de la connexion (mode normal) ou du portail (mode AP)
    // À appeler dans loop() sans interruption.
    void loop();

    bool    isConnected() const;  // true si WiFi actif en mode station
    bool    isApMode()    const;  // true si le portail de configuration est actif
    String  ssid()        const;  // Nom du réseau connecté
    String  localIP()     const;  // Adresse IP locale (station ou point d'accès)
    int8_t  rssi()        const;  // Force du signal en dBm (négatif, proche de 0 = fort)
    String  hostname()    const;  // Nom mDNS (depuis MDNS_HOSTNAME dans app_config.h)

    // Réseaux enregistrés en NVS (mots de passe inclus — usage serveur uniquement)
    std::vector<WifiCredential> savedNetworks() const;

    // Ajoute un réseau, ou met à jour son mot de passe s'il existe déjà
    bool addNetwork(const String& ssid, const String& password);

    // Supprime un réseau enregistré — false si le SSID est inconnu
    bool removeNetwork(const String& ssid);
};

// Instance globale — accessible depuis n'importe quel module via #include
extern WiFiManager wifiMgr;
