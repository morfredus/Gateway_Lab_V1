/**
 * DnsSdScanner — Découverte de services DNS-SD (RFC 6763 / RFC 6762 mDNS)
 *
 * DNS-SD (DNS Service Discovery) permet de trouver les *services* exposés par
 * chaque équipement du réseau : HTTP, SSH, SMB, AirPlay, HomeKit, Chromecast…
 *
 * Fonctionnement (depuis v0.8.2) :
 *   Interroger directement le composant mDNS d'ESP-IDF (`mdns_query_ptr()`)
 *   déjà initialisé par `MDNS.begin()` (ESPmDNS, voir wifi_manager.cpp) au
 *   lieu d'ouvrir un socket multicast dédié. Avant v0.8.2, ce scanner ouvrait
 *   son propre `WiFiUDP` sur 224.0.0.251:5353, qui entrait en conflit avec le
 *   socket exclusif du composant mDNS d'ESP-IDF (déjà actif en permanence dès
 *   que `MDNS.begin()` a réussi) — voir docs/WARNINGS.md.
 *
 *   1. Pour chaque type de service connu, interroger `mdns_query_ptr()` avec
 *      une fenêtre d'au moins 300 ms (RFC 6762 §6 : délai aléatoire de
 *      réponse 20-120 ms sur les enregistrements partagés — voir
 *      `MIN_QUERY_TIMEOUT_MS` dans dns_sd_scanner.cpp, corrigé en v0.8.3
 *      après un scan retournant systématiquement zéro résultat)
 *   2. Chaque résultat fournit directement hostname, port, TXT records et
 *      adresse(s) IPv4 — pas de parsing DNS manuel nécessaire
 *   3. Retourner une map IP → DnsSdInfo (services, modèle, catégorie, hostname)
 *
 * Non bloquant pour le reste du firmware : le scan tourne dans la tâche
 * FreeRTOS dédiée du scanner réseau (voir docs/WARNINGS.md). `timeout_ms`
 * est une cible répartie entre les types de service interrogés, plancher à
 * 300 ms chacun — la durée réelle peut donc dépasser `timeout_ms` lorsque le
 * nombre de types de service est élevé.
 *
 * Types de services interrogés (home network) :
 *   Web     : _http._tcp, _https._tcp
 *   Accès   : _ssh._tcp, _sftp-ssh._tcp, _smb._tcp, _afpovertcp._tcp, _ftp._tcp
 *   Apple   : _airplay._tcp, _raop._tcp, _homekit._tcp, _daap._tcp, _device-info._tcp
 *   Google  : _googlecast._tcp
 *   Musique : _sonos._tcp, _spotify-connect._tcp
 *   IoT     : _hue._tcp, _esphome._tcp, _home-assistant._tcp, _mqtt._tcp
 *   Print   : _ipp._tcp, _printer._tcp
 *   Infra   : _nfs._tcp
 */

#pragma once
#include <Arduino.h>
#include <map>

// ─── Résultat DNS-SD pour un équipement ────────────────────────────────────
// services : labels des services séparés par '|'  ex: "HTTP|SSH|SMB"
// model    : valeur du TXT record md= ou fn= (si disponible)
// hostname : cible du record SRV (ex: "nas.local")
// category : suggestion de catégorie déduite des services trouvés
struct DnsSdInfo {
    String   services;   // "HTTP|SSH|AirPlay" — séparateur '|'
    String   model;      // Depuis le TXT record (md=, fn=, model=...)
    String   hostname;   // Depuis le SRV record target
    String   category;   // Suggestion basée sur les services détectés
};

class DnsSdScanner {
public:
    // Lancer un scan DNS-SD complet.
    // Retourne une map IP → DnsSdInfo à fusionner dans NetworkDevice.
    // timeout_ms : fenêtre d'écoute totale cible, répartie entre les types de
    // service (plancher de 300 ms chacun — la durée réelle peut dépasser
    // timeout_ms, voir dns_sd_scanner.cpp).
    std::map<String, DnsSdInfo> scan(uint32_t timeout_ms = 9000);

private:
    // Déduire la meilleure catégorie depuis une liste de service labels
    static String _inferCategory(const String& services);
};

extern DnsSdScanner dnsSdScanner;
