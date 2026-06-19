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
#include <time.h>                // strftime() pour l'export CSV (dates lisibles)
#include <algorithm>             // std::sort (éviction LRU des équipements)
#include "hostname_resolver.h"   // mDNS passif + PTR DNS batch
#include "isp_detector.h"        // Détection Free / Orange / SFR / Bouygues
#include "ssdp_scanner.h"        // Découverte UPnP/SSDP
#include "dns_sd_scanner.h"      // Découverte de services DNS-SD (RFC 6763)
#include "device_store.h"        // Persistance LittleFS
#include "icmp_scanner.h"        // Sonde ICMP pour IPs non trouvées par ARP
#include "port_scanner.h"        // Scan TCP ports communs + banner HTTP/SSH/FTP + API IoT
#include "netbios_scanner.h"     // Node Status NetBIOS (UDP 137) - hostnames Windows/Samba
#include "snmp_scanner.h"        // SNMP sysDescr (UDP 161) - fabricant/modele en texte clair
#include "media_api_scanner.h"   // API HTTP proprietaires : Cast, Sonos, Roku, Samsung TV
#include "device_enricher.h"     // Enrichissement par pattern matching sur le hostname
#include "device_history.h"      // Journal chronologique des evenements (nouveaux/changements)
#include "time_sync.h"           // Epoch NTP pour firstSeen/lastSeen
#include "system_health.h"       // Mode degrade — refuse scans/notes/config si heap critique
#include <WiFi.h>
#include <ArduinoJson.h>
#include <LittleFS.h>            // Diagnostics : espace utilise/libre
#include <Preferences.h>         // Persistance NVS de la frequence de surveillance (namespace "monitor")
#include "../../include/app_config.h"   // MDNS_HOSTNAME, PROJECT_NAME
#include "lwip/etharp.h"   // etharp_get_entry(), etharp_request()
#include "lwip/netif.h"    // netif_default
#include "lwip/inet.h"     // ip4addr_ntoa_r()
#include "../../include/oui_table.h"  // OUI_TABLE[] généré depuis data/oui.json
#include "../utils/logger.h"

static const char* TAG = "Scanner";
static const char* MONITOR_NVS_NAMESPACE = "monitor";
static const char* MONITOR_NVS_KEY       = "intervalMin";
static const char* MONITOR_NVS_ENABLED_KEY = "enabled";

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
                    h.type         = oui->type;        // "Smart Speaker", "Smart Display"… ("" si non renseigné)
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
// Le protocole ARP ne peut pas découvrir sa propre adresse IP : aucune
// réponse ARP n'est jamais reçue pour soi-même. Injecter donc manuellement
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
void NetworkScanner::_applySsdpResult(NetworkDevice& d, const NetworkDevice& sdev) {
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
}

