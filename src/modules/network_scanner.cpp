/**
 * NetworkScanner — Implémentation v0.0.7
 *
 * Bibliothèques utilisées :
 *   lwip/etharp.h     — etharp_get_entry(), etharp_request()
 *   lwip/netif.h      — netif_default (interface réseau active)
 *   lwip/inet.h       — ip4addr_ntoa_r()
 *   FreeRTOS          — tâche asynchrone Core 0, mutex
 *   ArduinoJson       — sérialisation JSON sécurisée (champs échappés)
 *   HostnameResolver  — résolution mDNS + PTR DNS
 *   IspDetector       — détection des boxes FAI françaises
 */

#include "network_scanner.h"
#include "hostname_resolver.h"   // mDNS passif + PTR DNS batch
#include "isp_detector.h"        // Détection Free / Orange / SFR / Bouygues
#include "ssdp_scanner.h"        // Découverte UPnP/SSDP
#include "dns_sd_scanner.h"      // Découverte de services DNS-SD (RFC 6763)
#include "device_store.h"        // Persistance LittleFS
#include "icmp_scanner.h"        // Sonde ICMP pour IPs non trouvées par ARP
#include "port_scanner.h"        // Scan TCP ports communs + banner HTTP/SSH/FTP + API IoT
#include "netbios_scanner.h"     // Node Status NetBIOS (UDP 137) - hostnames Windows/Samba
#include "device_enricher.h"     // Enrichissement par pattern matching sur le hostname
#include "device_history.h"      // Journal chronologique des evenements (nouveaux/changements)
#include "time_sync.h"           // Epoch NTP pour firstSeen/lastSeen
#include <WiFi.h>
#include <ArduinoJson.h>
#include "../../include/app_config.h"   // MDNS_HOSTNAME, PROJECT_NAME
#include "lwip/etharp.h"   // etharp_get_entry(), etharp_request()
#include "lwip/netif.h"    // netif_default
#include "lwip/inet.h"     // ip4addr_ntoa_r()
#include "../../include/oui_table.h"  // OUI_TABLE[] généré depuis data/oui.json
#include "../utils/logger.h"

static const char* TAG = "Scanner";

// Instance globale exportée
NetworkScanner netScanner;

// Détecte une adresse MAC privée/aléatoire (randomized MAC), utilisée par les
// smartphones récents (iOS 14+, Android 10+) pour se connecter au Wi-Fi sans
// exposer leur OUI matériel réel. La norme IEEE impose que le bit "locally
// administered" (2ème bit du 1er octet) soit à 1 sur ces adresses générées,
// ce qui se traduit par un 2ème nibble de l'octet valant 2, 6, A ou E.
static bool isRandomizedMac(const String& mac) {
    if (mac.length() < 2) return false;
    char nibble = toupper(mac[1]);
    return nibble == '2' || nibble == '6' || nibble == 'A' || nibble == 'E';
}

// Recherche de l'entrée OUI à partir des 3 premiers octets de l'adresse MAC.
// Retourne nullptr si l'OUI n'est pas dans la table ou si la MAC est aléatoire
// (dans ce cas, l'OUI ne correspond à aucun fabricant réel).
static const OuiEntry* lookupOui(const String& mac) {
    if (mac.length() < 8) return nullptr;
    if (isRandomizedMac(mac)) return nullptr;
    String prefix = mac.substring(0, 8);
    prefix.toUpperCase();
    for (const auto& e : OUI_TABLE) {
        if (prefix == e.oui) return &e;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Lecture de la table ARP lwIP
//
// La table ARP associe chaque adresse IP à une adresse MAC. Elle est peuplée
// automatiquement par lwIP quand l'ESP32 communique avec un équipement ou
// reçoit une réponse ARP. Taille max : ARP_TABLE_SIZE (10 sur ESP32).
// On lit la table fréquemment pour éviter de perdre des entrées entre deux lots.
// ---------------------------------------------------------------------------
void NetworkScanner::_readArpTable() {
    ip4_addr_t*      ip_ptr;
    struct netif*    netif_ptr;
    struct eth_addr* eth_ptr;

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!etharp_get_entry(i, &ip_ptr, &netif_ptr, &eth_ptr)) continue;

        char ipStr[16];
        ip4addr_ntoa_r(ip_ptr, ipStr, sizeof(ipStr));

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
            eth_ptr->addr[0], eth_ptr->addr[1], eth_ptr->addr[2],
            eth_ptr->addr[3], eth_ptr->addr[4], eth_ptr->addr[5]);

        // Déduplication par MAC — mise à jour si le MAC existe déjà
        bool found = false;
        for (auto& d : _results) {
            if (d.mac == macStr) {
                d.ip       = ipStr;
                d.lastSeen = millis();
                d.online   = true;
                found = true;
                break;
            }
        }

        if (!found) {
            NetworkDevice h;
            h.ip       = ipStr;
            h.mac      = macStr;
            h.lastSeen = millis();
            h.online   = true;
            if (isRandomizedMac(macStr)) {
                h.manufacturer = "Unknown (Privacy Mode)";
                h.category     = "Mobile/Aléatoire";
            } else {
                const OuiEntry* oui = lookupOui(macStr);
                if (oui) {
                    h.manufacturer = oui->manufacturer;
                    h.category     = oui->category;   // "Router", "IoT", "SBC"…
                }
            }
            _results.push_back(h);
        }
    }
}

