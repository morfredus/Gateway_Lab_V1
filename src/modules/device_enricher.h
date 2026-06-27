/**
 * DeviceEnricher - Enrichissement par reconnaissance de patterns hostname
 *
 * Complete manufacturer, category et os a partir de mots-cles trouves dans
 * le hostname resolu (mDNS, PTR DNS ou NetBIOS) - aucune requete reseau,
 * detection 100% locale et instantanee.
 *
 * Tres efficace pour les equipements qui annoncent leur nature dans leur
 * nom d'hote (ex: "roborock-vacuum-a97", "tado-bridge-x", "iphone-fred")
 * mais qui n'exposent pas de service SSDP/DNS-SD ou ont un OUI inconnu
 * (MAC aleatoire iOS/Android).
 *
 * Regle : n'ecrase jamais un champ deja renseigne par une source plus fiable
 * (SSDP, API specifique, ISP detector, port scan) - applique uniquement
 * sur les champs encore vides, en derniere etape du scan.
 */

#pragma once
#include <Arduino.h>
#include "network_scanner.h"

struct EnrichPattern {
    const char* keyword;       // sous-chaine recherchee dans le hostname (minuscules)
    const char* manufacturer;  // "" = ne pas renseigner
    const char* category;      // "" = ne pas renseigner
    const char* os;            // "" = ne pas renseigner
};

static const EnrichPattern DEVICE_PATTERNS[] = {
    // Apple - Mobile
    { "iphone",        "Apple",                   "Mobile",        ""              },
    { "ipad",          "Apple",                   "Mobile",        ""              },
    { "ipod",          "Apple",                   "Mobile",        ""              },
    // Apple - Ordinateurs
    { "macbook",       "Apple",                   "Computer",      "macOS"         },
    { "imac",          "Apple",                   "Computer",      "macOS"         },
    { "mac-mini",      "Apple",                   "Computer",      "macOS"         },
    { "macmini",        "Apple",                   "Computer",      "macOS"         },
    { "mac-pro",       "Apple",                   "Computer",      "macOS"         },
    // Apple - Autres
    { "appletv",       "Apple",                   "Streaming",     "tvOS"          },
    { "apple-tv",      "Apple",                   "Streaming",     "tvOS"          },
    { "homepod",       "Apple",                   "Smart Speaker", "HomePod OS"    },
    // Robots aspirateurs
    { "roborock",      "Roborock",                "Robot Vacuum",  ""              },
    { "roomba",        "iRobot",                  "Robot Vacuum",  ""              },
    { "irobot",        "iRobot",                  "Robot Vacuum",  ""              },
    { "braava",        "iRobot",                  "Robot Vacuum",  ""              },
    { "ecovacs",       "ECOVACS",                 "Robot Vacuum",  ""              },
    { "deebot",        "ECOVACS",                 "Robot Vacuum",  ""              },
    // Domotique / smart home
    { "tado",          "Tado",                    "Smart Home",    ""              },
    { "nest",          "Google",                  "Smart Home",    ""              },
    { "hue-bridge",    "Philips",                 "Smart Hub",     ""              },
    { "smartthings",   "Samsung",                 "Smart Hub",     ""              },
    { "homeassistant", "Home Assistant",          "Smart Hub",     "Home Assistant"},
    { "hassio",        "Home Assistant",          "Smart Hub",     "Home Assistant"},
    // Routeurs / reseau
    { "fritzbox",      "AVM",                     "Router",        ""              },
    { "fritz",         "AVM",                     "Router",        ""              },
    { "unifi",         "Ubiquiti",                "Router",        ""              },
    { "ubnt",          "Ubiquiti",                "Router",        ""              },
    { "mikrotik",      "MikroTik",                "Router",        ""              },
    { "openwrt",       "OpenWrt",                 "Router",        "OpenWrt"       },
    { "dd-wrt",        "DD-WRT",                  "Router",        "DD-WRT"        },
    { "pfsense",       "Netgate",                 "Router",        "pfSense"       },
    // IoT / prises connectees
    { "shelly",        "Allterco",                "IoT",           ""              },
    { "tasmota",       "",                        "IoT",           "Tasmota"       },
    { "esphome",       "Espressif",               "IoT",           "ESPHome"       },
    { "sonoff",        "ITEAD",                   "IoT",           ""              },
    { "wemos",         "Wemos",                   "IoT",           ""              },
    { "esp32",         "Espressif",               "IoT",           ""              },
    { "esp8266",       "Espressif",               "IoT",           ""              },
    { "tuya",          "Tuya",                    "IoT",           ""              },
    { "meross",        "Meross",                  "IoT",           ""              },
    // NAS / serveurs
    { "synology",      "Synology",                "NAS",           ""              },
    { "diskstation",   "Synology",                "NAS",           ""              },
    { "qnap",          "QNAP",                     "NAS",           ""              },
    { "truenas",       "iXsystems",               "NAS",           "TrueNAS"       },
    // SBC
    { "raspberry",     "Raspberry Pi Foundation", "SBC",           "Raspberry Pi OS"},
    { "orangepi",      "Xunlong",                 "SBC",           ""              },
    { "bananapi",      "SinoVoip",                "SBC",           ""              },
    // Streaming
    { "chromecast",    "Google",                  "Streaming",     "Cast OS"       },
    { "fire-tv",       "Amazon",                  "Streaming",     "Fire OS"       },
    { "firetv",        "Amazon",                  "Streaming",     "Fire OS"       },
    { "shield",        "NVIDIA",                  "Streaming",     "Android TV"    },
    { "roku",          "Roku",                    "Streaming",     "Roku OS"       },
    // Enceintes
    { "sonos",         "Sonos",                   "Speaker",       ""              },
    { "alexa",         "Amazon",                  "Smart Speaker", ""              },
    { "echo-",         "Amazon",                  "Smart Speaker", ""              },
    // Imprimantes
    { "printer",       "",                        "Printer",       ""              },
    { "epson",         "Epson",                   "Printer",       ""              },
    { "canon",         "Canon",                   "Printer",       ""              },
    { "brother",       "Brother",                 "Printer",       ""              },
    { "lexmark",       "Lexmark",                 "Printer",       ""              },
    // Cameras / securite
    { "camera",        "",                        "Camera",        ""              },
    { "ipcam",         "",                        "Camera",        ""              },
    { "hikvision",     "Hikvision",               "Camera",        ""              },
    { "dahua",         "Dahua",                   "Camera",        ""              },
    { "axis",          "Axis",                    "Camera",        ""              },
    { "ring",          "Ring",                    "Security",      ""              },
    { "arlo",          "Arlo",                    "Security",      ""              },
    { "wyze",          "Wyze",                    "Security",      ""              },
    // Mobile / Android
    { "android",       "",                        "Mobile",        "Android"       },
    { "samsung",       "Samsung",                 "Mobile",        ""              },
    { "xiaomi",        "Xiaomi",                  "Mobile",        ""              },
    { "huawei",        "Huawei",                  "Mobile",        ""              },
    { "oneplus",       "OnePlus",                 "Mobile",        ""              },
    { "pixel",         "Google",                  "Mobile",        "Android"       },
    // Ordinateurs
    { "windows",       "Microsoft",               "Computer",      "Windows"       },
    { "desktop-",      "",                        "Computer",      ""              },
    { "thinkpad",      "Lenovo",                  "Computer",      ""              },
    { "ideapad",       "Lenovo",                  "Computer",      ""              },
    // Consoles de jeu
    { "playstation",   "Sony",                    "Console",       ""              },
    { "xbox",          "Microsoft",               "Console",       ""              },
    { "nintendo",      "Nintendo",                "Console",       ""              },

    { nullptr, nullptr, nullptr, nullptr }   // Sentinelle
};