void NetworkScanner::_mergeSsdp() {
    auto ssdpResults = ssdpScanner.scan(4000);
    if (ssdpResults.empty()) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (const auto& sdev : ssdpResults) {
        bool found = false;
        for (auto& d : _results) {
            if (d.ip != sdev.ip) continue;
            found = true;
            _applySsdpResult(d, sdev);
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
void NetworkScanner::_applyDnsSdResult(NetworkDevice& d, const DnsSdInfo& info) {
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
}

void NetworkScanner::_mergeDnsSd() {
    auto dnssdResults = dnsSdScanner.scan(9000);
    if (dnssdResults.empty()) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (const auto& kv : dnssdResults) {
        const String& ip      = kv.first;
        const DnsSdInfo& info = kv.second;

        bool found = false;
        for (auto& d : _results) {
            if (d.ip != ip) continue;
            found = true;
            _applyDnsSdResult(d, info);
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
    NetworkScanner* s = static_cast<NetworkScanner*>(self);
    s->_run();
    // Marge de pile restante au plus bas — surveille le risque de stack overflow
    // sans avoir à deviner une taille de pile a priori (TAG "NetScan" sur Serial)
    Log::i("NetScan", "Marge pile min. tache scan: %u octets", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    vTaskDelete(nullptr);
}

void NetworkScanner::_run() {
    uint32_t _t0 = millis();

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

    uint32_t durMs = millis() - _t0;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _lastScanMs = durMs;
    _scanMsTotal += durMs;
    _scanCount++;

    // Nouvel equipement = present en ligne maintenant mais absent (ou hors ligne) avant le scan
    for (const auto& d : _results) {
        if (!d.online) continue;
        bool wasOnline = false;
        for (const auto& p : previousState) {
            bool sameDevice = (!d.mac.isEmpty() && d.mac == p.mac) || (d.mac.isEmpty() && d.ip == p.ip);
            if (sameDevice && p.online) { wasOnline = true; break; }
        }
        if (!wasOnline) { _newDevicesPending = true; break; }
    }
    xSemaphoreGive(_mutex);

    Log::i(TAG, "Scan complet — %u équipement(s) total (%u ms)", (unsigned)_results.size(), (unsigned)durMs);
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
// Fabricants dont l'OUI est partage entre PC, portables, tablettes,
// smartphones, ecrans connectes et stations de travail. Pour ces marques,
// le seul prefixe MAC ne permet pas de deduire la categorie de l'equipement
// avec la meme fiabilite qu'un OUI dedie a une seule famille de produits
// (Espressif, Raspberry Pi, Amazon, Sonos...). On abaisse donc la confiance
// accordee a une identification basee uniquement sur l'OUI pour ces marques.
// ---------------------------------------------------------------------------
static const char* AMBIGUOUS_OUI_BRANDS[] = {
    "Apple", "Samsung", "Xiaomi", "Huawei", "Intel",
    "HP", "Dell", "Lenovo", "Microsoft", nullptr
};

static bool _isAmbiguousOuiBrand(const String& manufacturer) {
    for (int i = 0; AMBIGUOUS_OUI_BRANDS[i] != nullptr; i++) {
        if (manufacturer == AMBIGUOUS_OUI_BRANDS[i]) return true;
    }
    return false;
}

// Score de fiabilite generique associe a la source ayant produit une donnee
// (reutilise pour la marque, la categorie, le modele et le type - cf. tableau
// dans le commentaire de _confidenceFor).
static int _sourceTier(const String& source) {
    if (source == "Self")                                              return 100;
    if (source == "HueAPI" || source == "SynologyAPI" || source == "FreeboxAPI") return 95;
    if (source == "Cast" || source == "Sonos" || source == "Roku" || source == "SamsungTV") return 92;
    if (source == "mDNS")                                              return 90;
    if (source == "SNMP")                                              return 85;
    if (source.indexOf("SSDP") >= 0)                                   return 80;
    if (source == "PTR")                                               return 70;
    if (source == "NetBIOS")                                           return 65;
    if (source == "MAC")                                               return 60;
    return 40;   // Heuristiques : pattern matching hostname/ports/services
}

// ---------------------------------------------------------------------------
// Profil d'equipement probable - deduit des informations deja connues, pour
// choisir les modules de decouverte les plus pertinents pour la passe
// precise (rescanDevice). Simple hypothese : n'est jamais ecrit dans
// NetworkDevice, et n'a aucune incidence si elle se revele fausse (les
// modules manquants sont toujours disponibles via le scan approfondi
// "Unknown" qui les essaie tous).
// ---------------------------------------------------------------------------
String NetworkScanner::_profileFor(const NetworkDevice& d) {
    const String& cat   = d.category;
    const String& svc    = d.services;
    const String& mfr    = d.manufacturer;
    const String& ports  = d.openPorts;   // ex: "HTTP|SMB|IPP" - rempli par le port scan ciblé

    // Indices imprimante : 9100 (RAW), 515 (LPD), 631 (IPP/CUPS)
    if (cat == "Printer" ||
        ports.indexOf("IPP") >= 0 || ports.indexOf("LPD") >= 0 || ports.indexOf("IPP/CUPS") >= 0)
        return "Printer";

    // Indices NAS : port 5000 (DSM/UPnP), SMB, fabricant Synology/QNAP
    if (cat == "NAS" ||
        ports.indexOf("HTTP/5000") >= 0 ||
        mfr == "Synology" || mfr == "QNAP")
        return "NAS";

    if (cat == "Streaming" || cat == "Audio" ||
        svc.indexOf("Cast") >= 0 || svc.indexOf("AirPlay") >= 0 || svc.indexOf("Sonos") >= 0)
        return "Streaming";

    if (cat == "Home Automation" || cat == "Smart Home" ||
        mfr == "Philips Hue" || mfr == "Hue" || mfr == "Tuya / Smart Life" || mfr == "Sonoff / Itead" ||
        mfr == "Shelly / Allterco" || mfr == "Tado" || mfr == "Netatmo" || mfr == "Somfy")
        return "SmartHome";

    // Indices ordinateur : hostname/OUI PC, ports Windows (RPC/NetBIOS/SMB/RDP)
    if (cat == "Computer" || cat == "SBC" ||
        svc.indexOf("SSH") >= 0 || svc.indexOf("SMB") >= 0 || d.os.indexOf("Windows") >= 0 ||
        ports.indexOf("RPC") >= 0 || ports.indexOf("NetBIOS") >= 0 || ports.indexOf("RDP") >= 0)
        return "Computer";

    if (cat == "Mobile") return "Mobile";

    return "Unknown";
}

// ---------------------------------------------------------------------------
// Niveau de confiance de l'identification - explique a l'utilisateur d'ou
// vient la deduction manufacturer/category/model/type affichee dans l'UI.
//
// Le score affiche est volontairement prudent : plutot que de retenir le
// meilleur signal disponible, on retient le plus faible parmi marque et
// categorie (le maillon le plus incertain determine la confiance globale).
// Une OUI ambigue (Apple, Samsung, Xiaomi, Huawei, Intel, HP, Dell, Lenovo,
// Microsoft) ne suffit jamais a elle seule a justifier une confiance elevee,
// car le meme prefixe MAC peut designer un PC, un portable, une tablette,
// un smartphone, un ecran connecte ou une station de travail.
//
// L'infobulle (label) detaille la confiance par champ : Marque / Categorie /
// Modele / Type, pour que l'utilisateur comprenne quelle partie de la fiche
// est solide et laquelle reste une supposition.
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
        label = "Box FAI (DHCP) — Marque 100% · Categorie 100%";
        return 100;
    }

    bool ambiguousOui = (d.source == "MAC") && _isAmbiguousOuiBrand(d.manufacturer);

    int mfrConf = 0;
    if (!d.manufacturer.isEmpty()) {
        mfrConf = ambiguousOui ? 35 : _sourceTier(d.source);
    }

    int catConf = 0;
    if (!d.category.isEmpty()) {
        catConf = ambiguousOui ? 35 : _sourceTier(d.source);
        // Categorie "IoT" par defaut, sans port/service detecte : signal faible
        if (d.category == "IoT" && d.openPorts.isEmpty() && d.services.isEmpty())
            catConf = min(catConf, 30);
    }

    int modelConf = d.model.isEmpty() ? 0 : _sourceTier(d.source);
    int typeConf  = d.type.isEmpty()  ? 0 : _sourceTier(d.source);

    // Score global prudent : le maillon le plus faible entre marque et
    // categorie (s'ils existent tous les deux), sinon celui qui existe.
    int overall;
    if (mfrConf > 0 && catConf > 0) overall = min(mfrConf, catConf);
    else                            overall = max(mfrConf, catConf);
    if (overall == 0) overall = 20;   // Aucun signal

    label  = "Marque "     + String(mfrConf)  + "%";
    label += " · Categorie " + String(catConf) + "%";
    if (modelConf > 0) label += " · Modele " + String(modelConf) + "%";
    if (typeConf  > 0) label += " · Type "   + String(typeConf)  + "%";
    if (ambiguousOui) label += " (OUI partage entre plusieurs familles d'appareils)";

    return overall;
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
        case 53:   return "DNS";
        case 80:   return "HTTP";
        case 135:  return "RPC";
        case 139:  return "NetBIOS";
        case 443:  return "HTTPS";
        case 445:  return "SMB";
        case 515:  return "LPD";
        case 554:  return "RTSP";
        case 631:  return "IPP/CUPS";
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

// Heuristiques sysDescr SNMP → fabricant (texte libre, generalement explicite :
// "HP LaserJet...", "Cisco IOS...", "MikroTik RouterOS...")
static String _sysDescrToMfr(const String& descr) {
    String b = descr;
    b.toLowerCase();
    if (b.indexOf("hp ") >= 0 || b.indexOf("hewlett") >= 0) return "HP";
    if (b.indexOf("brother")  >= 0) return "Brother";
    if (b.indexOf("canon")    >= 0) return "Canon";
    if (b.indexOf("epson")    >= 0) return "Epson";
    if (b.indexOf("cisco")    >= 0) return "Cisco";
    if (b.indexOf("mikrotik") >= 0) return "MikroTik";
    if (b.indexOf("ubiquiti") >= 0) return "Ubiquiti";
    if (b.indexOf("synology") >= 0) return "Synology";
    if (b.indexOf("qnap")     >= 0) return "QNAP";
    if (b.indexOf("zyxel")    >= 0) return "Zyxel";
    if (b.indexOf("tp-link")  >= 0) return "TP-Link";
    if (b.indexOf("netgear")  >= 0) return "Netgear";
    if (b.indexOf("d-link")   >= 0) return "D-Link";
    if (b.indexOf("aruba")    >= 0) return "Aruba";
    if (b.indexOf("juniper")  >= 0) return "Juniper";
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

    // Titre HTML -> signature constructeur, si le banner n'a rien donne
    if (!pr.httpTitle.isEmpty()) {
        if (d.manufacturer.isEmpty()) {
            String mfr = _bannerToMfr(pr.httpTitle);
            if (!mfr.isEmpty()) d.manufacturer = mfr;
        }
        if (d.model.isEmpty() && pr.httpTitle.length() < 40) d.model = pr.httpTitle;
    }

    // API IoT identifiee precisement -> manufacturer/category/model/firmware
    if (!pr.iotType.isEmpty()) {
        if (d.manufacturer.isEmpty()) d.manufacturer = pr.iotType;
        if (d.category.isEmpty() || d.category == "IoT") {
            if (pr.iotType == "FritzBox") d.category = "Router";
            else if (pr.iotType == "Synology") d.category = "NAS";
            else if (pr.iotType == "Hue") d.category = "Home Automation";
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
void NetworkScanner::_updateHistory(const std::vector<NetworkDevice>& previous, bool allowRequeue) {
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
                d.presenceCount = 1;
                deviceHistory.addEvent(d.mac, d.ip, label, "new");
            }
            continue;
        }

        // Reporter l'historique deja connu (l'enricher/merge a pu creer une
        // nouvelle entree distincte sans recopier ces champs)
        if (d.firstSeenEpoch == 0) d.firstSeenEpoch = prev->firstSeenEpoch;
        d.seenCount             = prev->seenCount;
        d.presenceCount         = prev->presenceCount;
        d.absenceCount          = prev->absenceCount;
        d.reconnectionCount     = prev->reconnectionCount;
        d.lastDisconnectEpoch   = prev->lastDisconnectEpoch;
        d.totalOnlineSeconds    = prev->totalOnlineSeconds;
        d.totalOfflineSeconds   = prev->totalOfflineSeconds;
        d.mobilityOverride      = prev->mobilityOverride;
        d.mobileAwayNotified    = prev->mobileAwayNotified;

        int confBefore = 0;
        String confLabelTmp;
        if (!prev->online) confBefore = _confidenceFor(*prev, confLabelTmp);

        bool mobile = _isMobileDevice(d);

        if (d.online) {
            if (!prev->online) {
                // Reapparition - "reconnected" si l'equipement etait deja
                // connu (au moins une presence anterieure), sinon "online"
                // (comportement historique conserve pour ne pas casser les
                // filtres existants de la vue chronologique).
                if (prev->presenceCount > 0) {
                    d.reconnectionCount = prev->reconnectionCount + 1;
                    deviceHistory.addEvent(d.mac, d.ip, label, "reconnected");
                    if (mobile && d.mobileAwayNotified) {
                        d.mobileAwayNotified = false;
                        deviceHistory.addEvent(d.mac, d.ip, label, "mobile_returned");
                    }
                } else {
                    deviceHistory.addEvent(d.mac, d.ip, label, "online");
                }
            }
            d.presenceCount = prev->presenceCount + 1;
            if (epoch > 0) d.lastSeenEpoch = epoch;
            else d.lastSeenEpoch = prev->lastSeenEpoch;
            d.seenCount = prev->seenCount + 1;
            if (d.firstSeenEpoch == 0 && epoch > 0) d.firstSeenEpoch = epoch;

            // Cumul du temps hors ligne ecoule depuis la derniere deconnexion
            // connue (uniquement si l'horloge est synchronisee des deux cotes,
            // et si l'absence n'etait pas une absence mobile courte deja exclue
            // du decompte par _monitorTick()).
            if (!prev->online && epoch > 0 && prev->lastDisconnectEpoch > 0 &&
                epoch > prev->lastDisconnectEpoch && !(mobile)) {
                d.totalOfflineSeconds = prev->totalOfflineSeconds + (epoch - prev->lastDisconnectEpoch);
            }

            // Detection des changements de champs significatifs
            bool importantChange = false;
            auto checkChange = [&](const char* field, const String& oldV, const String& newV) {
                if (!oldV.isEmpty() && !newV.isEmpty() && oldV != newV) {
                    deviceHistory.addEvent(d.mac, d.ip, label, "changed", field, oldV, newV);
                    importantChange = true;
                }
            };
            checkChange("ip",           prev->ip,           d.ip);
            checkChange("manufacturer", prev->manufacturer, d.manufacturer);
            checkChange("category",     prev->category,     d.category);
            checkChange("type",         prev->type,          d.type);
            checkChange("hostname",     prev->hostname,      d.hostname);
            checkChange("openPorts",    prev->openPorts,     d.openPorts);

            // Amelioration sensible de l'identification (>= 15 points) -
            // journalisee independamment d'un "changed" de champ precis.
            String confLabelAfter;
            int confAfter = _confidenceFor(d, confLabelAfter);
            if (prev->online) {
                int confPrev = _confidenceFor(*prev, confLabelTmp);
                if (confAfter - confPrev >= 15) {
                    deviceHistory.addEvent(d.mac, d.ip, label, "identification_improved",
                                           "confidence", String(confPrev), String(confAfter));
                }
                // Confiance redevenue anormalement basse, ou changement
                // important detecte -> re-sondage via la file differee
                // (rafraichissement rapide, jamais immediat depuis ce thread).
                // allowRequeue est faux lorsque cet appel provient d'une passe
                // precise (_runRescan) : un scan rapide ne recueille volontairement
                // que peu d'informations et ne fera jamais remonter la confiance
                // au-dessus de 35% pour certains profils — sans cette garde, la
                // passe se remettrait elle-meme en file indefiniment.
                if (allowRequeue && (importantChange || confAfter < 35)) {
                    _queueQuickScanLocked(d.ip);
                }
            }
        } else if (prev->online) {
            // Transition present -> absent
            d.lastDisconnectEpoch = (epoch > 0) ? epoch : prev->lastDisconnectEpoch;
            if (!mobile) {
                d.absenceCount = prev->absenceCount + 1;
            }
            deviceHistory.addEvent(d.mac, d.ip, label, "offline");
            d.lastSeenEpoch = prev->lastSeenEpoch;
        } else {
            d.lastSeenEpoch = prev->lastSeenEpoch;

            // Absence prolongee d'un equipement mobile - cf. _monitorTick()
            // pour la meme logique appliquee entre deux scans complets.
            if (mobile && !d.mobileAwayNotified && d.lastDisconnectEpoch > 0 &&
                epoch > 0 && (epoch - d.lastDisconnectEpoch) * 1000UL >= MOBILE_AWAY_LONG_MS) {
                d.mobileAwayNotified = true;
                deviceHistory.addEvent(d.mac, d.ip, label, "mobile_left");
            }
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
    _evictOldestLocked();
    xSemaphoreGive(_mutex);
}

// ---------------------------------------------------------------------------
// Borne haute du nombre d'équipements suivis — évite une croissance illimitée
// du heap (ex: appareils à MAC aléatoire vus une seule fois). Appelée avec
// _mutex déjà acquis. Évince les entrées hors-ligne les moins récemment vues,
// jamais les favoris ni les équipements actuellement en ligne.
// ---------------------------------------------------------------------------
void NetworkScanner::_evictOldestLocked() {
    if (_results.size() <= MAX_TRACKED_DEVICES) return;

    std::sort(_results.begin(), _results.end(), [](const NetworkDevice& a, const NetworkDevice& b) {
        if (a.favorite != b.favorite) return !a.favorite && b.favorite; // favoris en dernier
        if (a.online != b.online) return !a.online && b.online;        // en ligne en dernier
        return a.lastSeenEpoch < b.lastSeenEpoch;                       // plus ancien en premier
    });

    size_t excess = _results.size() - MAX_TRACKED_DEVICES;
    _results.erase(_results.begin(), _results.begin() + excess);
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

    // Restauration de la frequence de surveillance continue (NVS, namespace
    // "monitor") — meme pattern que StatusLed::begin() pour la luminosite.
    Preferences prefs;
    prefs.begin(MONITOR_NVS_NAMESPACE, true);
    uint8_t storedMinutes = prefs.getUChar(MONITOR_NVS_KEY, MONITOR_INTERVAL_DEFAULT_MINUTES);
    bool    storedEnabled = prefs.getBool(MONITOR_NVS_ENABLED_KEY, true);
    prefs.end();
    if (storedMinutes < MONITOR_INTERVAL_MIN_MINUTES || storedMinutes > MONITOR_INTERVAL_MAX_MINUTES)
        storedMinutes = MONITOR_INTERVAL_DEFAULT_MINUTES;
    _monitorIntervalMinutes = storedMinutes;
    _monitorEnabled         = storedEnabled;

    Log::i(TAG, "Module initialisé — surveillance continue : %s, %u min",
           _monitorEnabled ? "activee" : "desactivee", (unsigned)_monitorIntervalMinutes);
}

void NetworkScanner::startScan() {
    if (_scanning) {
        Log::w(TAG, "Scan déjà en cours — ignoré");
        return;
    }
    if (systemHealth.isDegraded()) {
        Log::w(TAG, "Scan refusé — mode dégradé (%s)", systemHealth.reason().c_str());
        return;
    }
    _scanning = true;
    // Stack 24 Ko : lwIP + DNS + std::map × 3 + HTTP SSDP + XML + DNS-SD multicast
    xTaskCreatePinnedToCore(_task, "net_scan", 24576, this, 1, &_taskHandle, 0);
    Log::i(TAG, "Scan réseau lancé");
}

bool NetworkScanner::hasNewDevices() const {
    return _newDevicesPending;
}

void NetworkScanner::acknowledgeNewDevices() {
    _newDevicesPending = false;
}

void NetworkScanner::saveNow() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _saveToStore();
    xSemaphoreGive(_mutex);
}

bool NetworkScanner::isScanRunning() const { return _scanning; }

std::vector<NetworkDevice> NetworkScanner::getResults() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    auto copy = _results;
    xSemaphoreGive(_mutex);
    return copy;
}

// Nom "humain" du materiel - deduit de model/manufacturer/category, pour donner
// un intitule comprehensible au-dessus du hostname brut (ex: "device-72" -> "Google Nest Hub").
// Utilise uniquement pour l'export (CSV/JSON de /api/devices) - n'affecte jamais d.hostname.
static String _humanNameFor(const NetworkDevice& d) {
    if (!d.model.isEmpty()) return d.model;
    if (!d.manufacturer.isEmpty()) return d.type.isEmpty() ? d.manufacturer : d.manufacturer + " " + d.type;
    if (!d.category.isEmpty()) return d.category;
    return "";
}

// Hostname enrichi sur 3 lignes pour l'export : nom humain, puis hostname/alias affiche,
// puis la source de resolution - reproduit ce qu'affiche la colonne "Nom" de l'UI.
static String _enrichedHostname(const NetworkDevice& d) {
    String displayName = d.alias.isEmpty() ? d.hostname : d.alias;
    String human = _humanNameFor(d);
    String result;
    if (!human.isEmpty() && human != displayName) result += human + "\n";
    result += displayName + "\n";
    result += d.alias.isEmpty() ? d.source : "Alias";
    return result;
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
        obj["hostnameDisplay"] = _enrichedHostname(d);
        obj["category"]     = d.category;
        obj["type"]         = d.type;
        obj["model"]        = d.model;
        obj["os"]           = d.os;
        obj["source"]       = d.source;
        obj["elapsedMs"]    = now - d.lastSeen;
        obj["online"]       = d.online;
        obj["alias"]        = d.alias;
        obj["firstSeen"]    = d.firstSeenEpoch;
        obj["lastSeenAt"]   = d.lastSeenEpoch;
        obj["seenCount"]    = d.seenCount;
        obj["favorite"]     = d.favorite;
        JsonArray notesArr = obj["notes"].to<JsonArray>();
        for (const auto& n : d.notes) {
            JsonObject no = notesArr.add<JsonObject>();
            no["ts"]   = n.ts;
            no["text"] = n.text;
        }
        String confLabel;
        obj["confidence"]      = _confidenceFor(d, confLabel);
        obj["confidenceLabel"] = confLabel;
        // Surveillance continue / stabilite (v1.0.0)
        obj["presenceCount"]     = d.presenceCount;
        obj["absenceCount"]      = d.absenceCount;
        obj["reconnectionCount"] = d.reconnectionCount;
        obj["mobilityOverride"]  = d.mobilityOverride;
        obj["isMobile"]          = _isMobileDevice(d);
        obj["stabilityScore"]    = _stabilityScoreFor(d);
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
    if (systemHealth.isDegraded()) {
        Log::w(TAG, "Modification d'alias refusée — mode dégradé (%s)", systemHealth.reason().c_str());
        return false;
    }
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
// Favori utilisateur : identifie l'equipement par MAC (priorite) ou IP
// ---------------------------------------------------------------------------
bool NetworkScanner::setFavorite(const String& macOrIp, bool favorite) {
    if (systemHealth.isDegraded()) {
        Log::w(TAG, "Modification de favori refusée — mode dégradé (%s)", systemHealth.reason().c_str());
        return false;
    }
    bool found = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        if ((!d.mac.isEmpty() && d.mac == macOrIp) || d.ip == macOrIp) {
            d.favorite = favorite;
            found = true;
            break;
        }
    }
    xSemaphoreGive(_mutex);
    if (found) _saveToStore();
    return found;
}

// ---------------------------------------------------------------------------
// Notes utilisateur : inventaire libre (entretien, changements, observations)
// ---------------------------------------------------------------------------
bool NetworkScanner::addNote(const String& macOrIp, const String& text) {
    if (text.isEmpty()) return false;
    if (systemHealth.isDegraded()) {
        Log::w(TAG, "Note refusée — mode dégradé (%s)", systemHealth.reason().c_str());
        return false;
    }
    String trimmed = text.substring(0, MAX_NOTE_LENGTH);   // borne la taille d'une note
    bool found = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        if ((!d.mac.isEmpty() && d.mac == macOrIp) || d.ip == macOrIp) {
            DeviceNote n;
            n.ts   = timeSync.nowEpoch();
            n.text = trimmed;
            d.notes.push_back(n);
            // FIFO : borne le nombre de notes par equipement
            while (d.notes.size() > MAX_NOTES_PER_DEVICE)
                d.notes.erase(d.notes.begin());
            found = true;
            break;
        }
    }
    xSemaphoreGive(_mutex);
    if (found) _saveToStore();
    return found;
}

bool NetworkScanner::deleteNote(const String& macOrIp, uint32_t ts) {
    bool found = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        if ((!d.mac.isEmpty() && d.mac == macOrIp) || d.ip == macOrIp) {
            for (size_t i = 0; i < d.notes.size(); i++) {
                if (d.notes[i].ts == ts) {
                    d.notes.erase(d.notes.begin() + i);
                    found = true;
                    break;
                }
            }
            break;
        }
    }
    xSemaphoreGive(_mutex);
    if (found) _saveToStore();
    return found;
}

// ---------------------------------------------------------------------------
// Diagnostics : memoire, stockage, temps de scan moyens (cartouche UI)
// ---------------------------------------------------------------------------
ScanTimings NetworkScanner::getTimings() const {
    ScanTimings t;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    t.lastScanMs   = _lastScanMs;
    t.avgScanMs    = _scanCount   ? (_scanMsTotal   / _scanCount)   : 0;
    t.lastRescanMs = _lastRescanMs;
    t.avgRescanMs  = _rescanCount ? (_rescanMsTotal / _rescanCount) : 0;
    xSemaphoreGive(_mutex);
    return t;
}

String NetworkScanner::diagnosticsToJson() const {
    ScanTimings t = getTimings();
    JsonDocument doc;
    doc["freeHeap"]      = ESP.getFreeHeap();
    doc["freePsram"]     = ESP.getFreePsram();
    doc["fsUsedBytes"]   = LittleFS.usedBytes();
    doc["fsTotalBytes"]  = LittleFS.totalBytes();
    doc["lastScanMs"]    = t.lastScanMs;
    doc["avgScanMs"]     = t.avgScanMs;
    doc["lastRescanMs"]  = t.lastRescanMs;
    doc["avgRescanMs"]   = t.avgRescanMs;
    doc["degraded"]      = systemHealth.isDegraded();
    doc["degradedReason"]= systemHealth.reason();
    doc["deviceCount"]   = _results.size();
    doc["maxDevices"]    = MAX_TRACKED_DEVICES;
    doc["historyCount"]  = deviceHistory.load(0).size();
    String json;
    serializeJson(doc, json);
    return json;
}

// ---------------------------------------------------------------------------
// RAZ de la liste des equipements connus - repart sur une base vide,
// en conservant optionnellement les equipements alias / a fabricant resolu
// ---------------------------------------------------------------------------
int NetworkScanner::resetDevices(bool keepAlias, bool keepManufacturer) {
    if (systemHealth.isDegraded()) {
        Log::w(TAG, "Reset des équipements refusé — mode dégradé (%s)", systemHealth.reason().c_str());
        return 0;
    }
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
void NetworkScanner::_setRescanProgress(const String& step, int percent) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _rescanStatus.step    = step;
    _rescanStatus.percent = percent;
    xSemaphoreGive(_mutex);
    Log::i(TAG, "Rescan %s — %s (%d%%)", _rescanStatus.ip.c_str(), step.c_str(), percent);
}

bool NetworkScanner::rescanDevice(const String& ip, bool deep) {
    if (_scanning) return false;
    if (systemHealth.isDegraded()) {
        Log::w(TAG, "Rescan refusé — mode dégradé (%s)", systemHealth.reason().c_str());
        return false;
    }

    bool known = false;
    String profile = "Unknown";
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (const auto& d : _results) {
            if (d.ip == ip) { known = true; profile = _profileFor(d); break; }
        }
        xSemaphoreGive(_mutex);
    }
    if (!known) return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _rescanStatus.running = true;
    _rescanStatus.ok      = false;
    _rescanStatus.ip      = ip;
    _rescanStatus.step    = "Démarrage";
    _rescanStatus.percent = 0;
    _rescanStatus.mode     = deep ? "deep" : "quick";
    _rescanStatus.profile  = profile;
    _rescanStatus.log.clear();
    xSemaphoreGive(_mutex);

    _scanning = true;
    _rescanDeep = deep;
    xTaskCreatePinnedToCore(_rescanTask, "net_rescan", 24576, this, 1, &_rescanTaskHandle, 0);
    Log::i(TAG, "Passe précise (%s, profil %s) lancée — %s", deep ? "approfondie" : "rapide", profile.c_str(), ip.c_str());
    return true;
}

void NetworkScanner::_rescanTask(void* selfPtr) {
    NetworkScanner* self = static_cast<NetworkScanner*>(selfPtr);

    String ip;
    bool deep;
    {
        xSemaphoreTake(self->_mutex, portMAX_DELAY);
        ip   = self->_rescanStatus.ip;
        xSemaphoreGive(self->_mutex);
    }
    deep = self->_rescanDeep;
    self->_runRescan(ip, deep);

    xSemaphoreTake(self->_mutex, portMAX_DELAY);
    self->_rescanStatus.running = false;
    self->_rescanStatus.step    = "Terminé";
    self->_rescanStatus.percent = 100;
    xSemaphoreGive(self->_mutex);

    self->_scanning         = false;
    self->_rescanTaskHandle = nullptr;
    Log::i("NetScan", "Marge pile min. tache rescan: %u octets", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    vTaskDelete(nullptr);
}

void NetworkScanner::_runRescan(const String& ip, bool deep) {
    uint32_t _t0 = millis();
    std::vector<NetworkDevice> previousState;
    NetworkDevice before;
    String confLabel;
    int confBefore = 0;
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        previousState = _results;
        for (const auto& d : _results) {
            if (d.ip == ip) { before = d; confBefore = _confidenceFor(d, confLabel); break; }
        }
        xSemaphoreGive(_mutex);
    }

    // Profil hypothetique a partir des informations deja connues - affiche
    // tel quel pour le scan rapide. Le scan approfondi le redetermine apres
    // le scan de ports (cf. plus bas) car les indices les plus fiables (ports
    // ouverts) ne sont disponibles qu'a ce moment-la.
    String profile = _profileFor(before);
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _rescanStatus.profile = profile;
        xSemaphoreGive(_mutex);
    }

    std::vector<String> log;

    // ── Sonde ARP directe sur l'IP ───────────────────────────────────────
    _setRescanProgress("ARP", deep ? 5 : 15);
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
        _setRescanProgress("ICMP", deep ? 10 : 30);
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
        _setRescanProgress("Résolution hostname", deep ? 20 : 60);
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

        if (!deep) {
            // ── Scan rapide : confirmation de presence + hostname, rien d'autre ──
            _setRescanProgress("Vérification présence", 90);
        } else {
            // ── Scan approfondi ────────────────────────────────────────────
            // Etape 1 : scanner uniquement les ports de l'equipement cible
            // (jamais de decouverte multicast SSDP/DNS-SD/WS-Discovery, qui
            // interrogerait tout le sous-reseau pour ne garder qu'une IP).
            _setRescanProgress("Services réseau", 30);
            auto portResults = portScanner.scan({ ip }, 500, &kRescanTargetPorts);
            auto prIt = portResults.find(ip);
            bool hasExploitableService = (prIt != portResults.end());
            if (hasExploitableService) {
                xSemaphoreTake(_mutex, portMAX_DELAY);
                for (auto& d : _results) {
                    if (d.ip == ip) { _applyPortScanResult(d, prIt->second); break; }
                }
                xSemaphoreGive(_mutex);
            }

            if (!hasExploitableService) {
                // Regle importante : aucun service exploitable -> on arrete
                // immediatement la passe approfondie, sans lancer d'autres
                // modules (NetBIOS, API constructeur, SNMP...).
                log.push_back("Aucun service exploitable détecté. Passe approfondie terminée.");
                Log::i(TAG, "%s — aucun service exploitable, passe approfondie écourtée", ip.c_str());
            } else {
                // Etape 2 : le profil est redetermine maintenant que les ports
                // ouverts de la cible sont connus - bien plus fiable que
                // l'hypothese initiale basee sur le seul historique.
                NetworkDevice updated;
                {
                    xSemaphoreTake(_mutex, portMAX_DELAY);
                    for (const auto& d : _results) {
                        if (d.ip == ip) { updated = d; break; }
                    }
                    xSemaphoreGive(_mutex);
                }
                profile = _profileFor(updated);
                xSemaphoreTake(_mutex, portMAX_DELAY);
                _rescanStatus.profile = profile;
                xSemaphoreGive(_mutex);
                Log::i(TAG, "%s — profil déduit après scan de ports : %s (ports: %s)",
                       ip.c_str(), profile.c_str(), updated.openPorts.c_str());

                // Modules specialises, declenches uniquement si le profil
                // deduit en a l'utilite — jamais de decouverte multicast.
                bool wantNetBios = (profile == "Computer" || profile == "Unknown") &&
                                    (updated.openPorts.indexOf("NetBIOS") >= 0 ||
                                     updated.openPorts.indexOf("RPC") >= 0 ||
                                     updated.openPorts.indexOf("SMB") >= 0 ||
                                     updated.hostname.isEmpty());
                bool wantMedia   = (profile == "Streaming" || profile == "SmartHome");
                bool wantSnmp    = (profile == "Printer" || profile == "Unknown");

                if (wantNetBios) {
                    _setRescanProgress("APIs constructeur", 55);
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

                // API HTTP proprietaires (Cast / Sonos / Roku / Samsung TV) -
                // requetes unicast directes sur des ports fixes, deja ciblees
                // sur l'IP visee (aucune decouverte multicast impliquee).
                if (wantMedia) {
                    _setRescanProgress("Services multimédia", 70);
                    MediaApiResult mediaRes = mediaApiScanner.probe(ip, 600);
                    if (!mediaRes.apiType.isEmpty()) {
                        xSemaphoreTake(_mutex, portMAX_DELAY);
                        for (auto& d : _results) {
                            if (d.ip != ip) continue;
                            if (!mediaRes.manufacturer.isEmpty() &&
                                (d.manufacturer.isEmpty() || _isAmbiguousOuiBrand(d.manufacturer))) {
                                d.manufacturer = mediaRes.manufacturer;
                            }
                            if (!mediaRes.model.isEmpty()) d.model = mediaRes.model;
                            if (!mediaRes.category.isEmpty() && d.category.isEmpty()) d.category = mediaRes.category;
                            if (!mediaRes.friendlyName.isEmpty() && d.hostname.isEmpty()) d.hostname = mediaRes.friendlyName;
                            d.source = mediaRes.apiType;
                            break;
                        }
                        xSemaphoreGive(_mutex);
                    }
                }

                // SNMP sysDescr - requete UDP unicast directe sur l'IP visee.
                if (wantSnmp) {
                    _setRescanProgress("SNMP", 90);
                    auto snmpResults = snmpScanner.querySysDescr({ ip }, 400);
                    auto snIt = snmpResults.find(ip);
                    if (snIt != snmpResults.end()) {
                        xSemaphoreTake(_mutex, portMAX_DELAY);
                        for (auto& d : _results) {
                            if (d.ip != ip) continue;
                            if (d.model.isEmpty()) d.model = snIt->second;
                            String mfr = _sysDescrToMfr(snIt->second);
                            if (!mfr.isEmpty() && (d.manufacturer.isEmpty() || _isAmbiguousOuiBrand(d.manufacturer))) {
                                d.manufacturer = mfr;
                            }
                            if (d.source.isEmpty() || d.source == "MAC") d.source = "SNMP";
                            break;
                        }
                        xSemaphoreGive(_mutex);
                    }
                }
            }
        }
    }

    _enrichDevices();
    _classifyDevices();
    _updateHistory(previousState, /*allowRequeue=*/false);
    _saveToStore();

    // ── Journal des enrichissements ──────────────────────────────────────
    // Compare l'etat avant/apres pour produire un resume comprehensible des
    // gains de cette passe. Si la passe a ete ecourtee (aucun service
    // exploitable), le message correspondant est deja dans `log`.
    if (log.empty()) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (const auto& d : _results) {
            if (d.ip != ip) continue;
            String confLabelAfter;
            int confAfter = _confidenceFor(d, confLabelAfter);

            if (!d.model.isEmpty() && d.model != before.model)
                log.push_back("Modèle détecté : " + d.model);
            if (!d.manufacturer.isEmpty() && d.manufacturer != before.manufacturer)
                log.push_back("Fabricant détecté : " + d.manufacturer);
            if (!d.category.isEmpty() && d.category != before.category)
                log.push_back("Catégorie : " + d.category);
            if (!d.hostname.isEmpty() && d.hostname != before.hostname)
                log.push_back("Nom d'hôte détecté : " + d.hostname);
            if (!d.services.isEmpty() && d.services != before.services)
                log.push_back("Service détecté : " + d.services);
            if (!d.openPorts.isEmpty() && d.openPorts != before.openPorts)
                log.push_back("Ports détectés : " + d.openPorts);
            if (confAfter != confBefore)
                log.push_back("Confiance : " + String(confBefore) + "% → " + String(confAfter) + "%");
            break;
        }
        xSemaphoreGive(_mutex);
    }
    if (log.empty()) log.push_back("Aucune information supplémentaire détectée");

    uint32_t durMs = millis() - _t0;

    Log::i(TAG, "Rafraichissement cible terminé — %s (%s, %s, profil %s, %u ms)",
           ip.c_str(), isOnline ? "en ligne" : "hors ligne", deep ? "approfondi" : "rapide",
           profile.c_str(), (unsigned)durMs);

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _rescanStatus.ok  = isOnline;
    _rescanStatus.log = log;
    _lastRescanMs = durMs;
    _rescanMsTotal += durMs;
    _rescanCount++;
    xSemaphoreGive(_mutex);
}

RescanStatus NetworkScanner::getRescanStatus() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    RescanStatus s = _rescanStatus;
    xSemaphoreGive(_mutex);
    return s;
}