// ---------------------------------------------------------------------------
// Sweep ARP du sous-réseau — 3 passes
//
// Passe 1 : sweep complet par lots de 5, 100 ms entre chaque lot.
//   Capture les équipements actifs qui répondent rapidement.
//
// Passe 2 : re-sonde uniquement les IPs non encore trouvées, lots de 5,
//   150 ms entre lots. Rattrape les équipements lents à répondre.
//
// Passe 3 : attente 500 ms puis dernière lecture ARP.
//   Vide le buffer des réponses tardives avant de passer à la résolution.
// ---------------------------------------------------------------------------
void NetworkScanner::_sweepSubnet() {
    IPAddress local = WiFi.localIP();
    IPAddress mask  = WiFi.subnetMask();

    uint8_t net[4], bcast[4];
    for (int i = 0; i < 4; i++) {
        net[i]   = local[i] & mask[i];
        bcast[i] = net[i] | (~mask[i] & 0xFF);
    }

    const int h_start = (net[3]   == 0)   ? 1   : (int)net[3]   + 1;
    const int h_end   = (bcast[3] == 255) ? 254 : (int)bcast[3] - 1;

    // ── Passe 1 : sweep complet ──────────────────────────────────────────────
    constexpr int BATCH_SIZE     = 5;
    constexpr int BATCH_DELAY_MS = 100;

    Log::i(TAG, "ARP passe 1 : %d.%d.%d.%d–.%d (lots %d, %d ms/lot)",
           net[0], net[1], net[2], h_start, h_end, BATCH_SIZE, BATCH_DELAY_MS);

    int inBatch = 0;
    for (int h = h_start; h <= h_end; h++) {
        IPAddress target(net[0], net[1], net[2], h);
        if (target == local) continue;

        ip4_addr_t ip4target;
        IP4_ADDR(&ip4target, net[0], net[1], net[2], (uint8_t)h);
        etharp_request(netif_default, &ip4target);
        inBatch++;

        if (inBatch >= BATCH_SIZE) {
            vTaskDelay(pdMS_TO_TICKS(BATCH_DELAY_MS));
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _readArpTable();
            xSemaphoreGive(_mutex);
            hostnameResolver.update();
            inBatch = 0;
        }
    }
    if (inBatch > 0) {
        vTaskDelay(pdMS_TO_TICKS(BATCH_DELAY_MS));
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _readArpTable();
        xSemaphoreGive(_mutex);
        hostnameResolver.update();
    }

    // ── Passe 2 : re-sonde les IP non trouvées ───────────────────────────────
    // Collecte les IPs de la plage absentes de _results
    std::vector<int> missing;
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int h = h_start; h <= h_end; h++) {
            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
                     net[0], net[1], net[2], h);
            IPAddress tgt(net[0], net[1], net[2], h);
            if (tgt == local) continue;
            bool found = false;
            for (const auto& d : _results) {
                if (d.ip == ipStr) { found = true; break; }
            }
            if (!found) missing.push_back(h);
        }
        xSemaphoreGive(_mutex);
    }

    Log::i(TAG, "ARP passe 2 : %u IP(s) non trouvées re-sondées", (unsigned)missing.size());
    inBatch = 0;
    for (int h : missing) {
        ip4_addr_t ip4target;
        IP4_ADDR(&ip4target, net[0], net[1], net[2], (uint8_t)h);
        etharp_request(netif_default, &ip4target);
        inBatch++;

        if (inBatch >= BATCH_SIZE) {
            vTaskDelay(pdMS_TO_TICKS(150));
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _readArpTable();
            xSemaphoreGive(_mutex);
            hostnameResolver.update();
            inBatch = 0;
        }
    }
    if (inBatch > 0) {
        vTaskDelay(pdMS_TO_TICKS(150));
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _readArpTable();
        xSemaphoreGive(_mutex);
        hostnameResolver.update();
    }

    // ── Passe 3 : flush final après délai ────────────────────────────────────
    Log::i(TAG, "ARP passe 3 : lecture finale après 500 ms");
    vTaskDelay(pdMS_TO_TICKS(500));
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _readArpTable();
    xSemaphoreGive(_mutex);
    hostnameResolver.update();
}

// ---------------------------------------------------------------------------
// Résolution des noms d'hôtes + détection ISP
//
// 1. Collecte les IPs online sans hostname (candidats à la résolution)
// 2. Lance les requêtes PTR DNS batch (≤ 500 ms pour toutes les IPs)
// 3. Pour chaque équipement : résolution mDNS > PTR, puis ISP detection
// 4. Réécrit les champs enrichis dans _results sous protection du mutex
// ---------------------------------------------------------------------------
void NetworkScanner::_resolveHostnames() {
    // 1. Copie des IPs à résoudre (sans mutex prolongé)
    std::vector<String> ipsToResolve;
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (const auto& d : _results) {
            if (d.online) ipsToResolve.push_back(d.ip);
        }
        xSemaphoreGive(_mutex);
    }

    if (ipsToResolve.empty()) return;

    // 2. Batch PTR DNS (toutes les requêtes envoyées simultanément,
    //    réponses collectées dans une fenêtre unique de 500 ms)
    hostnameResolver.batchPtrDns(ipsToResolve);

    // 3. Enrichissement de chaque équipement
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        if (!d.online) continue;

        // Résolution hostname (mDNS cache en priorité, PTR DNS en fallback)
        HostnameSource src = HostnameSource::None;
        String name = hostnameResolver.resolve(d.ip, src);

        if (!name.isEmpty()) {
            d.hostname = name;
            d.source   = hostnameSourceStr(src);
        } else if (!d.manufacturer.isEmpty()) {
            d.source = hostnameSourceStr(HostnameSource::MAC);
        }

        // Détection ISP (Free / Orange / SFR / Bouygues)
        // Utilise le hostname fraîchement résolu + manufacturer OUI
        applyIspDetection(d);
    }
    xSemaphoreGive(_mutex);
}

// ---------------------------------------------------------------------------
// Entrée propre de l'ESP32 dans la liste des équipements
//
// Le protocole ARP ne peut pas découvrir sa propre adresse IP : on ne
// reçoit jamais de réponse ARP pour soi-même. On injecte donc manuellement
// une entrée représentant cet appareil, identifiable par son hostname mDNS
// et son adresse MAC. La source "Self" distingue cette entrée dans l'UI.
// ---------------------------------------------------------------------------
void NetworkScanner::_addSelfEntry() {
    String ip  = WiFi.localIP().toString();
    String mac = WiFi.macAddress();
    mac.toUpperCase();   // WiFi.macAddress() peut retourner des minuscules

    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Déduplication : si l'ESP32 est déjà dans la liste (scan précédent), mise à jour
    for (auto& d : _results) {
        if (d.mac == mac) {
            d.ip       = ip;
            d.lastSeen = millis();
            d.online   = true;
            xSemaphoreGive(_mutex);
            return;
        }
    }

    NetworkDevice self;
    self.ip       = ip;
    self.mac      = mac;
    self.hostname = MDNS_HOSTNAME;       // ex: "gateway-lab-v1"
    self.model    = PROJECT_NAME;        // ex: "GatewayLabV1"
    self.source   = "Self";
    self.category = "Gateway";
    self.lastSeen = millis();
    self.online   = true;

    // Fabricant depuis l'OUI (Espressif Systems pour les ESP32) — la MAC de
    // l'ESP32 lui-même n'est jamais aléatoire, mais on reste cohérent avec
    // lookupOui() qui ignore déjà ce cas.
    const OuiEntry* oui = lookupOui(mac);
    if (oui) self.manufacturer = oui->manufacturer;

    _results.push_back(self);
    xSemaphoreGive(_mutex);
    Log::i(TAG, "Auto-entrée : %s (%s) → %s", ip.c_str(), mac.c_str(), MDNS_HOSTNAME);
}

