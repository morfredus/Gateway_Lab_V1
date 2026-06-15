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
// Sweep ARP du sous-réseau
//
// etharp_request() envoie un ARP Request broadcast (la bonne primitive pour
// découvrir les équipements, contrairement au sweep UDP qui ne déclenche pas
// de réponse ARP directe).
//
// Stratégie par lots :
//   - BATCH_SIZE requêtes ARP, puis attente BATCH_DELAY_MS pour les réponses
//   - Lecture de la table ARP après chaque lot avant que la table (10 entrées)
//     ne soit écrasée par le lot suivant
//   - hostnameResolver.update() appelé après chaque lecture pour accumuler
//     les annonces mDNS passives pendant le sweep
// ---------------------------------------------------------------------------
void NetworkScanner::_sweepSubnet() {
    IPAddress local = WiFi.localIP();
    IPAddress mask  = WiFi.subnetMask();

    uint8_t net[4], bcast[4];
    for (int i = 0; i < 4; i++) {
        net[i]   = local[i] & mask[i];
        bcast[i] = net[i] | (~mask[i] & 0xFF);
    }

    int h_start = (net[3]   == 0)   ? 1   : (int)net[3]   + 1;
    int h_end   = (bcast[3] == 255) ? 254 : (int)bcast[3] - 1;

    constexpr int BATCH_SIZE     = 5;
    constexpr int BATCH_DELAY_MS = 100;

    Log::i(TAG, "Sweep ARP %d.%d.%d.%d – .%d (lots de %d, %d ms/lot)",
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
            hostnameResolver.update();   // Collecter les annonces mDNS reçues
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
// Tâche FreeRTOS — exécutée sur Core 0 (même core que lwIP / TCP stack)
// Stack 16 Ko : nécessaire pour les appels lwIP + résolution DNS + std::map
// ---------------------------------------------------------------------------
void NetworkScanner::_task(void* self) {
    static_cast<NetworkScanner*>(self)->_run();
    vTaskDelete(nullptr);
}

void NetworkScanner::_run() {
    // Démarrer l'écoute mDNS passive avant le sweep (capture les annonces
    // émises par les équipements qui deviennent actifs sur le réseau)
    hostnameResolver.clearCaches();
    hostnameResolver.begin();

    _sweepSubnet();

    // Dernière lecture ARP après la fin du sweep
    vTaskDelay(pdMS_TO_TICKS(200));
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _readArpTable();
    xSemaphoreGive(_mutex);
    hostnameResolver.update();   // Traiter les derniers paquets mDNS

    Log::i(TAG, "Sweep ARP terminé — %u équipement(s) détecté(s)", (unsigned)_results.size());

    // Résolution des hostnames + détection ISP
    _resolveHostnames();

    // Arrêter l'écoute mDNS (libère le socket UDP multicast)
    hostnameResolver.end();

    // Ajouter l'ESP32 lui-même (non détectable par ARP)
    _addSelfEntry();

    Log::i(TAG, "Scan complet — %u équipement(s) enrichi(s)", (unsigned)_results.size());
    _scanning   = false;
    _taskHandle = nullptr;
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
    // Stack 16 Ko : lwIP + résolution DNS + std::map + buffers de paquets
    xTaskCreatePinnedToCore(_task, "net_scan", 16384, this, 1, &_taskHandle, 0);
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
    xSemaphoreTake(_mutex, portMAX_DELAY);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& d : _results) {
        JsonObject obj = arr.add<JsonObject>();
        obj["ip"]           = d.ip;
        obj["mac"]          = d.mac;
        obj["manufacturer"] = d.manufacturer;
        obj["hostname"]     = d.hostname;
        obj["category"]     = d.category;   // remplace "type" (v0.0.7)
        obj["model"]        = d.model;
        obj["os"]           = d.os;
        obj["source"]       = d.source;
        obj["elapsedMs"]    = now - d.lastSeen;
        obj["online"]       = d.online;
    }
    xSemaphoreGive(_mutex);
    String json;
    serializeJson(doc, json);
    return json;
}
