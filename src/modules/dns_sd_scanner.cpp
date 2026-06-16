/**
 * DnsSdScanner — Implémentation v0.0.9
 *
 * Bibliothèques :
 *   WiFiUdp — socket UDP multicast mDNS (224.0.0.251:5353)
 *   FreeRTOS — vTaskDelay pour l'attente non bloquante
 */

#include "dns_sd_scanner.h"
#include "../utils/logger.h"

static const char* TAG = "DNSSD";

// Adresse et port mDNS (RFC 6762)
static const IPAddress MDNS_GROUP(224, 0, 0, 251);
static constexpr uint16_t MDNS_PORT = 5353;

// Instance globale
DnsSdScanner dnsSdScanner;

// ════════════════════════════════════════════════════════════════════════════
// Table des types de services DNS-SD à interroger
//
// Colonnes : type, label UI, suggestion de catégorie ("" = pas de suggestion)
// ════════════════════════════════════════════════════════════════════════════

struct ServiceEntry {
    const char* type;
    const char* label;
    const char* category;
};

static const ServiceEntry SERVICE_TYPES[] = {
    // Web
    { "_http._tcp",             "HTTP",     ""          },
    { "_https._tcp",            "HTTPS",    ""          },
    // Accès serveur
    { "_ssh._tcp",              "SSH",      "Computer"  },
    { "_sftp-ssh._tcp",         "SFTP",     "NAS"       },
    { "_smb._tcp",              "SMB",      "NAS"       },
    { "_afpovertcp._tcp",       "AFP",      "NAS"       },
    { "_nfs._tcp",              "NFS",      "NAS"       },
    { "_ftp._tcp",              "FTP",      ""          },
    // Écosystème Apple
    { "_airplay._tcp",          "AirPlay",  "TV"        },
    { "_raop._tcp",             "AirPlay",  "Speaker"   },
    { "_homekit._tcp",          "HomeKit",  "SmartHome" },
    { "_daap._tcp",             "iTunes",   "Computer"  },
    { "_device-info._tcp",      "AppleInf", ""          },
    // Google
    { "_googlecast._tcp",       "Cast",     "Streaming" },
    // Musique / Audio
    { "_sonos._tcp",            "Sonos",    "Speaker"   },
    { "_spotify-connect._tcp",  "Spotify",  "Speaker"   },
    // Domotique / IoT
    { "_hue._tcp",              "Hue",      "SmartHub"  },
    { "_esphome._tcp",          "ESPHome",  "IoT"       },
    { "_home-assistant._tcp",   "HA",       "SmartHome" },
    { "_mqtt._tcp",             "MQTT",     ""          },
    // Impression
    { "_ipp._tcp",              "IPP",      "Printer"   },
    { "_printer._tcp",          "Print",    "Printer"   },
    { nullptr, nullptr, nullptr }
};

// Retrouve l'entrée de service par type (ex: "_http._tcp")
static const ServiceEntry* findService(const String& type) {
    for (int i = 0; SERVICE_TYPES[i].type != nullptr; i++) {
        if (type == SERVICE_TYPES[i].type) return &SERVICE_TYPES[i];
    }
    return nullptr;
}

// ════════════════════════════════════════════════════════════════════════════
// Encodage d'un nom DNS en labels (RFC 1035)
// ex: "_http._tcp.local" → \x05_http\x04_tcp\x05local\x00
// ════════════════════════════════════════════════════════════════════════════

static int encodeDnsName(const String& name, uint8_t* buf, int pos) {
    int start = 0;
    int nameLen = (int)name.length();
    while (start <= nameLen) {
        int dot = name.indexOf('.', start);
        int end = (dot < 0) ? nameLen : dot;
        int labelLen = end - start;
        if (labelLen == 0) { buf[pos++] = 0; break; }
        buf[pos++] = (uint8_t)labelLen;
        for (int i = start; i < end; i++) buf[pos++] = (uint8_t)name[i];
        if (dot < 0) { buf[pos++] = 0; break; }
        start = dot + 1;
    }
    return pos;
}