// ---------------------------------------------------------------------------
// Fusion des résultats SSDP dans _results
//
// Pour chaque équipement UPnP découvert :
//   - S'il est déjà connu (même IP), on enrichit ses champs vides :
//       manufacturer, model, category, os, source, hostname
//   - S'il n'est pas encore dans la liste, on l'ajoute (équipement UPnP
//     sans réponse ARP — cas rare mais possible sur certains réseaux)
// ---------------------------------------------------------------------------
void NetworkScanner::_mergeSsdp() {
    auto ssdpResults = ssdpScanner.scan(4000);
    if (ssdpResults.empty()) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (const auto& sdev : ssdpResults) {
        bool found = false;
        for (auto& d : _results) {
            if (d.ip != sdev.ip) continue;
            found = true;
            // Enrichissement des champs vides uniquement (ne pas écraser
            // les données déjà résolues par ARP/mDNS/PTR/ISP)
            if (d.manufacturer.isEmpty() && !sdev.manufacturer.isEmpty())
                d.manufacturer = sdev.manufacturer;
            if (d.model.isEmpty() && !sdev.model.isEmpty())
                d.model = sdev.model;
            if (d.category.isEmpty() || d.category == "IoT")
                if (!sdev.category.isEmpty()) d.category = sdev.category;
            if (d.os.isEmpty() && !sdev.os.isEmpty())
                d.os = sdev.os;
            if (d.hostname.isEmpty() && !sdev.hostname.isEmpty())
                d.hostname = sdev.hostname;
            // La source SSDP est ajoutée en suffixe si une autre source existe déjà
            if (d.source.isEmpty()) {
                d.source = sdev.source;
            } else if (d.source.indexOf("SSDP") < 0 &&
                       d.source.indexOf("API")  < 0) {
                d.source = sdev.source;   // Remplace MAC/PTR par source enrichie
            }
            break;
        }
        if (!found && !sdev.ip.isEmpty()) {
            // Équipement UPnP inconnu de l'ARP — on l'ajoute directement
            _results.push_back(sdev);
            Log::i(TAG, "Nouvel équipement SSDP : %s (%s)",
                   sdev.ip.c_str(), sdev.manufacturer.c_str());
        }
    }
    xSemaphoreGive(_mutex);
    Log::i(TAG, "SSDP fusionné — %u équipement(s) total", (unsigned)_results.size());
}

// ---------------------------------------------------------------------------
// Fusion des résultats DNS-SD dans _results
//
// Le scan DNS-SD retourne un map IP → DnsSdInfo.
// Pour chaque entrée :
//   - services  : toujours injecté (labels pipe-séparés, ex: "HTTP|SSH|SMB")
//   - model     : injecté si vide
//   - hostname  : injecté si vide
//   - category  : injecté si vide ou "IoT" (catégorie par défaut trop générique)
// ---------------------------------------------------------------------------
void NetworkScanner::_mergeDnsSd() {
    auto dnssdResults = dnsSdScanner.scan(4000);
    if (dnssdResults.empty()) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (const auto& kv : dnssdResults) {
        const String& ip      = kv.first;
        const DnsSdInfo& info = kv.second;

        bool found = false;
        for (auto& d : _results) {
            if (d.ip != ip) continue;
            found = true;

            // Services : on accumule (DNS-SD peut découvrir des services complémentaires)
            if (d.services.isEmpty()) {
                d.services = info.services;
            } else {
                // Ajouter uniquement les services absents
                String remaining = info.services;
                int idx;
                while ((idx = remaining.indexOf('|')) >= 0) {
                    String svc = remaining.substring(0, idx);
                    remaining  = remaining.substring(idx + 1);
                    if (!svc.isEmpty() && d.services.indexOf(svc) < 0) {
                        d.services += "|"; d.services += svc;
                    }
                }
                if (!remaining.isEmpty() && d.services.indexOf(remaining) < 0) {
                    d.services += "|"; d.services += remaining;
                }
            }

            // Modèle depuis DNS-SD TXT records (souvent plus précis que SSDP)
            if (d.model.isEmpty() && !info.model.isEmpty())
                d.model = info.model;

            // Hostname depuis SRV record (nom canonique, souvent plus propre)
            if (d.hostname.isEmpty() && !info.hostname.isEmpty())
                d.hostname = info.hostname;

            // Catégorie DNS-SD (plus fine que "IoT" par défaut)
            if ((d.category.isEmpty() || d.category == "IoT") && !info.category.isEmpty())
                d.category = info.category;

            break;
        }

        if (!found) {
            Log::d(TAG, "DNS-SD : IP %s non trouvée dans les résultats ARP", ip.c_str());
        }
    }
    xSemaphoreGive(_mutex);

    Log::i(TAG, "DNS-SD fusionné — %u résultat(s)", (unsigned)dnssdResults.size());
}

// ---------------------------------------------------------------------------
// Tâche FreeRTOS — exécutée sur Core 0 (même core que lwIP / TCP stack)
// Stack 20 Ko : lwIP + DNS + std::map + HTTP client SSDP + XML parsing
// ---------------------------------------------------------------------------
void NetworkScanner::_task(void* self) {
    static_cast<NetworkScanner*>(self)->_run();
    vTaskDelete(nullptr);
}

