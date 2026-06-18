/**
 * DnsSdScanner — Implémentation (réécrite en v0.8.2, plancher de requête
 * corrigé en v0.8.3)
 *
 * Bibliothèque : mdns.h (composant mDNS ESP-IDF, déjà initialisé par
 * ESPmDNS/MDNS.begin() — voir wifi_manager.cpp). Aucun socket dédié : les
 * requêtes passent par le service mDNS déjà actif, ce qui élimine tout
 * risque de conflit de bind sur 224.0.0.251:5353 (voir docs/WARNINGS.md).
 *
 * v0.8.3 : le plancher de fenêtre d'attente par type de service était fixé
 * à 100 ms, trop court face au délai aléatoire de réponse de 20-120 ms
 * imposé par la RFC 6762 §6 sur les enregistrements partagés (cas des PTR
 * de découverte de service) — résultat observé : scan systématiquement
 * vide malgré des services DNS-SD réellement présents sur le réseau (Hue,
 * Echo, Synology…). Voir MIN_QUERY_TIMEOUT_MS ci-dessous.
 */

#include "dns_sd_scanner.h"
#include "../utils/logger.h"
#include <mdns.h>
#include <esp_err.h>

static const char* TAG = "DNSSD";

// Instance globale
DnsSdScanner dnsSdScanner;

// ════════════════════════════════════════════════════════════════════════════
// Table des types de services DNS-SD à interroger
//
// Colonnes : type, label UI, suggestion de catégorie ("" = pas de suggestion,
// non utilisée directement — voir _inferCategory(), basée sur les labels)
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
    { "_pdl-datastream._tcp",   "PDL",      "Printer"   },
    { "_scanner._tcp",          "Scanner",  "Printer"   },
    // Ordinateurs / partage de fichiers
    { "_workstation._tcp",      "Workst.",  "Computer"  },
    { "_companion-link._tcp",   "Companion","Mobile"    },
    // Domotique / IoT (suite)
    { "_privet._tcp",           "Privet",   "IoT"       },
    { "_matter._tcp",           "Matter",   "SmartHome" },
    { "_matterc._udp",          "Matter",   "SmartHome" },
    { "_sleep-proxy._tcp",      "SleepPxy", ""          },
    { nullptr, nullptr, nullptr }
};

// Sépare "_http._tcp" → service="_http", proto="_tcp"
static void _splitServiceType(const String& full, String& service, String& proto) {
    int idx = full.indexOf("._");
    if (idx < 0) { service = full; proto = "_tcp"; return; }
    service = full.substring(0, idx);
    proto   = full.substring(idx + 1);
}

static String _ip4ToString(const esp_ip4_addr_t& ip) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (unsigned)(ip.addr & 0xFF),
             (unsigned)((ip.addr >> 8) & 0xFF),
             (unsigned)((ip.addr >> 16) & 0xFF),
             (unsigned)((ip.addr >> 24) & 0xFF));
    return String(buf);
}

// ════════════════════════════════════════════════════════════════════════════
// Déduction de catégorie depuis les services détectés
//
// Priorité : services identifiants (AirPlay, Cast, Sonos…) > générique (SSH)
// ════════════════════════════════════════════════════════════════════════════

String DnsSdScanner::_inferCategory(const String& services) {
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
// Point d'entrée public
// ════════════════════════════════════════════════════════════════════════════

// Plancher de fenêtre d'attente par type de service.
//
// La RFC 6762 §6 impose aux répondeurs un délai aléatoire de 20 à 120 ms
// avant de répondre à une question portant sur un enregistrement partagé
// (cas des PTR de découverte de service) afin d'éviter une rafale de
// réponses simultanées. Un plancher de 100 ms laissait une marge quasi
// nulle pour l'aller-retour réseau au-delà de ce délai — résultat observé :
// zéro IP résolue malgré la présence d'objets DNS-SD réels sur le réseau
// (Hue, Echo, Synology…). Porter le plancher à 300 ms absorbe ce délai
// aléatoire avec une marge confortable.
static constexpr uint32_t MIN_QUERY_TIMEOUT_MS = 300;

std::map<String, DnsSdInfo> DnsSdScanner::scan(uint32_t timeout_ms) {
    std::map<String, DnsSdInfo> result;

    int typeCount = 0;
    for (int i = 0; SERVICE_TYPES[i].type != nullptr; i++) typeCount++;
    if (typeCount == 0) return result;

    uint32_t perTypeTimeout = timeout_ms / (uint32_t)typeCount;
    if (perTypeTimeout < MIN_QUERY_TIMEOUT_MS) perTypeTimeout = MIN_QUERY_TIMEOUT_MS;

    Log::i(TAG, "Démarrer le scan DNS-SD (%d type(s) de service, %ums chacun, ~%ums au total)",
           typeCount, (unsigned)perTypeTimeout, (unsigned)(perTypeTimeout * (uint32_t)typeCount));

    for (int i = 0; SERVICE_TYPES[i].type != nullptr; i++) {
        String service, proto;
        _splitServiceType(SERVICE_TYPES[i].type, service, proto);

        mdns_result_t* results = nullptr;
        esp_err_t err = mdns_query_ptr(service.c_str(), proto.c_str(),
                                        perTypeTimeout, 16, &results);
        if (err != ESP_OK) {
            Log::w(TAG, "Requête %s.%s échouée : %s",
                   service.c_str(), proto.c_str(), esp_err_to_name(err));
            continue;
        }
        if (results == nullptr) continue;

        for (mdns_result_t* r = results; r != nullptr; r = r->next) {
            String hostname;
            if (r->hostname != nullptr) hostname = String(r->hostname);

            String model;
            for (size_t t = 0; t < r->txt_count; t++) {
                if (r->txt[t].key == nullptr || r->txt[t].value == nullptr) continue;
                String key = String(r->txt[t].key);
                String val = String(r->txt[t].value);
                key.toLowerCase();
                if (val.isEmpty()) continue;
                if (model.isEmpty() &&
                    (key == "md" || key == "model" || key == "fn" || key == "am")) {
                    model = val;
                }
            }

            for (mdns_ip_addr_t* a = r->addr; a != nullptr; a = a->next) {
                if (a->addr.type != ESP_IPADDR_TYPE_V4) continue;
                String ip = _ip4ToString(a->addr.u_addr.ip4);
                if (ip.isEmpty()) continue;

                DnsSdInfo& info = result[ip];
                if (info.services.isEmpty()) {
                    info.services = SERVICE_TYPES[i].label;
                } else if (info.services.indexOf(SERVICE_TYPES[i].label) < 0) {
                    info.services += "|";
                    info.services += SERVICE_TYPES[i].label;
                }
                if (info.model.isEmpty() && !model.isEmpty()) {
                    info.model = model;
                }
                if (info.hostname.isEmpty() && !hostname.isEmpty()) {
                    String h = hostname;
                    if (h.endsWith(".local")) h = h.substring(0, h.length() - 6);
                    info.hostname = h;
                }
            }
        }

        mdns_query_results_free(results);
    }

    for (auto& kv : result) {
        kv.second.category = _inferCategory(kv.second.services);
    }

    Log::i(TAG, "DNS-SD terminé — %d IP(s) résolue(s)", (int)result.size());

#if LOG_LEVEL >= 4
    for (const auto& kv : result) {
        Log::d(TAG, "  %s → [%s] model=%s cat=%s",
               kv.first.c_str(), kv.second.services.c_str(),
               kv.second.model.c_str(), kv.second.category.c_str());
    }
#endif

    return result;
}