String NetworkScanner::rescanStatusToJson() const {
    RescanStatus s = getRescanStatus();
    JsonDocument doc;
    doc["running"] = s.running;
    doc["ok"]      = s.ok;
    doc["ip"]      = s.ip;
    doc["step"]    = s.step;
    doc["percent"] = s.percent;
    doc["mode"]    = s.mode;
    doc["profile"] = s.profile;
    JsonArray logArr = doc["log"].to<JsonArray>();
    for (const auto& l : s.log) logArr.add(l);
    String json;
    serializeJson(doc, json);
    return json;
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
        obj["type"]         = d.type;
        obj["model"]        = d.model;
        obj["os"]           = d.os;
        obj["source"]       = d.source;
        obj["services"]     = d.services;
        obj["openPorts"]    = d.openPorts;
        obj["alias"]        = d.alias;
        obj["firstSeen"]    = d.firstSeenEpoch;
        obj["lastSeenAt"]   = d.lastSeenEpoch;
        obj["seenCount"]    = d.seenCount;
        obj["favorite"]     = d.favorite;
        String confLabel;
        obj["confidence"]      = _confidenceFor(d, confLabel);
        obj["confidenceLabel"] = confLabel;
        JsonArray notesArr = obj["notes"].to<JsonArray>();
        for (const auto& n : d.notes) {
            JsonObject no = notesArr.add<JsonObject>();
            no["ts"]   = n.ts;
            no["text"] = n.text;
        }
    }

    String json;
    serializeJson(doc, json);
    return json;
}