// ════════════════════════════════════════════════════════════════════════════
// Décompression de nom DNS RFC 1035 (gère les pointeurs \xc0\xnn)
// Identique à HostnameResolver::_decodeDnsName (méthode privée non réutilisable)
// ════════════════════════════════════════════════════════════════════════════

String DnsSdScanner::_decodeName(const uint8_t* buf, int len,
                                   int offset, int& next) const {
    String name;
    bool jumped = false;
    int safety = 0;

    while (offset < len && safety++ < 128) {
        uint8_t c = buf[offset];

        if (c == 0x00) {
            if (!jumped) next = offset + 1;
            return name;
        }
        if ((c & 0xC0) == 0xC0) {
            if (offset + 1 >= len) { next = -1; return ""; }
            if (!jumped) next = offset + 2;
            offset = ((c & 0x3F) << 8) | buf[offset + 1];
            jumped = true;
            continue;
        }
        int labelLen = (int)c;
        offset++;
        if (offset + labelLen > len) { next = -1; return ""; }
        if (!name.isEmpty()) name += '.';
        for (int i = 0; i < labelLen; i++) name += (char)buf[offset++];
    }

    if (!jumped) next = offset + 1;
    return name;
}

// ════════════════════════════════════════════════════════════════════════════
// Parsing des TXT records DNS-SD
//
// Format : suite de chaînes précédées d'un octet de longueur.
// Chaque chaîne est "key=value" ou juste "key".
// On extrait les champs utiles : md, model, fn, fw, os, ver
// ════════════════════════════════════════════════════════════════════════════