void NetworkScanner::_run() {
    // Charger les devices connus depuis LittleFS (injectés offline)
    _mergePersistedDevices();

    // Etat de reference avant ce scan - utilise par _updateHistory() en fin
    // de scan pour detecter les nouveautes et les changements de champs
    std::vector<NetworkDevice> previousState;
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        previousState = _results;
        xSemaphoreGive(_mutex);
    }

    // Démarrer l'écoute mDNS passive avant le sweep (capture les annonces
    // émises par les équipements qui deviennent actifs sur le réseau)
    hostnameResolver.clearCaches();
    hostnameResolver.begin();

    // ARP sweep en 3 passes (implémenté dans _sweepSubnet)
    _sweepSubnet();

    hostnameResolver.update();   // Traiter les derniers paquets mDNS

    Log::i(TAG, "ARP terminé — %u équipement(s) détecté(s)", (unsigned)_results.size());

    // ── ICMP sweep sur les IP non trouvées par ARP ──────────────────────────
    // Collecte les IPs de la plage encore absentes de _results
    {
        IPAddress local = WiFi.localIP();
        IPAddress mask  = WiFi.subnetMask();
        uint8_t net[4], bcast[4];
        for (int i = 0; i < 4; i++) {
            net[i]   = local[i] & mask[i];
            bcast[i] = net[i] | (~mask[i] & 0xFF);
        }
        int h_start = (net[3] == 0)   ? 1   : (int)net[3]   + 1;
        int h_end   = (bcast[3] == 255) ? 254 : (int)bcast[3] - 1;

        std::vector<String> missingIps;
        {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            for (int h = h_start; h <= h_end; h++) {
                char ipStr[16];
                snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
                         net[0], net[1], net[2], h);
                IPAddress tgt(net[0], net[1], net[2], h);
                if (tgt == local) continue;
                bool found = false;
                for (const auto& d : _results) {
                    if (d.ip == ipStr && d.online) { found = true; break; }
                }
                if (!found) missingIps.push_back(String(ipStr));
            }
            xSemaphoreGive(_mutex);
        }

        // ICMP uniquement si la plage est raisonnable (évite les /16)
        if (!missingIps.empty() && missingIps.size() <= 100) {
            // pingWithTtl retourne IP + TTL pour déduire l'OS
            auto aliveWithTtl = icmpScanner.pingWithTtl(missingIps, 200);
            if (!aliveWithTtl.empty()) {
                xSemaphoreTake(_mutex, portMAX_DELAY);
                for (const auto& pr : aliveWithTtl) {
                    bool exists = false;
                    for (const auto& d : _results) {
                        if (d.ip == pr.ip) { exists = true; break; }
                    }
                    if (!exists) {
                        NetworkDevice d;
                        d.ip       = pr.ip;
                        d.source   = "ICMP";
                        d.online   = true;
                        d.lastSeen = millis();
                        if (pr.ttl > 0) d.os = _osFromTtl(pr.ttl);
                        _results.push_back(d);
                        Log::i(TAG, "ICMP nouveau device : %s (TTL=%u)", pr.ip.c_str(), pr.ttl);
                    }
                }
                xSemaphoreGive(_mutex);
            }
        }
    }

    // Résolution des hostnames + détection ISP
    _resolveHostnames();

    // Arrêter l'écoute mDNS (libère le socket UDP multicast)
    hostnameResolver.end();

    // Ajouter l'ESP32 lui-même (non détectable par ARP)
    _addSelfEntry();

    // Scan SSDP/UPnP — enrichit les équipements existants et ajoute les
    // équipements UPnP non détectés par ARP
    _mergeSsdp();

    // Scan DNS-SD — identifie les services exposés par chaque équipement
    _mergeDnsSd();

    // Scan TCP des ports communs + banner HTTP/SSH/FTP + API IoT
    _scanPorts();

    // Requete NetBIOS sur les equipements encore sans hostname (PC Windows/Samba)
    _scanNetBios();

    // Enrichissement final par pattern matching sur le hostname
    _enrichDevices();

    // Classification intelligente : affine la categorie en combinant tous
    // les signaux disponibles (manufacturer, services, ports, hostname)
    _classifyDevices();

    // Historique : firstSeen/lastSeen/seenCount + journal des changements
    _updateHistory(previousState);

    // Sauvegarder en LittleFS pour le prochain boot
    _saveToStore();

    Log::i(TAG, "Scan complet — %u équipement(s) total", (unsigned)_results.size());
    _scanning   = false;
    _taskHandle = nullptr;
}

// ---------------------------------------------------------------------------
// Déduction de l'OS depuis la valeur TTL ICMP
//
// Les systèmes d'exploitation utilisent des valeurs TTL initiales standards.
// En réseau local (1 saut), la valeur reçue est quasiment égale à l'initiale.
// Seuils conservateurs pour absorber quelques sauts supplémentaires.
// ---------------------------------------------------------------------------
String NetworkScanner::_osFromTtl(uint8_t ttl) {
    if (ttl > 200) return "Cisco / Réseau";
    if (ttl > 100) return "Windows";
    if (ttl > 50)  return "Linux / Android / iOS";
    return "";
}

// ---------------------------------------------------------------------------
// Niveau de confiance de l'identification - explique a l'utilisateur d'ou
// vient la deduction manufacturer/category affichee dans l'UI.
//
// Ponderation par fiabilite decroissante de la source ayant produit
// l'identification (la plus forte deja appliquee gagne) :
//   Box FAI (DHCP local)         : 100 - hostname confirme par le routeur lui-meme
//   API specifique (Hue/DSM/...) : 95  - reponse authentifiee de l'equipement
//   mDNS                         : 90  - annonce .local active de l'equipement
//   SSDP/UPnP                    : 80  - descripteur XML fourni par l'equipement
//   PTR DNS                      : 70  - resolution DNS inverse via le routeur
//   NetBIOS                      : 65  - Node Status (UDP 137)
//   OUI (adresse MAC)            : 60  - fabricant deduit, pas le modele exact
//   Heuristiques                 : 40  - pattern matching hostname/ports/services
//   Aucun signal                 : 20  - aucune source fiable n'a contribue
// ---------------------------------------------------------------------------
int NetworkScanner::_confidenceFor(const NetworkDevice& d, String& label) {
    if (d.source == "Self") {
        label = "Equipement local (ESP32)";
        return 100;
    }
    bool ispBox = (d.category == "Router") &&
                  (d.manufacturer == "Free" || d.manufacturer == "Orange" ||
                   d.manufacturer == "SFR"  || d.manufacturer == "Bouygues Telecom");
    if (ispBox) {
        label = "Box FAI (DHCP)";
        return 100;
    }
    if (d.source == "HueAPI" || d.source == "SynologyAPI" || d.source == "FreeboxAPI") {
        label = "API " + d.source;
        return 95;
    }
    if (d.source == "mDNS") {
        label = "mDNS";
        return 90;
    }
    if (d.source.indexOf("SSDP") >= 0) {
        label = "SSDP";
        return 80;
    }
    if (d.source == "PTR") {
        label = "PTR DNS";
        return 70;
    }
    if (d.source == "NetBIOS") {
        label = "NetBIOS";
        return 65;
    }
    if (!d.manufacturer.isEmpty()) {
        label = "OUI";
        return 60;
    }
    if (!d.category.isEmpty()) {
        label = "Heuristiques";
        return 40;
    }
    label = "Aucun signal";
    return 20;
}