bool NetworkScanner::restoreFromJson(const String& json) {
    if (systemHealth.isDegraded()) {
        Log::w(TAG, "Restauration refusée — mode dégradé (%s)", systemHealth.reason().c_str());
        return false;
    }
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
        d.type          = obj["type"]         | "";
        d.model         = obj["model"]        | "";
        d.os            = obj["os"]           | "";
        d.source        = obj["source"]       | "";
        d.services      = obj["services"]     | "";
        d.openPorts     = obj["openPorts"]    | "";
        d.alias         = obj["alias"]        | "";
        d.firstSeenEpoch= obj["firstSeen"]    | 0;
        d.lastSeenEpoch = obj["lastSeenAt"]   | 0;
        d.seenCount     = obj["seenCount"]    | 0;
        d.favorite      = obj["favorite"]     | false;
        JsonArray notesArr = obj["notes"].as<JsonArray>();
        for (JsonObject no : notesArr) {
            DeviceNote n;
            n.ts   = no["ts"]   | 0;
            String text = no["text"] | "";
            n.text = text.substring(0, MAX_NOTE_LENGTH);
            d.notes.push_back(n);
            while (d.notes.size() > MAX_NOTES_PER_DEVICE)
                d.notes.erase(d.notes.begin());
        }
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

// ---------------------------------------------------------------------------
// Export CSV - utilise par /api/devices/export.csv (tableur, scripts externes)
// ---------------------------------------------------------------------------
// Aplatit les retours a la ligne (note libre, hostname multi-source...) sur
// un espace avant echappement : un enregistrement = une seule ligne physique,
// meme pour les tableurs/scripts qui ne respectent pas les guillemets RFC4180.
static String csvField(const String& s) {
    String flat = s;
    flat.replace("\r\n", " ");
    flat.replace('\n', ' ');
    flat.replace('\r', ' ');
    bool needsQuotes = flat.indexOf(',') >= 0 || flat.indexOf('"') >= 0;
    if (!needsQuotes) return flat;
    flat.replace("\"", "\"\"");
    return "\"" + flat + "\"";
}

// Date lisible (heure locale, synchronisee par NTP) pour l'export CSV uniquement
// — le JSON garde les epochs bruts pour rester exploitable par /api/restore.
static String csvDate(uint32_t epoch) {
    if (epoch == 0) return "";
    time_t t = (time_t)epoch;
    struct tm tmInfo;
    localtime_r(&t, &tmInfo);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmInfo);
    return String(buf);
}

