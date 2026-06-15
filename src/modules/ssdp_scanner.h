/**
 * SsdpScanner — Découverte UPnP/SSDP des équipements réseau
 *
 * Fonctionnement (v0.0.8) :
 *   1. Envoi M-SEARCH multicast UDP → 239.255.255.250:1900
 *   2. Collecte des réponses SSDP (champ LOCATION = URL du descripteur XML)
 *   3. Pour chaque LOCATION unique, HTTP GET du descripteur XML UPnP
 *   4. Extraction robuste des champs XML (friendlyName, manufacturer, model…)
 *   5. Catégorisation automatique des équipements courants
 *   6. Enrichissement optionnel via APIs spécifiques :
 *        - Philips Hue Bridge  → /api/config
 *        - Synology DSM        → /webapi/query.cgi
 *        - Freebox             → /api_version
 *
 * Le scan est non bloquant : toutes les opérations réseau ont un timeout court.
 * XML malformé : le parsing tolère les namespaces, CDATA et encodages partiels.
 *
 * Résultat : std::vector<NetworkDevice> fusionnable avec les résultats ARP.
 */

#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <vector>
#include "network_scanner.h"   // NetworkDevice

class SsdpScanner {
public:
    // Lance un scan SSDP complet et retourne les équipements UPnP découverts.
    // timeout_ms : durée d'écoute des réponses M-SEARCH (3 s recommandé).
    std::vector<NetworkDevice> scan(uint32_t timeout_ms = 3000);

private:
    // Réponse brute à un M-SEARCH : URL du descripteur et IP du device
    struct SsdpResponse {
        String location;   // ex: "http://192.168.1.1:49000/desc.xml"
        String ip;         // ex: "192.168.1.1"
        uint16_t port;     // ex: 49000
        String path;       // ex: "/desc.xml"
    };

    // ── Couche réseau ───────────────────────────────────────────────────────

    // Envoi M-SEARCH et collecte des réponses jusqu'à timeout
    std::vector<SsdpResponse> _discover(uint32_t timeout_ms);

    // Requête HTTP GET simple (pas d'HTTPClient pour économiser la stack)
    // Retourne le body ou "" si échec/timeout
    static String _httpGet(const String& ip, uint16_t port,
                           const String& path, uint32_t timeout_ms = 2000);

    // ── Parsing ─────────────────────────────────────────────────────────────

    // Analyse le descripteur XML et remplit un NetworkDevice
    NetworkDevice _parseDescription(const SsdpResponse& resp, const String& xml);

    // Extraction robuste d'un tag XML : <tag>val</tag> et <ns:tag>val</ns:tag>
    static String _xmlTag(const String& xml, const String& tag);

    // Extraction d'un header HTTP (insensible à la casse) depuis une réponse brute
    static String _httpHeader(const String& response, const String& header);

    // ── Décomposition d'URL ─────────────────────────────────────────────────
    static String   _urlIp(const String& url);
    static uint16_t _urlPort(const String& url);
    static String   _urlPath(const String& url);

    // ── Catégorisation & enrichissement ────────────────────────────────────

    // Catégorisation automatique basée sur manufacturer / model / deviceType
    static void _categorize(NetworkDevice& dev, const String& deviceType);

    // Détection d'appartenance à une famille de produits
    static bool _isHue(const NetworkDevice& dev);
    static bool _isSynology(const NetworkDevice& dev);
    static bool _isFreebox(const NetworkDevice& dev);

    // APIs spécifiques (non authentifiées) pour enrichir les métadonnées
    void _enrichHue(NetworkDevice& dev);
    void _enrichSynology(NetworkDevice& dev);
    void _enrichFreebox(NetworkDevice& dev);
};

extern SsdpScanner ssdpScanner;