// ---------------------------------------------------------------------------
// Scan TCP des ports communs sur tous les équipements en ligne
//
// Pour chaque équipement online :
//   1. Sonde 14 ports communs par lots de 8 (contrainte lwIP max sockets)
//   2. Stocke les ports ouverts dans d.openPorts (pipe-séparés)
//   3. Déduit l'OS depuis d.os si encore vide (TTL ICMP non disponible)
//   4. Enrichit d.manufacturer depuis le banner HTTP si vide
//
// Port → service courants :
//   22 SSH · 80 HTTP · 443 HTTPS · 445 SMB · 554 RTSP · 1883 MQTT
//   3389 RDP · 8080 HTTP-Alt · 8123 HA · 9100 IPP
// ---------------------------------------------------------------------------
// Noms courts des ports pour la liste pipe-séparée
static String _portName(uint16_t p) {
    switch (p) {
        case 21:   return "FTP";
        case 22:   return "SSH";
        case 23:   return "Telnet";
        case 80:   return "HTTP";
        case 443:  return "HTTPS";
        case 445:  return "SMB";
        case 554:  return "RTSP";
        case 1883: return "MQTT";
        case 3389: return "RDP";
        case 5000: return "HTTP/5000";
        case 8080: return "HTTP/8080";
        case 8123: return "HA";
        case 8443: return "HTTPS/8443";
        case 9100: return "IPP";
        default:   return String(p);
    }
}

// Heuristiques banner HTTP → fabricant
static String _bannerToMfr(const String& banner) {
    String b = banner;
    b.toLowerCase();
    if (b.indexOf("synology") >= 0)  return "Synology";
    if (b.indexOf("qnap")     >= 0)  return "QNAP";
    if (b.indexOf("freebox")  >= 0)  return "Freebox";
    if (b.indexOf("philips")  >= 0)  return "Philips";
    if (b.indexOf("ubiquiti") >= 0)  return "Ubiquiti";
    if (b.indexOf("openwrt")  >= 0)  return "OpenWrt Router";
    if (b.indexOf("dd-wrt")   >= 0)  return "DD-WRT Router";
    if (b.indexOf("hikvision")>= 0)  return "Hikvision";
    if (b.indexOf("dahua")    >= 0)  return "Dahua";
    if (b.indexOf("axis")     >= 0)  return "Axis";
    if (b.indexOf("cisco")    >= 0)  return "Cisco";
    if (b.indexOf("mikrotik") >= 0)  return "MikroTik";
    if (b.indexOf("goahead")  >= 0)  return "IP Camera";   // GoAhead = firmware caméra IP
    if (b.indexOf("homeassistant") >= 0) return "Home Assistant";
    return "";
}

// Heuristiques banner HTTP → catégorie
static String _bannerToCategory(const String& banner) {
    String b = banner;
    b.toLowerCase();
    if (b.indexOf("camera") >= 0 || b.indexOf("hikvision") >= 0 ||
        b.indexOf("dahua")  >= 0 || b.indexOf("axis") >= 0 ||
        b.indexOf("goahead")>= 0) return "Camera";
    if (b.indexOf("synology") >= 0 || b.indexOf("qnap") >= 0)
        return "NAS";
    if (b.indexOf("homeassistant") >= 0) return "Smart Hub";
    return "";
}

// ---------------------------------------------------------------------------
// Fusionne le resultat d'un scan de ports dans un equipement donne.
// Extrait de _scanPorts() pour etre reutilisable par rescanDevice()
// (rafraichissement cible d'un seul equipement, sans mutex - appele sous
// protection du mutex par l'appelant).
// ---------------------------------------------------------------------------
void NetworkScanner::_applyPortScanResult(NetworkDevice& d, const PortScanResult& pr) {
    // Construire la liste pipe-séparée des ports ouverts
    if (!pr.openPorts.empty()) {
        String portList;
        for (size_t i = 0; i < pr.openPorts.size(); i++) {
            if (i) portList += "|";
            portList += _portName(pr.openPorts[i]);
        }
        d.openPorts = portList;
    }

    // Enrichir depuis le banner HTTP
    if (!pr.httpBanner.isEmpty()) {
        if (d.manufacturer.isEmpty()) {
            String mfr = _bannerToMfr(pr.httpBanner);
            if (!mfr.isEmpty()) d.manufacturer = mfr;
        }
        if (d.category.isEmpty() || d.category == "IoT") {
            String cat = _bannerToCategory(pr.httpBanner);
            if (!cat.isEmpty()) d.category = cat;
        }
        // Stocker le banner dans model si vide (info utile)
        if (d.model.isEmpty() && pr.httpBanner.length() < 40) {
            d.model = pr.httpBanner;
        }
    }

    // OS depuis TTL (si pas déjà rempli par SSDP/API)
    if (d.os.isEmpty() && !pr.openPorts.empty()) {
        // Port 3389 RDP → Windows certain
        for (uint16_t p : pr.openPorts) {
            if (p == 3389) { d.os = "Windows"; break; }
            if (p == 554)  { d.os = "Camera/NVR"; break; }
        }
    }

    // Banniere SSH -> OS probable (Linux/embarque) si encore vide
    if (d.os.isEmpty() && !pr.sshBanner.isEmpty()) {
        String sb = pr.sshBanner;
        sb.toLowerCase();
        if (sb.indexOf("openssh") >= 0) d.os = "Linux / Unix";
    }

    // API IoT identifiee precisement -> manufacturer/category/model/firmware
    if (!pr.iotType.isEmpty()) {
        if (d.manufacturer.isEmpty()) d.manufacturer = pr.iotType;
        if (d.category.isEmpty() || d.category == "IoT") {
            if (pr.iotType == "FritzBox") d.category = "Router";
            else d.category = "IoT";
        }
        if (d.model.isEmpty() && !pr.iotModel.isEmpty()) d.model = pr.iotModel;
        if (d.os.isEmpty() && !pr.iotFirmware.isEmpty()) d.os = pr.iotFirmware;
    }
}

void NetworkScanner::_scanPorts() {
    // Collecter les IPs online (excl. soi-même)
    std::vector<String> ipsToScan;
    String selfIp = WiFi.localIP().toString();
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (const auto& d : _results) {
            if (d.online && d.ip != selfIp)
                ipsToScan.push_back(d.ip);
        }
        xSemaphoreGive(_mutex);
    }

    if (ipsToScan.empty()) return;

    auto scanResults = portScanner.scan(ipsToScan, 250);
    if (scanResults.empty()) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        auto it = scanResults.find(d.ip);
        if (it == scanResults.end()) continue;
        _applyPortScanResult(d, it->second);
    }
    xSemaphoreGive(_mutex);

    Log::i(TAG, "Ports fusionnés — %u équipement(s) enrichis", (unsigned)scanResults.size());
}

// ---------------------------------------------------------------------------
// Requete NetBIOS Node Status sur les equipements encore sans hostname
//
// Cible uniquement les IP online dont le hostname est vide - tres efficace
// pour les PC Windows et serveurs Samba qui ne repondent pas a mDNS/PTR DNS.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Fusionne le resultat d'une requete NetBIOS dans un equipement donne.
// Extrait de _scanNetBios() pour etre reutilisable par rescanDevice().
// ---------------------------------------------------------------------------
void NetworkScanner::_applyNetBiosResult(NetworkDevice& d, const NetBiosInfo& info) {
    if (!d.hostname.isEmpty()) return;
    d.hostname = info.hostname;
    d.source   = "NetBIOS";
    if (d.model.isEmpty() && !info.workgroup.isEmpty())
        d.model = info.workgroup;
}

