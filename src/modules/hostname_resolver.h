/**
 * HostnameResolver — Résolution des noms d'hôtes des équipements réseau
 *
 * Deux mécanismes complémentaires, appliqués par ordre de priorité :
 *
 *   1. mDNS passif (priorité haute)
 *      Écoute les annonces multicast sur 224.0.0.251:5353 pendant le scan ARP.
 *      Les équipements actifs émettent spontanément leurs enregistrements A
 *      (.local). On construit une table IP → hostname.local sans interrogation
 *      active. Dépend de SO_REUSEADDR pour coexister avec ESPmDNS sur le port.
 *
 *   2. PTR DNS actif batch (fallback)
 *      Pour chaque IP sans hostname mDNS, envoie une requête DNS PTR à
 *      `d.c.b.a.in-addr.arpa` au serveur DNS du réseau (routeur/box).
 *      Toutes les requêtes sont envoyées en parallèle ; une seule fenêtre
 *      d'attente de 500 ms couvre l'ensemble. Retourne les hostnames DHCP
 *      enregistrés par le routeur (ex: "livebox", "freebox-server").
 *
 * Priorité de résolution : mDNS > PTR DNS
 * Non bloquant si le réseau ne répond pas : timeout total ≤ 500 ms.
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
    // Ouvre le socket multicast mDNS — appeler avant le début du scan ARP.
    // Échoue gracieusement si le port est verrouillé par le stack mDNS.
    void begin();

    // Traite les paquets mDNS en attente dans le buffer du socket (non bloquant).
    // Appeler périodiquement pendant le scan pour peupler le cache mDNS.
    void update();

    // Libère le socket UDP multicast.
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

    // Parse un paquet DNS/mDNS complet et extrait les enregistrements A
    // (nom.local → IPv4) dans _mdnsCache.
    void _parseDnsPacket(const uint8_t* buf, int len);

    // Encode une IP en nom PTR DNS : "192.168.1.20" → "20.1.168.192.in-addr.arpa"
    // Retourne false si l'IP est malformée.
    static bool _buildPtrName(const String& ip, uint8_t* pkt, int& pos);

    // Construit et envoie une requête DNS PTR pour une IP.
    // Retourne l'ID DNS utilisé (pour matcher la réponse), 0 en cas d'erreur.
    static uint16_t _sendPtrQuery(WiFiUDP& udp, const IPAddress& dns, const String& ip, uint16_t id);

    // Parse la réponse d'une requête PTR et retourne le premier hostname trouvé.
    String _parsePtrResponse(const uint8_t* buf, int len, uint16_t expectedId) const;

    WiFiUDP _udp;                          // Socket dédié à l'écoute mDNS multicast
    bool    _listening = false;            // true si le socket multicast est ouvert

    std::map<String, String> _mdnsCache;  // IP → hostname.local (sans ".local")
    std::map<String, String> _ptrCache;   // IP → hostname PTR DNS
};

// Instance globale — partagée entre NetworkScanner et tout futur module réseau
extern HostnameResolver hostnameResolver;