static String csvNotes(const std::vector<DeviceNote>& notes) {
    String joined;
    for (size_t i = 0; i < notes.size(); i++) {
        if (i) joined += " | ";
        joined += notes[i].text;
    }
    return joined;
}

String NetworkScanner::devicesToCsv() const {
    std::vector<NetworkDevice> copy;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    copy = _results;
    xSemaphoreGive(_mutex);

    String csv;
    csv.reserve(96 + copy.size() * 200);   // évite les réallocations répétées (fragmentation heap)
    csv = "ip,mac,hostname,alias,manufacturer,model,category,type,os,services,openPorts,online,favorite,confidence,notes,firstSeen,lastSeenAt,seenCount\n";
    for (const auto& d : copy) {
        if (d.ip.isEmpty() && d.mac.isEmpty()) continue;
        String confLabel;
        int confidence = _confidenceFor(d, confLabel);
        csv += csvField(d.ip)           + ",";
        csv += csvField(d.mac)          + ",";
        csv += csvField(d.hostname)     + ",";
        csv += csvField(d.alias)        + ",";
        csv += csvField(d.manufacturer) + ",";
        csv += csvField(d.model)        + ",";
        csv += csvField(d.category)     + ",";
        csv += csvField(d.type)         + ",";
        csv += csvField(d.os)           + ",";
        csv += csvField(d.services)     + ",";
        csv += csvField(d.openPorts)    + ",";
        csv += String(d.online   ? "Yes" : "No") + ",";
        csv += String(d.favorite ? "Yes" : "No") + ",";
        csv += String(confidence)             + ",";
        csv += csvField(csvNotes(d.notes))    + ",";
        csv += csvField(csvDate(d.firstSeenEpoch)) + ",";
        csv += csvField(csvDate(d.lastSeenEpoch))  + ",";
        csv += String(d.seenCount)      + "\n";
    }
    return csv;
}

