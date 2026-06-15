/**
 * DeviceApis — Modules d'enrichissement via APIs propriétaires (Niveau 5)
 *
 * Ces modules interrogent les APIs publiques (sans authentification) des
 * équipements détectés lors du scan SSDP ou ARP, pour extraire des
 * informations précises : modèle exact, version firmware, OS, etc.
 *
 * Modules disponibles :
 *   - Philips Hue Bridge : GET /api/config           → source = "HueAPI"
 *   - Synology DSM       : GET /webapi/query.cgi…    → source = "SynologyAPI"
 *   - Freebox            : GET /api_version           → source = "FreeboxAPI"
 *
 * Tous les appels HTTP sont synchrones mais avec timeout court (≤ 2 s).
 * Appelés depuis la tâche FreeRTOS du NetworkScanner — pas de blocage UI.
 */

#pragma once
#include <Arduino.h>
#include "network_scanner.h"

// Timeout HTTP pour les appels d'API (ms)
#define DEVICE_API_TIMEOUT_MS  2000

class DeviceApis {
public:
    /**
     * Analyse le device et applique le module d'enrichissement approprié.
     * Détecte automatiquement Hue Bridge, Synology NAS ou Freebox.
     * @return true si un module a été appliqué avec succès
     */
    static bool enrichIfApplicable(NetworkDevice& dev);

    // --- Modules spécifiques (peuvent être appelés directement) ---

    /** Philips Hue Bridge — GET /api/config (sans authentification) */
    static bool enrichHueBridge(NetworkDevice& dev);

    /** Synology DSM — GET /webapi/query.cgi (endpoint public) */
    static bool enrichSynologyNas(NetworkDevice& dev);

    /** Freebox — GET /api_version + /api/vX/system/ (endpoints publics) */
    static bool enrichFreebox(NetworkDevice& dev);

private:
    // Effectue GET http://ip:port/path et retourne le corps de la réponse.
    // Retourne "" en cas d'erreur ou de timeout.
    static String _httpGet(const String& ip, uint16_t port, const String& path);
};
