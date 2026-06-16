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

// Recherche de l'entrée OUI à partir des 3 premiers octets de l'adresse MAC.
// Retourne nullptr si l'OUI n'est pas dans la table.
static const OuiEntry* lookupOui(const String& mac) {
    if (mac.length() < 8) return nullptr;
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
            const OuiEntry* oui = lookupOui(macStr);
            if (oui) {
                h.manufacturer = oui->manufacturer;
                h.category     = oui->category;   // "Router", "IoT", "SBC"…
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

    // Fabricant depuis l'OUI (Espressif Systems pour les ESP32)
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
            auto alive = icmpScanner.ping(missingIps, 200);
            if (!alive.empty()) {
                xSemaphoreTake(_mutex, portMAX_DELAY);
                for (const auto& ip : alive) {
                    bool exists = false;
                    for (const auto& d : _results) {
                        if (d.ip == ip) { exists = true; break; }
                    }
                    if (!exists) {
                        NetworkDevice d;
                        d.ip       = ip;
                        d.source   = "ICMP";
                        d.online   = true;
                        d.lastSeen = millis();
                        _results.push_back(d);
                        Log::i(TAG, "ICMP nouveau device : %s", ip.c_str());
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

    // Sauvegarder en LittleFS pour le prochain boot
    _saveToStore();

    Log::i(TAG, "Scan complet — %u équipement(s) total", (unsigned)_results.size());
    _scanning   = false;
    _taskHandle = nullptr;
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
    }
    String json;
    serializeJson(doc, json);
    return json;
}