// ---------------------------------------------------------------------------
// Surveillance continue du reseau et score de stabilite (v1.0.0)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Classification mobile/fixe — l'override utilisateur (mobilityOverride)
// gagne toujours. A defaut, on infere depuis category/type : categories
// typiquement portees sur soi ou facilement deplacees (smartphone, tablette,
// montre connectee, portable) -> mobile ; categories typiquement fixes
// (NAS, imprimante, camera, routeur, TV, enceinte, hub domotique, serveur,
// SBC, streaming) -> fixe. Ambigu -> fixe (hypothese conservatrice : on
// prefere signaler une instabilite reelle plutot que la masquer a tort).
// ---------------------------------------------------------------------------
bool NetworkScanner::_isMobileDevice(const NetworkDevice& d) {
    if (d.mobilityOverride == "fixed")  return false;
    if (d.mobilityOverride == "mobile") return true;

    String cat = d.category; cat.toLowerCase();
    String typ = d.type;     typ.toLowerCase();

    auto has = [&](const char* needle) {
        return cat.indexOf(needle) >= 0 || typ.indexOf(needle) >= 0;
    };

    if (has("mobile") || has("smartphone") || has("phone") ||
        has("tablet") || has("tablette") || has("watch") ||
        has("montre") || has("laptop") || has("portable"))
        return true;

    if (has("nas") || has("printer") || has("imprimante") ||
        has("camera") || has("router") || has("routeur") ||
        has("tv") || has("speaker") || has("enceinte") ||
        has("smarthub") || has("smart hub") || has("smarthome") ||
        has("smart home") || has("home automation") || has("server") ||
        has("serveur") || has("sbc") || has("streaming") || has("gateway"))
        return false;

    // Categorie ambigue ou vide -> fixe par defaut (conservateur)
    return false;
}

// ---------------------------------------------------------------------------
// Score de stabilite 0-100% — uniquement pertinent pour les equipements fixes
// (les mobiles ne sont jamais penalises, cf. spec : retour -1 = "N/A").
//
// Heuristique volontairement simple :
//   - Historique trop court (< 1h observe au total) -> 100% par defaut,
//     pas de penalite prematuree (on n'a pas encore assez d'information).
//   - Sinon, ratio tempsEnLigne / tempsTotalObserve, exprime en %.
//   - Penalite additionnelle proportionnelle a la frequence de reconnexion
//     (plus de reconnexions par heure observee = equipement plus instable
//     que ne le suggere le seul ratio de temps, ex: un routeur qui flappe
//     toutes les 10 minutes mais reste online la plupart du temps).
// ---------------------------------------------------------------------------
int NetworkScanner::_stabilityScoreFor(const NetworkDevice& d) {
    if (_isMobileDevice(d)) return -1;   // N/A — jamais penalise

    uint32_t totalSeconds = d.totalOnlineSeconds + d.totalOfflineSeconds;
    // Moins d'1h d'observation cumulee : pas assez d'historique pour juger
    if (totalSeconds < 3600) return 100;

    float ratio = (float)d.totalOnlineSeconds / (float)totalSeconds;   // 0..1
    int base = (int)(ratio * 100.0f);

    // Penalite reconnexions : -2 points par reconnexion/heure observee,
    // plafonnee a -30 points pour ne jamais ecraser totalement le ratio.
    float hours = totalSeconds / 3600.0f;
    float reconnectsPerHour = hours > 0 ? (d.reconnectionCount / hours) : 0;
    int penalty = (int)(reconnectsPerHour * 2.0f);
    if (penalty > 30) penalty = 30;

    int score = base - penalty;
    if (score < 0)   score = 0;
    if (score > 100) score = 100;
    return score;
}