/**
 * Applique l'enrichissement par patterns hostname sur un NetworkDevice.
 * Ne renseigne que les champs encore vides (manufacturer, category, os).
 * Appelee en derniere etape du scan, apres SSDP/DNS-SD/port scan/NetBIOS.
 */
inline void applyDeviceEnrichment(NetworkDevice& d) {
    if (d.hostname.isEmpty()) return;

    String h = d.hostname;
    h.toLowerCase();

    for (int i = 0; DEVICE_PATTERNS[i].keyword != nullptr; i++) {
        const EnrichPattern& p = DEVICE_PATTERNS[i];
        if (h.indexOf(p.keyword) < 0) continue;

        if (d.manufacturer.isEmpty() && p.manufacturer[0] != '\0')
            d.manufacturer = p.manufacturer;
        if (isGenericCategory(d.category) && p.category[0] != '\0')
            d.category = p.category;
        if (d.os.isEmpty() && p.os[0] != '\0')
            d.os = p.os;

        if (!d.manufacturer.isEmpty() && !d.category.isEmpty() && !d.os.isEmpty())
            break;
    }
}

/**
 * Detection des points d'acces / repeteurs mesh WiFi (ex: TP-Link Deco,
 * Google Nest WiFi, Orbi...) a partir du hostname. Contrairement a
 * applyDeviceEnrichment(), renseigne `type` (absent de EnrichPattern) sans
 * jamais ecraser une categorie deja deduite par l'OUI/SSDP — seul `type` est
 * complete pour distinguer ces equipements des autres "Network Equipment".
 */
inline void applyMeshDetection(NetworkDevice& d) {
    if (d.hostname.isEmpty() || !d.type.isEmpty()) return;

    String h = d.hostname;
    h.toLowerCase();

    static const char* MESH_KEYWORDS[] = { "deco", "orbi", "eero", "nest-wifi", "velop", "tplink-mesh", nullptr };
    for (int i = 0; MESH_KEYWORDS[i] != nullptr; i++) {
        if (h.indexOf(MESH_KEYWORDS[i]) >= 0) {
            d.type = "Point d'accès / Répéteur mesh";
            if (isGenericCategory(d.category)) d.category = "Network";
            break;
        }
    }
}
