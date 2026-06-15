/**
 * HostnameResolver — Implémentation
 *
 * Bibliothèques utilisées :
 *   WiFiUdp  — socket UDP pour l'écoute mDNS multicast et les requêtes PTR DNS
 *   WiFi     — accès au serveur DNS du réseau (WiFi.dnsIP())
 *
 * Format des paquets DNS (RFC 1035 / RFC 6762 pour mDNS) :
 *   Header (12 octets) : ID(2) Flags(2) QDCOUNT(2) ANCOUNT(2) NSCOUNT(2) ARCOUNT(2)
 *   Questions : QDCOUNT × { Name TTL Type Class }
 *   Réponses  : ANCOUNT × { Name Type(2) Class(2) TTL(4) RDLen(2) RData }
 *   Autorité  : NSCOUNT × RR
 *   Additionnel : ARCOUNT × RR
 *   Noms DNS compressés : label normal (\x06google…) ou pointeur (\xc0\x0c)
 */

#include "hostname_resolver.h"
#include <WiFi.h>
#include "../utils/logger.h"

static const char* TAG = "Resolver";

// Groupe multicast mDNS — RFC 6762
static const IPAddress MDNS_GROUP(224, 0, 0, 251);
static constexpr uint16_t MDNS_PORT      = 5353;
static constexpr uint16_t DNS_PORT       = 53;
static constexpr uint32_t PTR_TIMEOUT_MS = 500;   // Fenêtre d'attente batch PTR

// Compteur d'ID DNS — incrémenté à chaque requête pour matcher les réponses
static uint16_t _dnsIdCounter = 0x3000;

// Instance globale exportée
HostnameResolver hostnameResolver;

// ---------------------------------------------------------------------------
// mDNS — Écoute multicast passive
// ---------------------------------------------------------------------------

void HostnameResolver::begin() {
    if (_listening) return;
    // SO_REUSEADDR activé par beginMulticast — coexistence avec le stack ESPmDNS
    if (_udp.beginMulticast(MDNS_GROUP, MDNS_PORT)) {
        _listening = true;
        Log::i(TAG, "Écoute mDNS multicast démarrée (224.0.0.251:5353)");
    } else {
        Log::w(TAG, "Impossible de rejoindre 224.0.0.251:5353 — PTR DNS uniquement");
    }
}

void HostnameResolver::update() {
    if (!_listening) return;
    int psize;
    while ((psize = _udp.parsePacket()) > 0) {
        uint8_t buf[512];
        int len = _udp.read(buf, sizeof(buf));
        if (len > 0) _parseDnsPacket(buf, len);
    }
}

void HostnameResolver::end() {
    if (_listening) {
        _udp.stop();
        _listening = false;
    }
}

void HostnameResolver::clearCaches() {
    _mdnsCache.clear();
    _ptrCache.clear();
}

// ---------------------------------------------------------------------------
// Décodage d'un nom DNS avec gestion des pointeurs de compression RFC 1035
//
// Les noms DNS sont encodés comme une suite de labels :
//   \x09 raspberry \x02 pi \x05 local \x00
// Un octet \xc0 suivi d'un offset indique un pointeur de compression :
//   \xc0 \x0c = "lire le nom depuis l'offset 12 du paquet"
// ---------------------------------------------------------------------------
String HostnameResolver::_decodeDnsName(const uint8_t* buf, int len,
                                         int offset, int& out_next) const {
    String name;
    bool   jumped  = false;
    int    safety  = 0;          // Protection contre les boucles infinies

    while (offset < len && safety++ < 128) {
        uint8_t c = buf[offset];

        if (c == 0x00) {
            // Fin du nom
            if (!jumped) out_next = offset + 1;
            return name;
        }

        if ((c & 0xC0) == 0xC0) {
            // Pointeur de compression
            if (offset + 1 >= len) { out_next = -1; return ""; }
            if (!jumped) out_next = offset + 2;
            offset = ((c & 0x3F) << 8) | buf[offset + 1];
            jumped = true;
            continue;
        }

        // Label normal
        int labelLen = (int)c;
        offset++;
        if (offset + labelLen > len) { out_next = -1; return ""; }
        if (!name.isEmpty()) name += '.';
        for (int i = 0; i < labelLen; i++) {
            name += (char)buf[offset++];
        }
    }

    if (!jumped) out_next = offset + 1;
    return name;
}

