/**
 * WsDiscoveryScanner — Découverte WS-Discovery (ONVIF)
 *
 * Protocole utilisé par la quasi-totalité des caméras IP et imprimantes
 * compatibles ONVIF pour s'annoncer sur le réseau local, indépendamment
 * de SSDP/UPnP.
 *
 * Fonctionnement :
 *   1. Envoi d'une requête SOAP "Probe" en multicast → 239.255.255.250:3702
 *   2. Collecte des réponses "ProbeMatch" pendant timeout_ms
 *      • <wsa:Address>      : URN unique de l'appareil
 *      • <d:Types>          : types annoncés (ex: "dn:NetworkVideoTransmitter"
 *                              pour une caméra ONVIF, "wprt:PrintDeviceType")
 *      • <d:XAddrs>         : URLs de service (contient l'IP réelle)
 *      • <d:Scopes>         : métadonnées libres (souvent le nom/modèle)
 *   3. Retourne un map IP → WsDiscoveryInfo
 *
 * Non bloquant, borné par timeout_ms, comme les autres scanners du projet.
 */

#pragma once
#include <Arduino.h>
#include <map>

struct WsDiscoveryInfo {
    String types;      // ex: "NetworkVideoTransmitter"
    String scopes;      // metadonnees brutes (souvent nom/modele/role)
    String category;    // suggestion deduite des types ("Camera", "Printer"...)
};

class WsDiscoveryScanner {
public:
    // Lance une decouverte WS-Discovery complete (broadcast, toutes IP).
    // timeout_ms : fenetre d'ecoute des ProbeMatch.
    std::map<String, WsDiscoveryInfo> scan(uint32_t timeout_ms = 2000);

private:
    static String _xmlTag(const String& xml, const char* tag);
    static String _ipFromXAddrs(const String& xaddrs);
    static String _inferCategory(const String& types);
};

extern WsDiscoveryScanner wsDiscoveryScanner;
