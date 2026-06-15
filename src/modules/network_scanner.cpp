/**
 * NetworkScanner — Implémentation
 *
 * Bibliothèques utilisées :
 *   WiFiUdp       — envoi de paquets UDP pour déclencher la résolution ARP
 *   lwip/etharp.h — accès à la table ARP du stack réseau lwIP
 *   lwip/netif.h  — accès à l'interface réseau active
 *   FreeRTOS      — tâche asynchrone pour ne pas bloquer l'interface web
 */

#include "network_scanner.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "lwip/etharp.h"   // etharp_get_entry() — lecture de la table ARP
#include "lwip/netif.h"    // netif_default       — interface réseau active
#include "../utils/logger.h"

static const char* TAG = "Scanner";

// Instance globale exportée
NetworkScanner netScanner;

// ---------------------------------------------------------------------------
// Table de correspondance OUI → Fabricant
// Les 3 premiers octets d'une adresse MAC identifient le fabricant (norme IEEE)
// Table limitée aux équipements couramment présents sur un réseau domestique
// ---------------------------------------------------------------------------
struct OuiEntry { const char* oui; const char* vendor; };
static const OuiEntry OUI_TABLE[] = {
    // Raspberry Pi Foundation
    {"B8:27:EB", "Raspberry Pi"}, {"DC:A6:32", "Raspberry Pi"},
    {"E4:5F:01", "Raspberry Pi"}, {"D8:3A:DD", "Raspberry Pi"},
    // Espressif Systems (ESP8266, ESP32...)
    {"18:FE:34", "Espressif"},    {"24:6F:28", "Espressif"},
    {"A4:CF:12", "Espressif"},    {"30:AE:A4", "Espressif"},
    {"AC:67:B2", "Espressif"},    {"3C:61:05", "Espressif"},
    {"FC:F5:C4", "Espressif"},    {"10:06:1C", "Espressif"},
    {"50:FF:20", "Espressif"},
    // Philips Hue (ampoules connectées)
    {"00:17:88", "Philips Hue"},  {"EC:B5:FA", "Philips Hue"},
    // Apple (iPhone, Mac, iPad...)
    {"28:CF:E9", "Apple"},        {"AC:BC:32", "Apple"},
    {"F0:18:98", "Apple"},        {"DC:56:E7", "Apple"},
    {"3C:22:FB", "Apple"},        {"00:25:00", "Apple"},
    {"00:1B:63", "Apple"},        {"00:16:CB", "Apple"},
    // Intel (cartes réseau PC/laptop)
    {"8C:8D:28", "Intel"},        {"00:1A:7D", "Intel"},
    // Amazon (Echo, Fire TV, Kindle...)
    {"18:31:BF", "Amazon"},       {"FC:A1:83", "Amazon"},
    {"74:75:48", "Amazon"},       {"44:65:0D", "Amazon"},
    // Samsung (téléphones, TV connectées...)
    {"34:D2:70", "Samsung"},      {"78:F5:FD", "Samsung"},
    // TP-Link (routeurs, prises connectées...)
    {"A8:9C:ED", "TP-Link"},      {"50:C7:BF", "TP-Link"},
    {"98:DA:C4", "TP-Link"},      {"C0:C9:E3", "TP-Link"},
    {"B0:4E:26", "TP-Link"},
    // Freebox (opérateur Free, France)
    {"F8:1A:67", "Freebox"},      {"00:24:D4", "Freebox"},
    // Microsoft (Surface, Xbox...)
    {"00:50:F2", "Microsoft"},
    // Google (Chromecast, Nest, Home...)
    {"7C:D1:C3", "Google"},       {"F4:F5:D8", "Google"},
    {"54:60:09", "Google"},
};

// Recherche du fabricant à partir des 3 premiers octets de l'adresse MAC
static String lookupVendor(const String& mac) {
    if (mac.length() < 8) return "";
    String prefix = mac.substring(0, 8);  // ex: "B8:27:EB"
    prefix.toUpperCase();
    for (const auto& e : OUI_TABLE) {
        if (prefix == e.oui) return e.vendor;
    }
    return "";  // Fabricant inconnu
}