void NetworkScanner::_scanNetBios() {
    std::vector<String> ipsToScan;
    String selfIp = WiFi.localIP().toString();
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (const auto& d : _results) {
            if (d.online && d.ip != selfIp && d.hostname.isEmpty())
                ipsToScan.push_back(d.ip);
        }
        xSemaphoreGive(_mutex);
    }

    if (ipsToScan.empty()) return;

    auto nbResults = netBiosScanner.scan(ipsToScan, 250);
    if (nbResults.empty()) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        auto it = nbResults.find(d.ip);
        if (it == nbResults.end()) continue;
        _applyNetBiosResult(d, it->second);
    }
    xSemaphoreGive(_mutex);

    Log::i(TAG, "NetBIOS fusionne - %u equipement(s) enrichis", (unsigned)nbResults.size());
}

// ---------------------------------------------------------------------------
// Enrichissement final : pattern matching sur le hostname resolu
//
// Derniere etape du scan - complete manufacturer/category/os encore vides
// a partir de mots-cles trouves dans le hostname (cf. device_enricher.h).
// ---------------------------------------------------------------------------
void NetworkScanner::_enrichDevices() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        applyDeviceEnrichment(d);
    }
    xSemaphoreGive(_mutex);
}

// ---------------------------------------------------------------------------
// Classification intelligente
//
// Combine plusieurs signaux deja collectes (services DNS-SD, ports ouverts,
// manufacturer, hostname) pour affiner la categorie quand elle est encore
// vide ou trop generique ("IoT" par defaut). N'ecrase jamais une categorie
// specifique deja deduite par une source plus fiable (SSDP, API, enricher).
// ---------------------------------------------------------------------------
void NetworkScanner::_classifyDevices() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        bool generic = d.category.isEmpty() || d.category == "IoT";
        if (!generic) continue;

        bool hasHttp  = d.openPorts.indexOf("HTTP") >= 0 || d.services.indexOf("HTTP") >= 0;
        bool hasSsh   = d.openPorts.indexOf("SSH")  >= 0 || d.services.indexOf("SSH")  >= 0;
        bool hasSmb   = d.openPorts.indexOf("SMB")  >= 0 || d.services.indexOf("SMB")  >= 0;
        bool hasRtsp  = d.openPorts.indexOf("RTSP") >= 0;
        bool hasMqtt  = d.openPorts.indexOf("MQTT") >= 0 || d.services.indexOf("MQTT") >= 0;
        bool hasHa    = d.openPorts.indexOf("HA")   >= 0 || d.services.indexOf("HA")   >= 0;
        bool hasPrint = d.openPorts.indexOf("IPP")  >= 0 || d.services.indexOf("Print") >= 0;
        bool hasAirplay = d.services.indexOf("AirPlay") >= 0;

        // Combinaisons de signaux les plus specifiques d'abord
        if (hasRtsp) {
            d.category = "Camera";
        } else if (hasHa) {
            d.category = "Smart Hub";
        } else if (hasSmb && (hasSsh || hasHttp)) {
            d.category = "NAS";
        } else if (hasPrint) {
            d.category = "Printer";
        } else if (hasAirplay) {
            d.category = "Speaker";
        } else if (hasMqtt && !hasSsh) {
            d.category = "IoT";   // confirme la categorie generique avec un signal fort
        } else if (hasSsh && !hasHttp && d.os.indexOf("Windows") < 0) {
            d.category = "Computer";
        } else if (d.category.isEmpty()) {
            d.category = "IoT";   // valeur par defaut conservee si aucun signal specifique
        }
    }
    xSemaphoreGive(_mutex);
}

// ---------------------------------------------------------------------------
// Historique : firstSeen/lastSeen/seenCount + journal des evenements
//
// Compare l'etat courant a l'etat precedent (avant ce scan) pour detecter :
//   - les nouveaux equipements ("new")
//   - les reapparitions apres une absence ("online")
//   - les passages hors ligne ("offline")
//   - les changements de champs significatifs ("changed" : ip/manufacturer/
//     category/hostname/openPorts)
// Met egalement a jour firstSeenEpoch/lastSeenEpoch/seenCount pour chaque
// equipement vu en ligne lors de ce scan.
// ---------------------------------------------------------------------------
void NetworkScanner::_updateHistory(const std::vector<NetworkDevice>& previous) {
    uint32_t epoch = timeSync.nowEpoch();

    auto findPrev = [&](const NetworkDevice& d) -> const NetworkDevice* {
        for (const auto& p : previous) {
            if ((!d.mac.isEmpty() && p.mac == d.mac) ||
                (d.mac.isEmpty() && p.ip == d.ip))
                return &p;
        }
        return nullptr;
    };

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        const NetworkDevice* prev = findPrev(d);
        String label = !d.alias.isEmpty() ? d.alias :
                       (!d.hostname.isEmpty() ? d.hostname : d.ip);

        if (!prev) {
            // Equipement jamais vu auparavant
            if (d.online) {
                if (epoch > 0) d.firstSeenEpoch = epoch;
                d.lastSeenEpoch = epoch;
                d.seenCount     = 1;
                deviceHistory.addEvent(d.mac, d.ip, label, "new");
            }
            continue;
        }

        // Reporter l'historique deja connu (l'enricher/merge a pu creer une
        // nouvelle entree distincte sans recopier ces champs)
        if (d.firstSeenEpoch == 0) d.firstSeenEpoch = prev->firstSeenEpoch;
        d.seenCount = prev->seenCount;

        if (d.online) {
            if (!prev->online)
                deviceHistory.addEvent(d.mac, d.ip, label, "online");
            if (epoch > 0) d.lastSeenEpoch = epoch;
            else d.lastSeenEpoch = prev->lastSeenEpoch;
            d.seenCount = prev->seenCount + 1;
            if (d.firstSeenEpoch == 0 && epoch > 0) d.firstSeenEpoch = epoch;

            // Detection des changements de champs significatifs
            auto checkChange = [&](const char* field, const String& oldV, const String& newV) {
                if (!oldV.isEmpty() && !newV.isEmpty() && oldV != newV)
                    deviceHistory.addEvent(d.mac, d.ip, label, "changed", field, oldV, newV);
            };
            checkChange("ip",           prev->ip,           d.ip);
            checkChange("manufacturer", prev->manufacturer, d.manufacturer);
            checkChange("category",     prev->category,     d.category);
            checkChange("hostname",     prev->hostname,      d.hostname);
            checkChange("openPorts",    prev->openPorts,     d.openPorts);
        } else if (prev->online) {
            deviceHistory.addEvent(d.mac, d.ip, label, "offline");
            d.lastSeenEpoch = prev->lastSeenEpoch;
        } else {
            d.lastSeenEpoch = prev->lastSeenEpoch;
        }
    }
    xSemaphoreGive(_mutex);
}

