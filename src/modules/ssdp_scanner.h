/**
 * SSDPScanner — Découverte des équipements UPnP/SSDP (Niveau 4)
 *
 * Protocole SSDP (Simple Service Discovery Protocol) :
 *   1. Envoi d'un M-SEARCH en multicast UDP (239.255.255.250:1900)
 *   2. Collecte des réponses pendant SSDP_LISTEN_TIMEOUT_MS
 *   3. Extraction de l'URL de description (champ "LOCATION:")
 *   4. Requête HTTP GET pour récupérer le XML de description UPnP
 *   5. Extraction : friendlyName, modelName, manufacturer, deviceType
 *   6. Catégorisation automatique (Router, Speaker, NAS, TV, SmartHub…)
 *
 * Intégration :
 *   - discover() est appelé depuis la tâche FreeRTOS du NetworkScanner
 *   - Retourne une liste de NetworkDevice à merger dans les résultats ARP
 *   - Non bloquant pour le serveur web (s'exécute dans la tâche Core 0)
 */

#pragma once
#include <Arduino.h>
#include <vector>
#include "network_scanner.h"

// Durée d'écoute SSDP après le M-SEARCH (ms)
#define SSDP_LISTEN_TIMEOUT_MS  3000
// Timeout HTTP pour récupérer le XML de description d'un device (ms)
#define SSDP_HTTP_TIMEOUT_MS    1500
// Nombre maximum de devices SSDP traités par scan
#define SSDP_MAX_DEVICES        20
// Taille maximale du body XML lu (octets)
#define SSDP_MAX_XML_SIZE       4096

class SSDPScanner {
public:
    /**
     * Lance un scan SSDP complet :
     *   - Envoie M-SEARCH multicast
     *   - Collecte les réponses LOCATION:
     *   - Fetch + parse chaque description XML
     *   - Catégorise automatiquement
     *
     * @return Liste de NetworkDevice enrichis (un par IP unique découverte)
     */
    std::vector<NetworkDevice> discover();

private:
    // Envoi M-SEARCH et collecte des URLs LOCATION uniques
    std::vector<String> _sendMSearch();

    // Requête HTTP GET sur l'URL de description + parsing XML
    // Retourne true si au moins un champ utile a été extrait
    bool _fetchAndParse(const String& locationUrl, NetworkDevice& out);

    // Extraction robuste d'une valeur entre balises XML (supporte namespaces)
    static String _xmlTag(const String& xml, const String& tag);

    // Catégorisation automatique depuis les métadonnées UPnP
    static void _categorize(NetworkDevice& dev,
                             const String& deviceType,
                             const String& manufacturer,
                             const String& modelName,
                             const String& friendlyName);
};

// Instance globale
extern SSDPScanner ssdpScanner;