// ---------------------------------------------------------------------------
// Lecture de la table ARP lwIP
//
// La table ARP (Address Resolution Protocol) associe chaque adresse IP
// à une adresse MAC. Elle est peuplée automatiquement par lwIP quand
// l'ESP32 communique avec un équipement ou reçoit une réponse ARP.
// Taille max : ARP_TABLE_SIZE entrées (10 par défaut sur ESP32).
// ---------------------------------------------------------------------------
void NetworkScanner::_readArpTable() {
    ip4_addr_t*      ip_ptr;    // Pointeur vers l'adresse IP de l'entrée
    struct netif*    netif_ptr; // Pointeur vers l'interface réseau concernée
    struct eth_addr* eth_ptr;   // Pointeur vers l'adresse MAC de l'entrée

    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        // Lecture de l'entrée i de la table ARP (retourne false si vide)
        if (!etharp_get_entry(i, &ip_ptr, &netif_ptr, &eth_ptr)) continue;

        // Conversion de l'adresse IP en chaîne lisible (ex: "192.168.1.5")
        char ipStr[16];
        ip4addr_ntoa_r(ip_ptr, ipStr, sizeof(ipStr));

        // Formatage de l'adresse MAC en notation standard (ex: "B8:27:EB:AA:BB:CC")
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
            eth_ptr->addr[0], eth_ptr->addr[1], eth_ptr->addr[2],
            eth_ptr->addr[3], eth_ptr->addr[4], eth_ptr->addr[5]);

        // Déduplication : si le MAC existe déjà, on met à jour l'IP et l'horodatage
        bool found = false;
        for (auto& d : _results) {
            if (d.mac == macStr) {
                d.ip = ipStr;
                d.lastSeenMs = millis();
                found = true;
                break;
            }
        }
        // Nouvel équipement découvert : ajout à la liste
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
// Sweep UDP du sous-réseau
//
// Principe : envoyer un paquet UDP à chaque IP du sous-réseau déclenche
// une résolution ARP par lwIP (l'ESP32 doit connaître le MAC avant d'envoyer).
// Le port 9 (discard) est utilisé — les paquets sont ignorés par les destinataires,
// mais la résolution ARP a bien lieu côté lwIP.
//
// Calcul de la plage : octet par octet pour éviter les problèmes de byte-order
// entre IPAddress Arduino et les structures lwIP.
// ---------------------------------------------------------------------------
void NetworkScanner::_sweepSubnet() {
    IPAddress local = WiFi.localIP();    // ex: 192.168.1.42
    IPAddress mask  = WiFi.subnetMask(); // ex: 255.255.255.0

    // Calcul de l'adresse de réseau et de diffusion (broadcast)
    uint8_t net[4], bcast[4];
    for (int i = 0; i < 4; i++) {
        net[i]   = local[i] & mask[i];           // ex: 192.168.1.0
        bcast[i] = net[i] | (~mask[i] & 0xFF);   // ex: 192.168.1.255
    }

    // Plage du dernier octet à sonder (limité au /24 pour les sous-réseaux larges)
    int last_start = (net[3] == 0)     ? 1   : net[3];
    int last_end   = (bcast[3] == 255) ? 254 : bcast[3];

    Log::i(TAG, "Sweep %d.%d.%d.%d – %d.%d.%d.%d",
           net[0], net[1], net[2], last_start,
           net[0], net[1], net[2], last_end);

    WiFiUDP udp;
    for (int h = last_start; h <= last_end; h++) {
        IPAddress target(net[0], net[1], net[2], h);
        if (target == local) continue;   // Ignorer sa propre IP

        // Envoi d'un paquet UDP vide → lwIP va résoudre l'ARP avant d'envoyer
        udp.beginPacket(target, 9);
        udp.write((uint8_t)0);
        udp.endPacket();

        // Lecture de la table ARP tous les 16 hôtes.
        // La table lwIP ne fait que 10 entrées : sans lecture régulière,
        // les premières réponses ARP seraient écrasées par les suivantes.
        if ((h - last_start) % 16 == 15) {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _readArpTable();
            xSemaphoreGive(_mutex);
            vTaskDelay(pdMS_TO_TICKS(15));  // Laisser lwIP traiter les réponses
        }
    }
}

// ---------------------------------------------------------------------------
// Tâche FreeRTOS — exécutée sur le Core 0 (même core que lwIP)
// ---------------------------------------------------------------------------
void NetworkScanner::_task(void* self) {
    static_cast<NetworkScanner*>(self)->_run();
    vTaskDelete(nullptr);   // Suppression de la tâche une fois terminée
}

void NetworkScanner::_run() {
    _sweepSubnet();

    // Attente finale pour capturer les dernières réponses ARP
    // (certains équipements répondent lentement)
    vTaskDelay(pdMS_TO_TICKS(600));

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _readArpTable();   // Dernière lecture pour ne rien manquer
    xSemaphoreGive(_mutex);

    Log::i(TAG, "Scan terminé — %u équipement(s) détecté(s)", (unsigned)_results.size());
    _scanning   = false;
    _taskHandle = nullptr;
}

// ---------------------------------------------------------------------------
// API publique
// ---------------------------------------------------------------------------
void NetworkScanner::begin() {
    // Création du mutex de protection (obligatoire avant tout accès à _results)
    _mutex = xSemaphoreCreateMutex();
    Log::i(TAG, "Module initialisé");
}

void NetworkScanner::startScan() {
    if (_scanning) {
        Log::w(TAG, "Scan déjà en cours — ignoré");
        return;
    }
    _scanning = true;
    // Création de la tâche sur Core 0 (8 Ko de stack — nécessaire pour les appels lwIP)
    xTaskCreatePinnedToCore(_task, "net_scan", 8192, this, 1, &_taskHandle, 0);
    Log::i(TAG, "Scan réseau lancé");
}

bool NetworkScanner::isScanRunning() const { return _scanning; }

std::vector<HostInfo> NetworkScanner::getResults() const {
    // Copie protégée : le scan peut modifier _results depuis Core 0
    // pendant que le serveur web lit depuis Core 1
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
