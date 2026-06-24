/**
 * DhcpSniffer — Fingerprinting passif DHCP (UDP 67)
 *
 * Quand un equipement rejoint le reseau ou renouvelle son bail, il envoie
 * un DHCPDISCOVER/DHCPREQUEST en broadcast (255.255.255.255:67). Ce paquet
 * contient, en clair, des informations bien plus fiables que l'OUI MAC :
 *
 *   - Option 12 (Host Name)               : le nom choisi par l'OS lui-meme
 *   - Option 60 (Vendor Class Identifier) : identifie souvent l'OS/firmware
 *     (ex: "MSFT 5.0" = Windows, "android-dhcp-x" = Android, "dhcpcd-x" /
 *     "udhcp x.x.x" = Linux/embarque)
 *   - Option 55 (Parameter Request List)  : conservee a titre informatif
 *     mais non utilisee pour deviner l'OS (signature trop instable/ambigue
 *     pour etre fiable sans base de signatures externe)
 *
 * Particularite : ce module est purement **passif**. Aucune requete n'est
 * jamais emise, aucun port n'est sonde — un simple socket UDP ecoute le
 * port 67 (port serveur DHCP) en INADDR_ANY, ce qui suffit a lwIP pour
 * delivrer les broadcasts DHCP des autres clients du sous-reseau. Cela
 * fonctionne meme en l'absence de serveur DHCP local : le Gateway ne
 * repond jamais aux DISCOVER recus, il les observe uniquement.
 *
 * Consequence directe du caractere passif : ce module n'ajoute STRICTEMENT
 * rien au cout du scan complet ni de la passe precise — il tourne en
 * continu, independamment, et se contente d'enrichir une table MAC ->
 * empreinte que NetworkScanner consulte (lecture memoire uniquement) lors
 * de l'enrichissement final de chaque equipement deja decouvert par ARP.
 */

#pragma once
#include <Arduino.h>
#include <map>

struct DhcpFingerprint {
    String   hostname;      // Option 12
    String   vendorClass;   // Option 60 (texte brut)
    String   osGuess;       // Deduit de vendorClass via table de signatures connues
    uint32_t lastSeen = 0;  // millis() de la derniere trame captee pour ce MAC
};

class DhcpSniffer {
public:
    // Ouvre le socket d'ecoute passive UDP/67. A appeler une fois le WiFi
    // connecte (comme les autres services reseau). Echec silencieux (log
    // uniquement) si le port est indisponible — le reste du firmware n'en
    // depend pas.
    void begin();

    // A appeler depuis loop() : draine les paquets en attente (non bloquant,
    // plafonne a quelques paquets par appel pour ne jamais bloquer loop()).
    void loop();

    // Recherche l'empreinte DHCP la plus recente pour un MAC donne
    // (format "XX:XX:XX:XX:XX:XX", insensible a la casse). Chaine vide /
    // struct par defaut si rien n'a ete capte pour ce MAC.
    bool lookup(const String& mac, DhcpFingerprint& out) const;

private:
    void _handlePacket(const uint8_t* buf, int len);
    static String _macToString(const uint8_t* mac6);
    static String _guessOs(const String& vendorClass);

    int _sock = -1;
    mutable SemaphoreHandle_t _mutex = nullptr;
    std::map<String, DhcpFingerprint> _table;   // MAC -> empreinte (borne, cf. .cpp)
};

extern DhcpSniffer dhcpSniffer;