// ---------------------------------------------------------------------------
// Chargement des devices connus depuis LittleFS
// Injectés avec online=false — seront mis à jour si découverts par ARP/ICMP
// ---------------------------------------------------------------------------
void NetworkScanner::_mergePersistedDevices() {
    auto stored = deviceStore.load();
    if (stored.empty()) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (const auto& s : stored) {
        bool found = false;
        for (const auto& d : _results) {
            if ((!s.mac.isEmpty() && d.mac == s.mac) ||
                (!s.ip.isEmpty()  && d.ip  == s.ip)) {
                found = true; break;
            }
        }
        if (!found) _results.push_back(s);
    }
    xSemaphoreGive(_mutex);
}

// ---------------------------------------------------------------------------
// Sauvegarde des résultats dans LittleFS
// ---------------------------------------------------------------------------
void NetworkScanner::_saveToStore() {
    std::vector<NetworkDevice> copy;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    copy = _results;
    xSemaphoreGive(_mutex);
    deviceStore.save(copy);
}

// ---------------------------------------------------------------------------
// Statistiques du scan pour l'UI
// ---------------------------------------------------------------------------
ScanStats NetworkScanner::getStats() const {
    ScanStats s;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (const auto& d : _results) {
        s.known++;
        if (d.online) s.online++; else s.offline++;
    }
    xSemaphoreGive(_mutex);
    return s;
}

// ---------------------------------------------------------------------------
// API publique
// ---------------------------------------------------------------------------
void NetworkScanner::begin() {
    if (_mutex) return;   // Idempotent — guard contre la double initialisation
    _mutex = xSemaphoreCreateMutex();
    Log::i(TAG, "Module initialisé");
}

void NetworkScanner::startScan() {
    if (_scanning) {
        Log::w(TAG, "Scan déjà en cours — ignoré");
        return;
    }
    _scanning = true;
    // Stack 24 Ko : lwIP + DNS + std::map × 3 + HTTP SSDP + XML + DNS-SD multicast
    xTaskCreatePinnedToCore(_task, "net_scan", 24576, this, 1, &_taskHandle, 0);
    Log::i(TAG, "Scan réseau lancé");
}

bool NetworkScanner::isScanRunning() const { return _scanning; }

std::vector<NetworkDevice> NetworkScanner::getResults() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    auto copy = _results;
    xSemaphoreGive(_mutex);
    return copy;
}

String NetworkScanner::resultsToJson() const {
    uint32_t now = millis();
    // Copie locale sous mutex (durée < 1 ms) pour libérer rapidement
    std::vector<NetworkDevice> copy;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    copy = _results;
    xSemaphoreGive(_mutex);

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& d : copy) {
        JsonObject obj = arr.add<JsonObject>();
        obj["ip"]           = d.ip;
        obj["mac"]          = d.mac;
        obj["manufacturer"] = d.manufacturer;
        obj["hostname"]     = d.hostname;
        obj["category"]     = d.category;
        obj["model"]        = d.model;
        obj["os"]           = d.os;
        obj["source"]       = d.source;
        obj["elapsedMs"]    = now - d.lastSeen;
        obj["online"]       = d.online;
        obj["alias"]        = d.alias;
        obj["firstSeen"]    = d.firstSeenEpoch;
        obj["lastSeenAt"]   = d.lastSeenEpoch;
        obj["seenCount"]    = d.seenCount;
        String confLabel;
        obj["confidence"]      = _confidenceFor(d, confLabel);
        obj["confidenceLabel"] = confLabel;
        // services : tableau JSON ["HTTP","SSH","SMB"] depuis la chaîne pipe-séparée
        JsonArray svcArr = obj["services"].to<JsonArray>();
        if (!d.services.isEmpty()) {
            String tmp = d.services;
            int idx;
            while ((idx = tmp.indexOf('|')) >= 0) {
                svcArr.add(tmp.substring(0, idx));
                tmp = tmp.substring(idx + 1);
            }
            if (!tmp.isEmpty()) svcArr.add(tmp);
        }
        // openPorts : tableau JSON ["HTTP","SSH","SMB"] depuis la chaîne pipe-séparée
        JsonArray portArr = obj["openPorts"].to<JsonArray>();
        if (!d.openPorts.isEmpty()) {
            String tmp = d.openPorts;
            int idx;
            while ((idx = tmp.indexOf('|')) >= 0) {
                portArr.add(tmp.substring(0, idx));
                tmp = tmp.substring(idx + 1);
            }
            if (!tmp.isEmpty()) portArr.add(tmp);
        }
    }
    String json;
    serializeJson(doc, json);
    return json;
}

// ---------------------------------------------------------------------------
// Alias utilisateur : identifie l'equipement par MAC (priorite) ou IP
// ---------------------------------------------------------------------------
bool NetworkScanner::setAlias(const String& macOrIp, const String& alias) {
    bool found = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        if ((!d.mac.isEmpty() && d.mac == macOrIp) || d.ip == macOrIp) {
            d.alias = alias;
            found = true;
            break;
        }
    }
    xSemaphoreGive(_mutex);
    if (found) _saveToStore();
    return found;
}

// ---------------------------------------------------------------------------
// RAZ de la liste des equipements connus - repart sur une base vide,
// en conservant optionnellement les equipements alias / a fabricant resolu
// ---------------------------------------------------------------------------
int NetworkScanner::resetDevices(bool keepAlias, bool keepManufacturer) {
    int removed = 0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    std::vector<NetworkDevice> kept;
    for (auto& d : _results) {
        bool keep = (keepAlias && !d.alias.isEmpty()) ||
                    (keepManufacturer && !d.manufacturer.isEmpty());
        if (keep) kept.push_back(d);
        else      removed++;
    }
    _results = kept;
    xSemaphoreGive(_mutex);
    if (removed > 0) _saveToStore();
    return removed;
}