void DnsSdScanner::_parseTxt(const uint8_t* rdata, int rdlen,
                               _Instance& inst) const {
    int pos = 0;
    while (pos < rdlen) {
        int slen = (int)rdata[pos++];
        if (pos + slen > rdlen) break;

        // Extraire "key=value"
        String kv;
        kv.reserve(slen);
        for (int i = 0; i < slen; i++) kv += (char)rdata[pos + i];
        pos += slen;

        int eq = kv.indexOf('=');
        if (eq < 0) continue;

        String key = kv.substring(0, eq);
        String val = kv.substring(eq + 1);
        key.toLowerCase();
        val.trim();
        if (val.isEmpty()) continue;

        // Champs utiles pour l'identification du device
        // md = model (Chromecast, AppleTV, Synology DS224+…)
        // fn = friendly name (nom configuré par l'utilisateur)
        // model = variante de md
        // fw  = firmware version
        // am  = Apple model identifier (AppleTV6,2…)
        if ((key == "md" || key == "model") && inst.model.isEmpty()) {
            inst.model = val;
        } else if (key == "fn" && inst.model.isEmpty()) {
            inst.model = val;
        } else if (key == "am" && inst.model.isEmpty()) {
            // Apple model code → lisible (ex: "AppleTV6,2" → gardé tel quel)
            inst.model = val;
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Envoi du paquet mDNS multi-question
//
// Un seul paquet UDP contient N questions PTR (une par service type).
// Cela minimise la charge réseau et réduit le délai d'obtention des réponses.
// ════════════════════════════════════════════════════════════════════════════

void DnsSdScanner::_sendQueries(WiFiUDP& udp) {
    // Compter les types de services
    int count = 0;
    for (int i = 0; SERVICE_TYPES[i].type != nullptr; i++) count++;

    // Taille max estimée : 12 (header) + count × ~35 (question)
    // Pour 23 services ≈ 12 + 23 × 35 = 817 octets — dans les limites UDP
    const int BUF_SIZE = 1400;
    uint8_t* pkt = (uint8_t*)malloc(BUF_SIZE);
    if (!pkt) { Log::e(TAG, "Allocation échouée"); return; }
    memset(pkt, 0, 12);

    // Header mDNS (RFC 6762) : ID=0, Flags=0x0000 (query), QD=count
    pkt[0] = 0; pkt[1] = 0;         // ID = 0 (requis par mDNS)
    pkt[2] = 0; pkt[3] = 0;         // Flags = standard query
    pkt[4] = (uint8_t)(count >> 8);
    pkt[5] = (uint8_t)(count & 0xFF); // QDCOUNT

    int pos = 12;
    for (int i = 0; SERVICE_TYPES[i].type != nullptr && pos < BUF_SIZE - 40; i++) {
        // Encoder "_type._tcp.local"
        String fullType = String(SERVICE_TYPES[i].type) + ".local";
        pos = encodeDnsName(fullType, pkt, pos);
        // QTYPE = PTR (12), QCLASS = 0x8001 (IN + QU bit)
        pkt[pos++] = 0x00; pkt[pos++] = 0x0C;   // PTR
        pkt[pos++] = 0x80; pkt[pos++] = 0x01;   // IN + QU
    }

    udp.beginPacket(MDNS_GROUP, MDNS_PORT);
    udp.write(pkt, pos);
    bool ok = udp.endPacket();
    free(pkt);

    if (ok) {
        Log::i(TAG, "DNS-SD : %d requêtes PTR envoyées → 224.0.0.251:5353", count);
    } else {
        Log::e(TAG, "DNS-SD : envoi échoué");
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Traitement d'un paquet DNS/mDNS reçu
//
// Parcourt les sections Answer + Additional et extrait :
//   PTR (12) : service instance name → associe à un type de service connu
//   SRV (33) : hostname + port de l'instance
//   TXT (16) : métadonnées (modèle, firmware…)
//   A   (1)  : hostname.local → IP (peuple _hostToIp)
// ════════════════════════════════════════════════════════════════════════════

void DnsSdScanner::_processPacket(const uint8_t* buf, int len) {
    if (len < 12) return;

    // On accepte aussi bien les réponses (QR=1) que les announcements spontanés
    uint16_t flags = ((uint16_t)buf[2] << 8) | buf[3];
    if (!(flags & 0x8000)) return;   // Ignorer les requêtes

    uint16_t qdCount = ((uint16_t)buf[4]  << 8) | buf[5];
    uint16_t anCount = ((uint16_t)buf[6]  << 8) | buf[7];
    uint16_t nsCount = ((uint16_t)buf[8]  << 8) | buf[9];
    uint16_t arCount = ((uint16_t)buf[10] << 8) | buf[11];

    int pos = 12;

    // Sauter la section Questions
    for (uint16_t q = 0; q < qdCount && pos < len; q++) {
        int next;
        _decodeName(buf, len, pos, next);
        if (next < 0 || next + 4 > len) return;
        pos = next + 4;
    }

    // Parcourir Answer + Authority + Additional
    int totalRR = (int)anCount + (int)nsCount + (int)arCount;
    for (int r = 0; r < totalRR && pos < len; r++) {
        int nameEnd;
        String rrName = _decodeName(buf, len, pos, nameEnd);
        if (nameEnd < 0 || nameEnd + 10 > len) return;

        uint16_t rrType  = ((uint16_t)buf[nameEnd]     << 8) | buf[nameEnd + 1];
        // rrClass at nameEnd+2,+3 (ignored, can be 0x8001 or 0x0001)
        // TTL    at nameEnd+4,5,6,7
        uint16_t rdlen   = ((uint16_t)buf[nameEnd + 8] << 8) | buf[nameEnd + 9];
        int      rdPos   = nameEnd + 10;

        if (rdPos + (int)rdlen > len) return;

        // ── Type A : hostname.local → IP ──────────────────────────────────
        if (rrType == 1 && rdlen == 4) {
            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
                     buf[rdPos], buf[rdPos+1], buf[rdPos+2], buf[rdPos+3]);
            String host = rrName;
            host.toLowerCase();
            if (_hostToIp.find(host) == _hostToIp.end()) {
                _hostToIp[host] = String(ipStr);
                Log::d(TAG, "A: %s → %s", host.c_str(), ipStr);
            }
        }

        // ── Type PTR : _service._tcp.local → instance.service.local ──────
        else if (rrType == 12) {
            int dummy;
            String instanceFull = _decodeName(buf, len, rdPos, dummy);
            if (instanceFull.isEmpty()) { pos = rdPos + rdlen; continue; }

            // Extraire le type de service depuis le nom de l'owner RR
            // rrName = "_googlecast._tcp.local"
            String svcType = rrName;
            svcType.toLowerCase();
            // Retirer ".local" final
            if (svcType.endsWith(".local")) svcType = svcType.substring(0, svcType.length() - 6);

            const ServiceEntry* svc = findService(svcType);
            if (!svc) { pos = rdPos + rdlen; continue; }

            // instanceFull = "Living Room TV._googlecast._tcp.local"
            // instanceName = "Living Room TV" (tout avant le premier "._")
            String instanceName = instanceFull;
            int underDot = instanceFull.indexOf("._");
            if (underDot > 0) instanceName = instanceFull.substring(0, underDot);

            String key = instanceName + "|" + svcType;
            if (_instances.find(key) == _instances.end()) {
                _Instance inst;
                inst.serviceType  = svcType;
                inst.serviceLabel = svc->label;
                inst.instanceName = instanceName;
                inst.categoryHint = svc->category;
                _instances[key]   = inst;
                Log::d(TAG, "PTR: %s → %s (%s)", svcType.c_str(),
                       instanceName.c_str(), svc->label);
            }
        }

        // ── Type SRV : instance → priority + weight + port + hostname ─────
        else if (rrType == 33 && rdlen >= 7) {
            uint16_t port = ((uint16_t)buf[rdPos + 4] << 8) | buf[rdPos + 5];
            int dummy;
            String target = _decodeName(buf, len, rdPos + 6, dummy);
            target.toLowerCase();

            // Associer ce SRV à toutes les instances avec ce nom
            for (auto& kv : _instances) {
                if (rrName.startsWith(kv.second.instanceName)) {
                    if (kv.second.port == 0) kv.second.port = port;
                    if (kv.second.hostname.isEmpty()) kv.second.hostname = target;
                }
            }
        }

        // ── Type TXT : instance → métadonnées key=value ───────────────────
        else if (rrType == 16 && rdlen > 0) {
            for (auto& kv : _instances) {
                if (rrName.startsWith(kv.second.instanceName)) {
                    if (kv.second.model.isEmpty()) {
                        _parseTxt(buf + rdPos, rdlen, kv.second);
                    }
                }
            }
        }

        pos = rdPos + rdlen;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Déduction de catégorie depuis les services détectés
//
// Priorité : services identifiants (AirPlay, Cast, Sonos…) > générique (SSH)
// ════════════════════════════════════════════════════════════════════════════

String DnsSdScanner::_inferCategory(const String& services) {
    // Ordre de priorité : le premier match gagne
    static const struct { const char* svc; const char* cat; } RULES[] = {
        { "HomeKit",  "SmartHome"  },
        { "AirPlay",  "TV"         },
        { "Cast",     "Streaming"  },
        { "Sonos",    "Speaker"    },
        { "Spotify",  "Speaker"    },
        { "Hue",      "SmartHub"   },
        { "ESPHome",  "IoT"        },
        { "HA",       "SmartHome"  },
        { "MQTT",     "IoT"        },
        { "IPP",      "Printer"    },
        { "Print",    "Printer"    },
        { "SMB",      "NAS"        },
        { "AFP",      "NAS"        },
        { "NFS",      "NAS"        },
        { "SFTP",     "NAS"        },
        { "SSH",      "Computer"   },
        { nullptr,    nullptr      }
    };
    for (int i = 0; RULES[i].svc != nullptr; i++) {
        if (services.indexOf(RULES[i].svc) >= 0) return RULES[i].cat;
    }
    return "";
}

// ════════════════════════════════════════════════════════════════════════════
// Construction du résultat final : IP → DnsSdInfo
// ════════════════════════════════════════════════════════════════════════════

std::map<String, DnsSdInfo> DnsSdScanner::_buildResult() const {
    std::map<String, DnsSdInfo> result;

    for (const auto& kv : _instances) {
        const _Instance& inst = kv.second;

        // Résoudre l'IP : depuis le record A capturé ou depuis le hostname
        String ip;
        if (!inst.ip.isEmpty()) {
            ip = inst.ip;
        } else if (!inst.hostname.isEmpty()) {
            auto it = _hostToIp.find(inst.hostname);
            if (it != _hostToIp.end()) ip = it->second;
        }
        // Fallback : chercher le hostname de l'instance dans _hostToIp
        if (ip.isEmpty()) {
            String h = inst.instanceName;
            h.toLowerCase();
            h.replace(' ', '-');
            h += ".local";
            auto it = _hostToIp.find(h);
            if (it != _hostToIp.end()) ip = it->second;
        }

        if (ip.isEmpty()) continue;   // Impossible de résoudre l'IP — on ignore

        DnsSdInfo& info = result[ip];

        // Ajouter le service label s'il n'est pas déjà présent
        if (info.services.isEmpty()) {
            info.services = inst.serviceLabel;
        } else if (info.services.indexOf(inst.serviceLabel) < 0) {
            info.services += "|";
            info.services += inst.serviceLabel;
        }

        // Modèle (premier non vide)
        if (info.model.isEmpty() && !inst.model.isEmpty()) {
            info.model = inst.model;
        }
        // Hostname (premier non vide)
        if (info.hostname.isEmpty() && !inst.hostname.isEmpty()) {
            String h = inst.hostname;
            if (h.endsWith(".local")) h = h.substring(0, h.length() - 6);
            info.hostname = h;
        }
    }

    // Déduire les catégories depuis les services
    for (auto& kv : result) {
        kv.second.category = _inferCategory(kv.second.services);
    }

    return result;
}

// ════════════════════════════════════════════════════════════════════════════
// Point d'entrée public
// ════════════════════════════════════════════════════════════════════════════

std::map<String, DnsSdInfo> DnsSdScanner::scan(uint32_t timeout_ms) {
    _instances.clear();
    _hostToIp.clear();

    Log::i(TAG, "Démarrage scan DNS-SD (timeout=%ums)", timeout_ms);

    WiFiUDP udp;
    // Rejoindre le groupe multicast pour recevoir les réponses mDNS
    if (!udp.beginMulticast(MDNS_GROUP, MDNS_PORT)) {
        Log::e(TAG, "Impossible de rejoindre 224.0.0.251:5353");
        return {};
    }

    _sendQueries(udp);

    // Écoute pendant timeout_ms
    uint32_t deadline = millis() + timeout_ms;
    while (millis() < deadline) {
        int psize = udp.parsePacket();
        if (psize > 0) {
            const int MAXPKT = 1024;
            uint8_t* buf = (uint8_t*)malloc(MAXPKT);
            if (buf) {
                int len = udp.read(buf, MAXPKT);
                if (len > 0) _processPacket(buf, len);
                free(buf);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    udp.stop();

    auto result = _buildResult();
    Log::i(TAG, "DNS-SD terminé — %d instance(s), %d IP(s) résolue(s)",
           (int)_instances.size(), (int)result.size());

    if (Log::level() >= LOG_LEVEL_DEBUG) {
        for (const auto& kv : result) {
            Log::d(TAG, "  %s → [%s] model=%s cat=%s",
                   kv.first.c_str(), kv.second.services.c_str(),
                   kv.second.model.c_str(), kv.second.category.c_str());
        }
    }

    return result;
}