// ---------------------------------------------------------------------------
// File d'attente differee (scan rapide / approfondi) — protegee par _mutex,
// dedupliquee. Drainee par _drainPendingScans() une entree a la fois pour
// eviter toute "tempete" de rescans simultanes.
// ---------------------------------------------------------------------------
void NetworkScanner::_queueQuickScanLocked(const String& ip) {
    for (const auto& q : _pendingQuickScan) if (q == ip) return;
    _pendingQuickScan.push_back(ip);
}

void NetworkScanner::_queueDeepScanLocked(const String& ip) {
    for (const auto& q : _pendingDeepScan) if (q == ip) return;
    _pendingDeepScan.push_back(ip);
}

void NetworkScanner::_drainPendingScans() {
    if (_scanning) return;   // Mutuelle exclusion avec scan complet/rescan

    String ip;
    bool deep = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!_pendingQuickScan.empty()) {
        ip = _pendingQuickScan.front();
        _pendingQuickScan.erase(_pendingQuickScan.begin());
        deep = false;
    } else if (!_pendingDeepScan.empty()) {
        ip = _pendingDeepScan.front();
        _pendingDeepScan.erase(_pendingDeepScan.begin());
        deep = true;
    }
    xSemaphoreGive(_mutex);

    if (ip.isEmpty()) return;   // Rien a drainer ce tick

    // Une seule passe demarree par drain — evite les tempetes de rescans.
    // rescanDevice() refuse lui-meme si _scanning est devenu vrai entre-temps.
    rescanDevice(ip, deep);
}

// ---------------------------------------------------------------------------
// Tick de surveillance continue — sweep ARP seul + mise a jour de presence/
// absence/compteurs de stabilite. Jamais de SSDP/DNS-SD/WS-Discovery/SNMP/API
// ici : uniquement ARP (deja implemente par _sweepSubnet(), reutilise as-is).
// ---------------------------------------------------------------------------
void NetworkScanner::_monitorTick() {
    // Etat de reference avant le tick — pour detecter les transitions
    std::vector<NetworkDevice> previousState;
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        previousState = _results;
        xSemaphoreGive(_mutex);
    }

    // Sweep ARP-only (3 passes, identique au scan complet) — reutilise
    // tel quel, aucune nouvelle decouverte de service.
    _sweepSubnet();

    uint32_t epoch  = timeSync.nowEpoch();
    uint32_t nowMs  = millis();

    auto findPrev = [&](const NetworkDevice& d) -> const NetworkDevice* {
        for (const auto& p : previousState) {
            if ((!d.mac.isEmpty() && p.mac == d.mac) ||
                (d.mac.isEmpty() && p.ip == d.ip))
                return &p;
        }
        return nullptr;
    };

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        const NetworkDevice* prev = findPrev(d);
        bool wasOnline    = prev ? prev->online : false;
        bool isNewDevice  = (prev == nullptr);
        String label = !d.alias.isEmpty() ? d.alias :
                       (!d.hostname.isEmpty() ? d.hostname : d.ip);
        bool mobile = _isMobileDevice(d);

        // Report des compteurs deja connus (le sweep ARP peut avoir recree
        // une entree sans recopier ces champs lui-meme)
        if (prev) {
            d.presenceCount       = prev->presenceCount;
            d.absenceCount        = prev->absenceCount;
            d.reconnectionCount   = prev->reconnectionCount;
            d.lastDisconnectEpoch = prev->lastDisconnectEpoch;
            d.totalOnlineSeconds  = prev->totalOnlineSeconds;
            d.totalOfflineSeconds = prev->totalOfflineSeconds;
            d.mobileAwayNotified  = prev->mobileAwayNotified;
            if (d.mobilityOverride.isEmpty()) d.mobilityOverride = prev->mobilityOverride;
        }

        if (isNewDevice) {
            // Nouvel equipement jamais vu — point 2 de la spec : etat
            // "Identification en cours". Aucun scan automatique declenche ici :
            // la surveillance continue se limite a la presence (ARP), l'identification
            // approfondie reste a l'initiative de l'utilisateur (scan complet ou rescan manuel).
            if (d.online) {
                if (epoch > 0) d.firstSeenEpoch = epoch;
                d.lastSeenEpoch = epoch;
                d.seenCount     = 1;
                d.presenceCount = 1;
                if (d.category.isEmpty()) d.category = "Identification en cours";
                deviceHistory.addEvent(d.mac, d.ip, label, "new");
            }
            continue;
        }

        if (d.online && !wasOnline) {
            // Reapparition — point 3 : equipement connu qui revient
            d.presenceCount++;
            d.seenCount = prev->seenCount + 1;
            if (epoch > 0) d.lastSeenEpoch = epoch;
            if (d.firstSeenEpoch == 0 && epoch > 0) d.firstSeenEpoch = epoch;

            bool wasAwayLong = mobile && d.mobileAwayNotified;
            if (wasAwayLong) {
                // Retour d'un mobile signale comme longuement absent
                deviceHistory.addEvent(d.mac, d.ip, label, "mobile_returned");
                d.mobileAwayNotified = false;
            } else if (mobile && d.lastDisconnectEpoch > 0 && epoch > 0 &&
                       (epoch - d.lastDisconnectEpoch) * 1000UL < MOBILE_AWAY_SHORT_MS) {
                // Absence courte d'un mobile — pas de penalite, pas d'evenement bruyant
                deviceHistory.addEvent(d.mac, d.ip, label, "reconnected");
            } else {
                // Reconnexion "normale" (fixe, ou mobile absent moyennement longtemps)
                d.reconnectionCount++;
                deviceHistory.addEvent(d.mac, d.ip, label, "reconnected");
            }

            // Cumul du temps hors ligne ecoule depuis la derniere deconnexion
            // (sauf absence mobile courte, deja exclue de la penalite ci-dessus
            // au niveau evenement — on cumule neanmoins le temps reel observe
            // pour les equipements fixes, qui doivent rester comparables)
            if (!mobile && epoch > 0 && d.lastDisconnectEpoch > 0 && epoch > d.lastDisconnectEpoch) {
                d.totalOfflineSeconds += (epoch - d.lastDisconnectEpoch);
            }

            // Changement de champ important detecte sur cette reapparition —
            // signale a titre informatif uniquement : la surveillance continue
            // ne declenche plus de scan automatique (presence ARP seule).

        } else if (d.online && wasOnline) {
            // Toujours en ligne — cumul du temps en ligne depuis le dernier tick
            d.seenCount = prev->seenCount + 1;
            if (epoch > 0) d.lastSeenEpoch = epoch;
            if (!mobile && epoch > 0 && prev->lastSeenEpoch > 0 && epoch > prev->lastSeenEpoch) {
                d.totalOnlineSeconds += (epoch - prev->lastSeenEpoch);
            }

            // Identification ameliorée : confiance en hausse significative
            // (suivi informatif uniquement — aucun scan approfondi automatique :
            // celui-ci reste a l'initiative de l'utilisateur)
            String labelBefore, labelAfter;
            int confBefore = _confidenceFor(*prev, labelBefore);
            int confAfter  = _confidenceFor(d, labelAfter);
            if (confAfter - confBefore >= 15) {
                deviceHistory.addEvent(d.mac, d.ip, label, "identification_improved", "confidence",
                                        String(confBefore) + "%", String(confAfter) + "%");
            }

            d.lastDisconnectEpoch = 0;   // Toujours en ligne — pas de deconnexion en cours

        } else if (!d.online && wasOnline) {
            // Passage hors ligne — point 9 : traitement special mobile
            d.lastDisconnectEpoch = epoch;

            if (mobile) {
                // Absence courte (<30 min) : aucune penalite, aucun evenement bruyant
                uint32_t awayMs = (prev->lastSeen > 0) ? (nowMs - prev->lastSeen) : 0;
                if (awayMs >= MOBILE_AWAY_LONG_MS && !d.mobileAwayNotified) {
                    String who = !d.alias.isEmpty() ? d.alias : (!d.hostname.isEmpty() ? d.hostname : d.ip);
                    deviceHistory.addEvent(d.mac, d.ip, label, "mobile_left", "", "", who + " a quitte le reseau");
                    d.mobileAwayNotified = true;
                }
                // awayMs entre 30min et 2h : silence total (ni penalite, ni evenement)
            } else {
                d.absenceCount++;
                deviceHistory.addEvent(d.mac, d.ip, label, "disappeared");
            }
        } else {
            // Toujours hors ligne — cumul du temps hors ligne pour les fixes,
            // sauf si l'absence mobile est encore "courte" (pas de penalite)
            if (!mobile && epoch > 0 && prev->lastSeenEpoch > 0) {
                // On cumule uniquement depuis le dernier tick (pas depuis la
                // deconnexion initiale, deja comptee au moment de la transition)
            }
            d.lastSeenEpoch = prev->lastSeenEpoch;

            if (mobile && !d.mobileAwayNotified) {
                uint32_t awayMs = (prev->lastSeen > 0) ? (nowMs - prev->lastSeen) : 0;
                if (awayMs >= MOBILE_AWAY_LONG_MS) {
                    String who = !d.alias.isEmpty() ? d.alias : (!d.hostname.isEmpty() ? d.hostname : d.ip);
                    deviceHistory.addEvent(d.mac, d.ip, label, "mobile_left", "", "", who + " a quitte le reseau");
                    d.mobileAwayNotified = true;
                }
            }
        }
    }
    xSemaphoreGive(_mutex);

    _saveToStore();
    _lastMonitorTickMs = nowMs;
}

