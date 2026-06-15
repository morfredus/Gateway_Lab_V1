#include "network_scanner.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "../utils/logger.h"

static const char* TAG = "Scanner";

NetworkScanner netScanner;

// ---------------------------------------------------------------------------
// Table OUI minimale (premiers 3 octets MAC → fabricant)
// ---------------------------------------------------------------------------
struct OuiEntry { const char* oui; const char* vendor; };
static const OuiEntry OUI_TABLE[] = {
    {"B8:27:EB", "Raspberry Pi"}, {"DC:A6:32", "Raspberry Pi"},
    {"E4:5F:01", "Raspberry Pi"}, {"D8:3A:DD", "Raspberry Pi"},
    {"18:FE:34", "Espressif"},    {"24:6F:28", "Espressif"},
    {"A4:CF:12", "Espressif"},    {"30:AE:A4", "Espressif"},
    {"AC:67:B2", "Espressif"},    {"3C:61:05", "Espressif"},
    {"FC:F5:C4", "Espressif"},    {"10:06:1C", "Espressif"},
    {"00:17:88", "Philips Hue"},  {"EC:B5:FA", "Philips Hue"},
    {"28:CF:E9", "Apple"},        {"AC:BC:32", "Apple"},
    {"F0:18:98", "Apple"},        {"DC:56:E7", "Apple"},
    {"3C:22:FB", "Apple"},        {"00:25:00", "Apple"},
    {"8C:8D:28", "Intel"},        {"00:1A:7D", "Intel"},
    {"18:31:BF", "Amazon"},       {"FC:A1:83", "Amazon"},
    {"74:75:48", "Amazon"},       {"44:65:0D", "Amazon"},
    {"34:D2:70", "Samsung"},      {"78:F5:FD", "Samsung"},
    {"A8:9C:ED", "TP-Link"},      {"50:C7:BF", "TP-Link"},
    {"98:DA:C4", "TP-Link"},      {"C0:C9:E3", "TP-Link"},
    {"F8:1A:67", "Freebox"},      {"00:24:D4", "Freebox"},
    {"00:50:F2", "Microsoft"},    {"00:1B:63", "Apple"},
    {"00:16:CB", "Apple"},        {"7C:D1:C3", "Google"},
    {"F4:F5:D8", "Google"},       {"54:60:09", "Google"},
    {"B0:4E:26", "TP-Link"},      {"50:FF:20", "Espressif"},
};

static String lookupVendor(const String& mac) {
    if (mac.length() < 8) return "";
    String prefix = mac.substring(0, 8);
    prefix.toUpperCase();
    for (const auto& e : OUI_TABLE) {
        if (prefix == e.oui) return e.vendor;
    }
    return "";
}

// ---------------------------------------------------------------------------
// Lecture de la table ARP lwIP — doit être appelée depuis le thread principal
// ou protégée via tcpip_callback. Ici appelée depuis la task Core 0 (même
// core que le stack lwIP) donc sûr.
// ---------------------------------------------------------------------------
void NetworkScanner::_readArpTable() {
    ip4_addr_t*    ip_ptr;
    struct netif*  netif_ptr;
    struct eth_addr* eth_ptr;

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!etharp_get_entry(i, &ip_ptr, &netif_ptr, &eth_ptr)) continue;

        char ipStr[16];
        ip4addr_ntoa_r(ip_ptr, ipStr, sizeof(ipStr));

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
            eth_ptr->addr[0], eth_ptr->addr[1], eth_ptr->addr[2],
            eth_ptr->addr[3], eth_ptr->addr[4], eth_ptr->addr[5]);

        // Dédupliquer par MAC, mettre à jour si IP a changé
        bool found = false;
        for (auto& d : _results) {
            if (d.mac == macStr) {
                d.ip = ipStr;
                d.lastSeenMs = millis();
                found = true;
                break;
            }
        }
        if (!found) {
            HostInfo h;
            h.ip         = ipStr;
            h.mac        = macStr;
            h.vendor     = lookupVendor(macStr);
            h.lastSeenMs = millis();
            _results.push_back(h);
        }
    }
}

// ---------------------------------------------------------------------------
// Sweep UDP : envoie un paquet vide à chaque IP du sous-réseau
// Cela déclenche la résolution ARP et peuple la table lwIP
// ---------------------------------------------------------------------------
void NetworkScanner::_sweepSubnet() {
    IPAddress local = WiFi.localIP();
    IPAddress mask  = WiFi.subnetMask();

    // Déduire la base du réseau (octet par octet pour éviter les problèmes
    // de byte-order sur les différentes versions du SDK)
    uint8_t net[4], bcast[4];
    for (int i = 0; i < 4; i++) {
        net[i]   = local[i] & mask[i];
        bcast[i] = net[i] | (~mask[i] & 0xFF);
    }

    // Limiter au /24 même sur des sous-réseaux plus larges
    int last_start = (net[3] == 0)   ? 1   : net[3];
    int last_end   = (bcast[3] == 255) ? 254 : bcast[3];

    WiFiUDP udp;
    Log::i(TAG, "Sweep %d.%d.%d.%d–%d", net[0], net[1], net[2], last_start, last_end);

    for (int h = last_start; h <= last_end; h++) {
        IPAddress target(net[0], net[1], net[2], h);
        if (target == local) continue;

        udp.beginPacket(target, 9);  // port 9 = discard, non routable
        udp.write((uint8_t)0);
        udp.endPacket();

        // Lire la table ARP tous les 16 hôtes pour capturer les réponses rapides
        if ((h - last_start) % 16 == 15) {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _readArpTable();
            xSemaphoreGive(_mutex);
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}

// ---------------------------------------------------------------------------
// Tâche FreeRTOS (Core 0 = même core que le stack TCP/IP)
// ---------------------------------------------------------------------------
void NetworkScanner::_task(void* self) {
    static_cast<NetworkScanner*>(self)->_run();
    vTaskDelete(nullptr);
}

void NetworkScanner::_run() {
    _sweepSubnet();

    // Lecture finale après que les dernières réponses ARP soient arrivées
    vTaskDelay(pdMS_TO_TICKS(600));
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _readArpTable();
    xSemaphoreGive(_mutex);

    Log::i(TAG, "Scan terminé — %u équipements", (unsigned)_results.size());
    _scanning    = false;
    _taskHandle  = nullptr;
}

// ---------------------------------------------------------------------------
// API publique
// ---------------------------------------------------------------------------
void NetworkScanner::begin() {
    _mutex = xSemaphoreCreateMutex();
    Log::i(TAG, "Module initialisé");
}

void NetworkScanner::startScan() {
    if (_scanning) {
        Log::w(TAG, "Scan déjà en cours");
        return;
    }
    _scanning = true;
    xTaskCreatePinnedToCore(_task, "net_scan", 8192, this, 1, &_taskHandle, 0);
    Log::i(TAG, "Scan réseau lancé");
}

bool NetworkScanner::isScanRunning() const { return _scanning; }

std::vector<HostInfo> NetworkScanner::getResults() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    auto copy = _results;
    xSemaphoreGive(_mutex);
    return copy;
}

String NetworkScanner::resultsToJson() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    String json = "[";
    for (size_t i = 0; i < _results.size(); i++) {
        const auto& d = _results[i];
        if (i > 0) json += ',';
        json += "{\"ip\":\"" + d.ip + "\","
                "\"mac\":\"" + d.mac + "\","
                "\"vendor\":\"" + d.vendor + "\","
                "\"lastSeen\":" + String(d.lastSeenMs) + "}";
    }
    json += "]";
    xSemaphoreGive(_mutex);
    return json;
}
