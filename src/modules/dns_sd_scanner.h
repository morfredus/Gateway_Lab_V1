/**
 * DnsSdScanner — Découverte de services DNS-SD (RFC 6763 / RFC 6762 mDNS)
 *
 * DNS-SD (DNS Service Discovery) permet de trouver les *services* exposés par
 * chaque équipement du réseau : HTTP, SSH, SMB, AirPlay, HomeKit, Chromecast…
 *
 * Fonctionnement (v0.0.9) :
 *   1. Construction d'un paquet mDNS multi-question pour N types de services
 *   2. Envoi en multicast → 224.0.0.251:5353 (même canal que mDNS)
 *   3. Écoute des réponses PTR + SRV + TXT + A pendant timeout_ms
 *      • PTR : "ce service existe → nom de l'instance"
 *      • SRV : port et hostname de l'instance
 *      • TXT : métadonnées (model, firmware, friendly name…)
 *      • A   : IP du hostname de l'instance
 *   4. Retourne un map IP → DnsSdInfo (services, modèle, catégorie, hostname)
 *
 * Non bloquant : tous les delais sont bornés par timeout_ms.
 * Mémoire : buffers alloués sur le tas, pas de vecteurs imbriqués profonds.
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
#include <WiFiUdp.h>
#include <map>
#include <vector>

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
    // Lance un scan DNS-SD complet.
    // Retourne un map IP → DnsSdInfo à fusionner dans NetworkDevice.
    // timeout_ms : fenêtre d'écoute totale après l'envoi des requêtes.
    std::map<String, DnsSdInfo> scan(uint32_t timeout_ms = 4000);

private:
    // Instance DNS-SD en cours de construction
    struct _Instance {
        String serviceType;    // "_googlecast._tcp"
        String serviceLabel;   // "Cast"
        String instanceName;   // "Living Room TV"
        String hostname;       // "livingroom.local"
        String ip;             // "192.168.1.50" (depuis A record)
        uint16_t port  = 0;
        String model;          // depuis TXT md= ou fn=
        String categoryHint;   // suggestion de catégorie
    };

    // ── Réseau ─────────────────────────────────────────────────────────────

    // Construit et envoie un paquet mDNS avec N questions PTR (un par service type)
    void _sendQueries(WiFiUDP& udp);

    // ── Parsing DNS ────────────────────────────────────────────────────────

    // Traite un paquet UDP reçu — extrait PTR, SRV, TXT, A
    void _processPacket(const uint8_t* buf, int len);

    // Décompression de nom DNS RFC 1035 (gère les pointeurs \xc0\xnn)
    String _decodeName(const uint8_t* buf, int len, int offset, int& next) const;

    // Extrait les métadonnées utiles des TXT records (model, hostname, firmware…)
    void _parseTxt(const uint8_t* rdata, int rdlen, _Instance& inst) const;

    // ── Consolidation ──────────────────────────────────────────────────────

    // Reconstruit la map IP → DnsSdInfo depuis les instances collectées
    std::map<String, DnsSdInfo> _buildResult() const;

    // Déduit la meilleure catégorie depuis une liste de service labels
    static String _inferCategory(const String& services);

    // ── État interne ───────────────────────────────────────────────────────

    // Map hostname.local → IP (peuplée par les records A reçus)
    std::map<String, String>  _hostToIp;

    // Map (instanceName + serviceType) → _Instance
    std::map<String, _Instance> _instances;
};

extern DnsSdScanner dnsSdScanner;