void NetworkScanner::serviceMonitor() {
    if (!_monitorEnabled) return;   // Surveillance continue desactivee par l'utilisateur

    uint32_t intervalMs = _monitorIntervalMinutes * 60UL * 1000UL;
    uint32_t nowMs = millis();

    // Premier appel ou intervalle ecoule ?
    bool due = (_lastMonitorTickMs == 0) || (nowMs - _lastMonitorTickMs >= intervalMs);
    if (due) {
        if (_scanning) {
            // Scan complet ou rescan en cours — on saute ce tick. On reporte
            // l'echeance d'un intervalle complet (comme pour le mode degrade
            // ci-dessous) plutot que de relancer le tick aussitot le scan en
            // cours termine : sans cela, le scan automatique au demarrage
            // (souvent long, dizaines de secondes) declenche un sweep ARP
            // de surveillance qui ressemble a un second scan complet juste
            // apres la fin du premier.
            Log::d(TAG, "Surveillance continue : tick ignore (scan en cours)");
            _lastMonitorTickMs = nowMs;
        } else if (systemHealth.isDegraded()) {
            Log::w(TAG, "Surveillance continue refusee — mode degrade (%s)", systemHealth.reason().c_str());
            _lastMonitorTickMs = nowMs;   // Evite de boucler en continu en mode degrade
        } else {
            _monitorTick();
        }
    }

    // Drainage de la file d'attente differee — une seule entree par appel
    _drainPendingScans();
}

void NetworkScanner::setMonitorInterval(int minutes) {
    if (minutes < MONITOR_INTERVAL_MIN_MINUTES) minutes = MONITOR_INTERVAL_MIN_MINUTES;
    if (minutes > MONITOR_INTERVAL_MAX_MINUTES) minutes = MONITOR_INTERVAL_MAX_MINUTES;
    _monitorIntervalMinutes = (uint32_t)minutes;

    Preferences prefs;
    prefs.begin(MONITOR_NVS_NAMESPACE, false);
    prefs.putUChar(MONITOR_NVS_KEY, (uint8_t)minutes);
    prefs.end();
    Log::i(TAG, "Frequence de surveillance continue : %d min", minutes);
}

int NetworkScanner::getMonitorInterval() const {
    return (int)_monitorIntervalMinutes;
}

void NetworkScanner::setMonitorEnabled(bool enabled) {
    _monitorEnabled = enabled;

    Preferences prefs;
    prefs.begin(MONITOR_NVS_NAMESPACE, false);
    prefs.putBool(MONITOR_NVS_ENABLED_KEY, enabled);
    prefs.end();
    Log::i(TAG, "Surveillance continue : %s", enabled ? "activee" : "desactivee");
}

bool NetworkScanner::getMonitorEnabled() const {
    return _monitorEnabled;
}

bool NetworkScanner::setMobility(const String& macOrIp, const String& mode) {
    if (systemHealth.isDegraded()) {
        Log::w(TAG, "Modification de mobilite refusee — mode degrade (%s)", systemHealth.reason().c_str());
        return false;
    }
    if (mode != "" && mode != "fixed" && mode != "mobile") return false;

    bool found = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto& d : _results) {
        if ((!d.mac.isEmpty() && d.mac == macOrIp) || d.ip == macOrIp) {
            d.mobilityOverride = mode;
            found = true;
            break;
        }
    }
    xSemaphoreGive(_mutex);
    if (found) _saveToStore();
    return found;
}

// ---------------------------------------------------------------------------
// Donnees de sante reseau pour le tableau de bord — calcule les compteurs
// des 24 dernieres heures depuis l'historique, et identifie les equipements
// les moins stables (5 equipements fixes au score le plus bas, hors mobiles
// jamais penalises). Choix documente : on retient les 5 scores les plus bas
// sans seuil minimum, meme si tous sont >=90 — utile pour suivre la tendance
// meme sur un reseau globalement sain.
// ---------------------------------------------------------------------------
String NetworkScanner::networkHealthToJson() const {
    std::vector<NetworkDevice> copy;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    copy = _results;
    xSemaphoreGive(_mutex);

    int presentCount = 0;
    int knownCount    = 0;
    for (const auto& d : copy) {
        if (d.ip.isEmpty() && d.mac.isEmpty()) continue;
        knownCount++;
        if (d.online) presentCount++;
    }

    // Compteurs 24h depuis l'historique
    uint32_t nowEpoch = timeSync.nowEpoch();
    uint32_t dayAgo    = (nowEpoch > 86400) ? (nowEpoch - 86400) : 0;
    int newDevices24h     = 0;
    int reconnections24h  = 0;
    int unstableDevices24h = 0;
    if (nowEpoch > 0) {
        auto entries = deviceHistory.load(0);
        for (const auto& e : entries) {
            if (e.epoch == 0 || e.epoch < dayAgo) continue;
            if (e.event == "new")             newDevices24h++;
            else if (e.event == "reconnected") reconnections24h++;
            // "offline" (scan complet) et "disappeared" (surveillance continue)
            // designent le meme type de transition presence->absence
            else if (e.event == "disappeared" || e.event == "offline") unstableDevices24h++;
        }
    }

    // Equipements les moins stables (fixes uniquement — mobiles jamais penalises)
    struct ScoredDevice { String label; int score; };
    std::vector<ScoredDevice> scored;
    for (const auto& d : copy) {
        if (d.ip.isEmpty() && d.mac.isEmpty()) continue;
        int score = _stabilityScoreFor(d);
        if (score < 0) continue;   // Mobile — exclu du classement
        String label = !d.alias.isEmpty() ? d.alias : (!d.hostname.isEmpty() ? d.hostname : d.ip);
        scored.push_back({ label, score });
    }
    std::sort(scored.begin(), scored.end(), [](const ScoredDevice& a, const ScoredDevice& b) {
        return a.score < b.score;
    });
    constexpr size_t LEAST_STABLE_COUNT = 5;

    JsonDocument doc;
    doc["presentCount"] = presentCount;
    doc["knownCount"]   = knownCount;
    JsonObject last24h = doc["last24h"].to<JsonObject>();
    last24h["newDevices"]      = newDevices24h;
    last24h["reconnections"]   = reconnections24h;
    last24h["unstableDevices"] = unstableDevices24h;
    JsonArray leastStable = doc["leastStable"].to<JsonArray>();
    for (size_t i = 0; i < scored.size() && i < LEAST_STABLE_COUNT; i++) {
        JsonObject o = leastStable.add<JsonObject>();
        o["label"] = scored[i].label;
        o["score"] = scored[i].score;
    }

    String json;
    serializeJson(doc, json);
    return json;
}
