/**
 * HostnameResolver — Résolution des noms d'hôtes des équipements réseau
 *
 * Depuis v0.8.2 : un seul mécanisme actif.
 *
 *   PTR DNS actif batch (seul mécanisme — port 53, unicast)
 *      Pour chaque IP, envoie une requête DNS PTR à `d.c.b.a.in-addr.arpa`
 *      au serveur DNS du réseau (routeur/box). Toutes les requêtes sont
 *      envoyées en parallèle ; une seule fenêtre d'attente de 500 ms couvre
 *      l'ensemble. Retourne les hostnames DHCP enregistrés par le routeur
 *      (ex: "livebox", "freebox-server").
 *
 * L'écoute mDNS passive (224.0.0.251:5353) a été retirée en v0.8.2 : le
 * composant mDNS d'ESP-IDF (initialisé par `MDNS.begin()` dans
 * wifi_manager.cpp) garde ce socket exclusivement pour son propre
 * responder — aucun socket applicatif tiers ne peut le rejoindre, même
 * avec SO_REUSEADDR (voir docs/WARNINGS.md). Il n'existe pas d'API
 * ESP-IDF publique pour écouter passivement les annonces mDNS reçues par
 * ce service ; begin()/update()/end() sont donc conservés comme no-op
 * pour ne pas casser les appelants existants, et `_mdnsCache` /
 * `HostnameSource::MDNS` restent présents mais ne sont plus jamais
 * peuplés. PTR DNS (port 53, sans rapport avec ce conflit) reste le seul
 * mécanisme de résolution.
 */

#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <map>
#include <vector>

// Source ayant produit le nom d'hôte — injectée dans NetworkDevice.source
enum class HostnameSource : uint8_t {
    None,        // Aucune résolution disponible
    MAC,         // Fabricant identifié par OUI seulement (pas de nom d'hôte)
    ReverseDNS,  // Requête PTR DNS (hostname DHCP/local du routeur)
    MDNS,        // Annonce mDNS passive (.local) — priorité maximale
};

// Retourne le label JSON pour une source de résolution
inline const char* hostnameSourceStr(HostnameSource s) {
    switch (s) {
        case HostnameSource::MDNS:       return "mDNS";
        case HostnameSource::ReverseDNS: return "PTR";
        case HostnameSource::MAC:        return "MAC";
        default:                         return "";
    }
}

class HostnameResolver {
public:
    // No-op depuis v0.8.2 (conservé pour compatibilité des appelants).
    // L'écoute mDNS passive est structurellement impossible tant qu'ESPmDNS
    // garde 224.0.0.251:5353 — voir docs/WARNINGS.md.
    void begin();

    // No-op depuis v0.8.2 — voir begin().
    void update();

    // No-op depuis v0.8.2 — voir begin().
    void end();

    // Envoie des requêtes PTR DNS en batch pour toutes les IP fournies,
    // attend les réponses dans une fenêtre unique de PTR_TIMEOUT_MS.
    // Peuple le cache PTR interne.
    void batchPtrDns(const std::vector<String>& ips);

    // Retourne le meilleur hostname disponible pour une IP.
    // Cherche d'abord dans le cache mDNS, puis dans le cache PTR DNS.
    // out_source indique quelle méthode a fourni le résultat.
    String resolve(const String& ip, HostnameSource& out_source) const;

    // Vide les caches (utile entre deux scans)
    void clearCaches();

private:
    // Décode un nom DNS avec gestion de la compression RFC 1035.
    // Retourne le nom et avance out_next après le nom dans le buffer.
    // out_next = -1 en cas d'erreur de décodage.
    String _decodeDnsName(const uint8_t* buf, int len, int offset, int& out_next) const;

    // Encode une IP en nom PTR DNS : "192.168.1.20" → "20.1.168.192.in-addr.arpa"
    // Retourne false si l'IP est malformée.
    static bool _buildPtrName(const String& ip, uint8_t* pkt, int& pos);

    // Construit et envoie une requête DNS PTR pour une IP.
    // Retourne l'ID DNS utilisé (pour matcher la réponse), 0 en cas d'erreur.
    static uint16_t _sendPtrQuery(WiFiUDP& udp, const IPAddress& dns, const String& ip, uint16_t id);

    // Parse la réponse d'une requête PTR et retourne le premier hostname trouvé.
    String _parsePtrResponse(const uint8_t* buf, int len, uint16_t expectedId) const;

    std::map<String, String> _mdnsCache;  // Toujours vide depuis v0.8.2 (conservé pour resolve())
    std::map<String, String> _ptrCache;   // IP → hostname PTR DNS
};

// Instance globale — partagée entre NetworkScanner et tout futur module réseau
extern HostnameResolver hostnameResolver;