// ---------------------------------------------------------------------------
// Rafraichissement cible d'un seul equipement (par IP), sans relancer un
// scan complet du sous-reseau : sonde ARP + ICMP, puis - si l'equipement
// repond - resolution de nom, scan de ports et NetBIOS limites a cette IP.
// Refuse si un scan complet est en cours (_scanning).
// ---------------------------------------------------------------------------
bool NetworkScanner::rescanDevice(const String& ip) {
    if (_scanning) return false;

    bool known = false;
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (const auto& d : _results) {
            if (d.ip == ip) { known = true; break; }
        }
        xSemaphoreGive(_mutex);
    }
    if (!known) return false;

    _scanning = true;

    std::vector<NetworkDevice> previousState;
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        previousState = _results;
        xSemaphoreGive(_mutex);
    }

    // ── Sonde ARP directe sur l'IP ───────────────────────────────────────
    IPAddress target;
    target.fromString(ip);
    ip4_addr_t ip4target;
    IP4_ADDR(&ip4target, target[0], target[1], target[2], target[3]);
    etharp_request(netif_default, &ip4target);
    vTaskDelay(pdMS_TO_TICKS(250));

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _readArpTable();
    xSemaphoreGive(_mutex);

    bool isOnline = false;
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (const auto& d : _results) {
            if (d.ip == ip) { isOnline = d.online; break; }
        }
        xSemaphoreGive(_mutex);
    }

    // ── Repli ICMP si l'ARP n'a pas repondu ──────────────────────────────
    if (!isOnline) {
        auto pings = icmpScanner.pingWithTtl({ ip }, 300);
        if (!pings.empty() && pings[0].ttl > 0) {
            isOnline = true;
            xSemaphoreTake(_mutex, portMAX_DELAY);
            for (auto& d : _results) {
                if (d.ip == ip) {
                    d.online   = true;
                    d.lastSeen = millis();
                    if (d.os.isEmpty()) d.os = _osFromTtl(pings[0].ttl);
                    break;
                }
            }
            xSemaphoreGive(_mutex);
        } else {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            for (auto& d : _results) {
                if (d.ip == ip) { d.online = false; break; }
            }
            xSemaphoreGive(_mutex);
        }
    }

    // ── Enrichissement, uniquement si l'equipement repond ────────────────
    if (isOnline) {
        hostnameResolver.batchPtrDns({ ip });
        HostnameSource src = HostnameSource::None;
        String name = hostnameResolver.resolve(ip, src);

        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (auto& d : _results) {
            if (d.ip != ip) continue;
            if (!name.isEmpty()) {
                d.hostname = name;
                d.source   = hostnameSourceStr(src);
            }
            applyIspDetection(d);
            break;
        }
        xSemaphoreGive(_mutex);

        auto portResults = portScanner.scan({ ip }, 250);
        auto prIt = portResults.find(ip);
        if (prIt != portResults.end()) {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            for (auto& d : _results) {
                if (d.ip == ip) { _applyPortScanResult(d, prIt->second); break; }
            }
            xSemaphoreGive(_mutex);
        }

        bool needsHostname = false;
        {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            for (const auto& d : _results) {
                if (d.ip == ip) { needsHostname = d.hostname.isEmpty(); break; }
            }
            xSemaphoreGive(_mutex);
        }
        if (needsHostname) {
            auto nbResults = netBiosScanner.scan({ ip }, 250);
            auto nbIt = nbResults.find(ip);
            if (nbIt != nbResults.end()) {
                xSemaphoreTake(_mutex, portMAX_DELAY);
                for (auto& d : _results) {
                    if (d.ip == ip) { _applyNetBiosResult(d, nbIt->second); break; }
                }
                xSemaphoreGive(_mutex);
            }
        }
    }

    _enrichDevices();
    _classifyDevices();
    _updateHistory(previousState);
    _saveToStore();

    Log::i(TAG, "Rafraichissement cible terminé — %s (%s)", ip.c_str(), isOnline ? "en ligne" : "hors ligne");

    _scanning = false;
    return true;
}

// ---------------------------------------------------------------------------
// Sauvegarde / restauration complete - utilisee par /api/backup et /api/restore
// ---------------------------------------------------------------------------
String NetworkScanner::backupToJson() const {
    std::vector<NetworkDevice> copy;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    copy = _results;
    xSemaphoreGive(_mutex);

    JsonDocument doc;
    doc["version"] = PROJECT_VERSION;
    JsonArray arr = doc["devices"].to<JsonArray>();
    for (const auto& d : copy) {
        if (d.ip.isEmpty() && d.mac.isEmpty()) continue;
        JsonObject obj = arr.add<JsonObject>();
        obj["ip"]           = d.ip;
        obj["mac"]          = d.mac;
        obj["manufacturer"] = d.manufacturer;
        obj["hostname"]     = d.hostname;
        obj["category"]     = d.category;
        obj["model"]        = d.model;
        obj["os"]           = d.os;
        obj["source"]       = d.source;
        obj["services"]     = d.services;
        obj["openPorts"]    = d.openPorts;
        obj["alias"]        = d.alias;
        obj["firstSeen"]    = d.firstSeenEpoch;
        obj["lastSeenAt"]   = d.lastSeenEpoch;
        obj["seenCount"]    = d.seenCount;
    }

    String json;
    serializeJson(doc, json);
    return json;
}

bool NetworkScanner::restoreFromJson(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Log::w(TAG, "Restauration : JSON invalide (%s)", err.c_str());
        return false;
    }

    JsonArray arr = doc["devices"].as<JsonArray>();
    if (arr.isNull()) {
        Log::w(TAG, "Restauration : champ 'devices' absent");
        return false;
    }

    std::vector<NetworkDevice> restored;
    for (JsonObject obj : arr) {
        NetworkDevice d;
        d.ip            = obj["ip"]           | "";
        d.mac           = obj["mac"]          | "";
        d.manufacturer  = obj["manufacturer"] | "";
        d.hostname      = obj["hostname"]     | "";
        d.category      = obj["category"]     | "";
        d.model         = obj["model"]        | "";
        d.os            = obj["os"]           | "";
        d.source        = obj["source"]       | "";
        d.services      = obj["services"]     | "";
        d.openPorts     = obj["openPorts"]    | "";
        d.alias         = obj["alias"]        | "";
        d.firstSeenEpoch= obj["firstSeen"]    | 0;
        d.lastSeenEpoch = obj["lastSeenAt"]   | 0;
        d.seenCount     = obj["seenCount"]    | 0;
        d.online        = false;
        d.lastSeen      = 0;
        if (!d.ip.isEmpty() || !d.mac.isEmpty())
            restored.push_back(d);
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _results = restored;
    xSemaphoreGive(_mutex);
    _saveToStore();
    deviceHistory.clear();

    Log::i(TAG, "Restauration : %u équipement(s) importés", (unsigned)restored.size());
    return true;
}