// ---------------------------------------------------------------------------
// Parse d'un paquet DNS/mDNS — extrait les enregistrements de type A (IPv4)
// présents dans toutes les sections (Answer, Authority, Additional).
//
// Un enregistrement A associe un nom (ex: raspberrypi.local) à une adresse IPv4.
// On supprime le suffixe ".local" pour stocker "raspberrypi" dans le cache.
// ---------------------------------------------------------------------------
void HostnameResolver::_parseDnsPacket(const uint8_t* buf, int len) {
    if (len < 12) return;

    // Flags : QR = bit 15 ; 1 = réponse, 0 = requête
    uint16_t flags = ((uint16_t)buf[2] << 8) | buf[3];
    if (!(flags & 0x8000)) return;   // Ignorer les requêtes

    uint16_t qdCount = ((uint16_t)buf[4] << 8) | buf[5];
    uint16_t anCount = ((uint16_t)buf[6] << 8) | buf[7];
    uint16_t nsCount = ((uint16_t)buf[8] << 8) | buf[9];
    uint16_t arCount = ((uint16_t)buf[10] << 8) | buf[11];

    int pos = 12;

    // Sauter les questions (QNAME + QTYPE(2) + QCLASS(2))
    for (uint16_t q = 0; q < qdCount && pos < len; q++) {
        int next;
        _decodeDnsName(buf, len, pos, next);
        if (next < 0) return;
        pos = next + 4;
    }

    // Parcourir tous les Resource Records (Answer + Authority + Additional)
    int totalRR = anCount + nsCount + arCount;
    for (int r = 0; r < totalRR && pos < len; r++) {
        int nameEnd;
        String name = _decodeDnsName(buf, len, pos, nameEnd);
        if (nameEnd < 0 || nameEnd + 10 > len) return;

        uint16_t rrType = ((uint16_t)buf[nameEnd]     << 8) | buf[nameEnd + 1];
        uint16_t rdlen  = ((uint16_t)buf[nameEnd + 8] << 8) | buf[nameEnd + 9];
        int      rdPos  = nameEnd + 10;

        if (rdPos + (int)rdlen > len) return;

        if (rrType == 1 && rdlen == 4) {
            // Type A : RData = 4 octets IPv4
            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
                     buf[rdPos], buf[rdPos+1], buf[rdPos+2], buf[rdPos+3]);

            // Supprimer le suffixe ".local" pour stocker "raspberrypi"
            String hostname = name;
            if (hostname.endsWith(".local")) {
                hostname = hostname.substring(0, hostname.length() - 6);
            }
            hostname.toLowerCase();

            if (!hostname.isEmpty() && _mdnsCache.find(ipStr) == _mdnsCache.end()) {
                _mdnsCache[ipStr] = hostname;
                Log::d(TAG, "mDNS A: %s → %s", ipStr, hostname.c_str());
            }
        }

        pos = rdPos + rdlen;
    }
}

// ---------------------------------------------------------------------------
// Requêtes PTR DNS batch
//
// Stratégie : envoyer toutes les requêtes PTR en rafale, puis attendre
// les réponses dans une fenêtre unique de PTR_TIMEOUT_MS ms.
// Cela limite le temps total à PTR_TIMEOUT_MS quelque soit le nombre d'IPs.
// ---------------------------------------------------------------------------

bool HostnameResolver::_buildPtrName(const String& ip, uint8_t* pkt, int& pos) {
    int a, b, c, d;
    if (sscanf(ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;

    // Encoder "d.c.b.a.in-addr.arpa\0" en labels DNS
    auto writeLabel = [&](int num) {
        char label[4];
        uint8_t llen = (uint8_t)snprintf(label, sizeof(label), "%d", num);
        pkt[pos++] = llen;
        for (int i = 0; i < llen; i++) pkt[pos++] = (uint8_t)label[i];
    };
    writeLabel(d); writeLabel(c); writeLabel(b); writeLabel(a);

    // "in-addr"
    pkt[pos++] = 7;
    memcpy(pkt + pos, "in-addr", 7); pos += 7;
    // "arpa"
    pkt[pos++] = 4;
    memcpy(pkt + pos, "arpa", 4);    pos += 4;
    // Terminaison
    pkt[pos++] = 0;
    return true;
}

uint16_t HostnameResolver::_sendPtrQuery(WiFiUDP& udp, const IPAddress& dns,
                                          const String& ip, uint16_t id) {
    uint8_t pkt[64];
    memset(pkt, 0, sizeof(pkt));

    // Header DNS
    pkt[0] = id >> 8;   pkt[1] = id & 0xFF;   // ID
    pkt[2] = 0x01;      pkt[3] = 0x00;         // Flags : RD=1 (recursion desired)
    pkt[4] = 0x00;      pkt[5] = 0x01;         // QDCOUNT = 1

    int pos = 12;
    if (!_buildPtrName(ip, pkt, pos)) return 0;

    pkt[pos++] = 0x00; pkt[pos++] = 0x0C;      // QTYPE  = PTR (12)
    pkt[pos++] = 0x00; pkt[pos++] = 0x01;      // QCLASS = IN (1)

    udp.beginPacket(dns, DNS_PORT);
    udp.write(pkt, pos);
    return udp.endPacket() ? id : 0;
}

String HostnameResolver::_parsePtrResponse(const uint8_t* buf, int len,
                                             uint16_t expectedId) const {
    if (len < 12) return "";

    uint16_t id     = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t flags  = ((uint16_t)buf[2] << 8) | buf[3];
    uint16_t anCnt  = ((uint16_t)buf[6] << 8) | buf[7];
    uint16_t qdCnt  = ((uint16_t)buf[4] << 8) | buf[5];

    if (id != expectedId || !(flags & 0x8000) || anCnt == 0) return "";

    int pos = 12;

    // Sauter les questions
    for (uint16_t q = 0; q < qdCnt && pos < len; q++) {
        int next;
        _decodeDnsName(buf, len, pos, next);
        if (next < 0) return "";
        pos = next + 4;
    }

    // Lire les réponses — on cherche le premier PTR (type 12)
    for (uint16_t r = 0; r < anCnt && pos < len; r++) {
        int nameEnd;
        _decodeDnsName(buf, len, pos, nameEnd);
        if (nameEnd < 0 || nameEnd + 10 > len) return "";

        uint16_t rrType = ((uint16_t)buf[nameEnd]     << 8) | buf[nameEnd + 1];
        uint16_t rdlen  = ((uint16_t)buf[nameEnd + 8] << 8) | buf[nameEnd + 9];
        int      rdPos  = nameEnd + 10;

        if (rdPos + (int)rdlen > len) return "";

        if (rrType == 12) {
            // Type PTR : RData = nom DNS encodé
            int dummy;
            String ptrName = _decodeDnsName(buf, len, rdPos, dummy);
            // Supprimer le domaine local (ex: "livebox.home" → "livebox")
            // On garde uniquement le premier label (le plus significatif)
            int dotPos = ptrName.indexOf('.');
            if (dotPos > 0) ptrName = ptrName.substring(0, dotPos);
            ptrName.toLowerCase();
            return ptrName;
        }

        pos = rdPos + rdlen;
    }
    return "";
}

void HostnameResolver::batchPtrDns(const std::vector<String>& ips) {
    IPAddress dnsServer = WiFi.dnsIP();
    if (dnsServer == IPAddress(0, 0, 0, 0)) {
        Log::w(TAG, "PTR DNS : serveur DNS introuvable");
        return;
    }

    WiFiUDP queryUdp;
    if (!queryUdp.begin(0)) {   // Port local aléatoire
        Log::w(TAG, "PTR DNS : impossible d'ouvrir le socket UDP");
        return;
    }

    // Envoyer toutes les requêtes en rafale
    std::map<uint16_t, String> pending;   // id DNS → IP
    for (const auto& ip : ips) {
        uint16_t id = ++_dnsIdCounter;
        uint16_t sent = _sendPtrQuery(queryUdp, dnsServer, ip, id);
        if (sent) pending[id] = ip;
    }

    if (pending.empty()) { queryUdp.stop(); return; }
    Log::i(TAG, "PTR DNS : %u requête(s) envoyée(s) à %s",
           (unsigned)pending.size(), dnsServer.toString().c_str());

    // Fenêtre d'attente unique pour toutes les réponses
    uint32_t start = millis();
    while (millis() - start < PTR_TIMEOUT_MS && !pending.empty()) {
        int rsize = queryUdp.parsePacket();
        if (rsize > 0) {
            uint8_t rbuf[512];
            int rlen = queryUdp.read(rbuf, sizeof(rbuf));
            if (rlen >= 12) {
                uint16_t rid = ((uint16_t)rbuf[0] << 8) | rbuf[1];
                auto it = pending.find(rid);
                if (it != pending.end()) {
                    String name = _parsePtrResponse(rbuf, rlen, rid);
                    if (!name.isEmpty()) {
                        _ptrCache[it->second] = name;
                        Log::d(TAG, "PTR: %s → %s", it->second.c_str(), name.c_str());
                    }
                    pending.erase(it);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    Log::i(TAG, "PTR DNS : %u nom(s) résolu(s) en %lu ms",
           (unsigned)_ptrCache.size(), (unsigned long)(millis() - start));
    queryUdp.stop();
}

// ---------------------------------------------------------------------------
// Résolution publique — priorité mDNS > PTR DNS
// ---------------------------------------------------------------------------
String HostnameResolver::resolve(const String& ip, HostnameSource& out_source) const {
    auto it = _mdnsCache.find(ip);
    if (it != _mdnsCache.end() && !it->second.isEmpty()) {
        out_source = HostnameSource::MDNS;
        return it->second;
    }

    auto it2 = _ptrCache.find(ip);
    if (it2 != _ptrCache.end() && !it2->second.isEmpty()) {
        out_source = HostnameSource::ReverseDNS;
        return it2->second;
    }

    out_source = HostnameSource::None;
    return "";
}
