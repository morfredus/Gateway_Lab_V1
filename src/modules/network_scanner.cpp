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
#include "lwip/etharp.h"   // etharp_get_entry(), etharp_request() — table et requêtes ARP
#include "lwip/netif.h"    // netif_default — interface réseau active
#include "lwip/inet.h"     // inet_pton() — conversion IP texte → binaire
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
    {"50:FF:20", "Espressif"},    {"C8:F0:9E", "Espressif"},
    {"CC:50:E3", "Espressif"},    {"08:3A:F2", "Espressif"},
    // Philips Hue (ampoules connectées)
    {"00:17:88", "Philips Hue"},  {"EC:B5:FA", "Philips Hue"},
    // Apple (iPhone, Mac, iPad...)
    {"28:CF:E9", "Apple"},        {"AC:BC:32", "Apple"},
    {"F0:18:98", "Apple"},        {"DC:56:E7", "Apple"},
    {"3C:22:FB", "Apple"},        {"00:25:00", "Apple"},
    {"00:1B:63", "Apple"},        {"00:16:CB", "Apple"},
    {"A8:86:DD", "Apple"},        {"70:3E:AC", "Apple"},
    {"8C:85:90", "Apple"},        {"BC:D0:74", "Apple"},
    // Intel (cartes réseau PC/laptop)
    {"8C:8D:28", "Intel"},        {"00:1A:7D", "Intel"},
    {"00:1E:64", "Intel"},        {"74:E5:F9", "Intel"},
    // Amazon (Echo, Fire TV, Kindle...)
    {"18:31:BF", "Amazon"},       {"FC:A1:83", "Amazon"},
    {"74:75:48", "Amazon"},       {"44:65:0D", "Amazon"},
    {"F0:27:2D", "Amazon"},       {"40:B4:CD", "Amazon"},
    // Samsung (téléphones, TV connectées...)
    {"34:D2:70", "Samsung"},      {"78:F5:FD", "Samsung"},
    {"8C:77:12", "Samsung"},      {"CC:07:AB", "Samsung"},
    {"00:26:37", "Samsung"},      {"F4:9F:54", "Samsung"},
    // TP-Link (routeurs, prises connectées...)
    {"A8:9C:ED", "TP-Link"},      {"50:C7:BF", "TP-Link"},
    {"98:DA:C4", "TP-Link"},      {"C0:C9:E3", "TP-Link"},
    {"B0:4E:26", "TP-Link"},      {"54:AF:97", "TP-Link"},
    // Freebox (opérateur Free, France)
    {"F8:1A:67", "Freebox"},      {"00:24:D4", "Freebox"},
    {"68:A3:78", "Freebox"},
    // Microsoft (Surface, Xbox...)
    {"00:50:F2", "Microsoft"},    {"28:18:78", "Microsoft"},
    {"00:17:FA", "Microsoft"},
    // Google (Chromecast, Nest, Home...)
    {"7C:D1:C3", "Google"},       {"F4:F5:D8", "Google"},
    {"54:60:09", "Google"},       {"20:DF:B9", "Google"},
    {"48:D6:D5", "Google"},
    // Xiaomi (smartphones, objets connectés...)
    {"28:6C:07", "Xiaomi"},       {"F4:8B:32", "Xiaomi"},
    {"50:EC:50", "Xiaomi"},       {"64:09:80", "Xiaomi"},
    {"78:02:F8", "Xiaomi"},       {"18:59:36", "Xiaomi"},
    {"34:CE:00", "Xiaomi"},       {"AC:F7:F3", "Xiaomi"},
    // iRobot (aspirateurs Roomba...)
    {"50:14:79", "iRobot"},       {"68:37:E9", "iRobot"},
    {"00:50:C2", "iRobot"},
    // Roborock (aspirateurs robots...)
    {"78:2B:46", "Roborock"},     {"58:63:56", "Roborock"},
    // Brother (imprimantes, MFC...)
    {"00:1B:A9", "Brother"},      {"00:80:77", "Brother"},
    {"00:0C:00", "Brother"},      {"30:05:5C", "Brother"},
    // Sonos (enceintes connectées...)
    {"78:28:CA", "Sonos"},        {"5C:AA:FD", "Sonos"},
    {"94:9F:3E", "Sonos"},        {"B8:E9:37", "Sonos"},
    {"34:7E:5C", "Sonos"},
    // Ring (sonnettes, caméras...)
    {"B0:09:DA", "Ring"},         {"EC:1A:59", "Ring"},
    // Synology (NAS...)
    {"00:11:32", "Synology"},     {"BC:5F:F4", "Synology"},
    // QNAP (NAS...)
    {"24:5E:BE", "QNAP"},         {"00:08:9B", "QNAP"},
    // Roku (TV stick, box streaming...)
    {"B8:3E:59", "Roku"},         {"D0:4D:2C", "Roku"},
    {"CC:6D:A0", "Roku"},         {"08:05:81", "Roku"},
    // Nvidia (Shield, cartes graphiques réseau...)
    {"00:04:4B", "Nvidia"},
    // Livebox / Orange (opérateur France)
    {"F4:CA:E5", "Orange"},       {"00:90:D0", "Orange"},
    {"10:65:30", "Orange"},
    // SFR (opérateur France)
    {"00:22:A0", "SFR"},          {"84:9D:C7", "SFR"},
    // Netgear (routeurs, switches...)
    {"00:09:5B", "Netgear"},      {"20:E5:2A", "Netgear"},
    {"28:C6:8E", "Netgear"},      {"A0:40:A0", "Netgear"},
    // Asus (routeurs, PC...)
    {"00:0C:6E", "Asus"},         {"04:92:26", "Asus"},
    {"10:BF:48", "Asus"},         {"2C:FD:A1", "Asus"},
};

// Recherche du fabricant à partir des 3 premiers octets de l'adresse MAC (OUI)
static String lookupManufacturer(const String& mac) {
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
                d.ip       = ipStr;
                d.lastSeen = millis();
                d.online   = true;
                found = true;
                break;
            }
        }
        // Nouvel équipement découvert : ajout à la liste
        if (!found) {
            NetworkDevice h;
            h.ip           = ipStr;
            h.mac          = macStr;
            h.manufacturer = lookupManufacturer(macStr);
            h.lastSeen     = millis();
            h.online       = true;
            _results.push_back(h);
        }
    }
}

// ---------------------------------------------------------------------------
// Sweep ARP du sous-réseau
//
// Pourquoi etharp_request() plutôt que UDP ?
//   - Le sweep UDP envoyait des paquets sans attendre la réponse ARP.
//     lwIP résout l'ARP de façon asynchrone : la réponse arrivait souvent
//     APRÈS qu'on ait déjà avancé dans la boucle et réécrasé la table.
//   - etharp_request() envoie directement un ARP Request (broadcast),
//     qui est la bonne primitive pour découvrir des équipements.
//
// Stratégie par lots :
//   - On envoie BATCH_SIZE requêtes ARP, puis on attend BATCH_DELAY_MS
//     pour laisser le temps aux équipements de répondre.
//   - On lit la table ARP après chaque lot et on accumule dans _results.
//   - La table lwIP ne fait que ARP_TABLE_SIZE entrées (10) :
//     lire fréquemment évite de perdre des réponses entre deux lectures.
//
// Durée typique sur /24 : ~5 s (254 hôtes / 5 par lot × 100 ms)
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

    // Nombre de requêtes ARP envoyées avant de lire la table et d'attendre
    // Valeur faible = plus de précision, scan plus lent
    // Valeur élevée = scan plus rapide, risque de manquer des équipements lents
    constexpr int BATCH_SIZE     = 5;
    constexpr int BATCH_DELAY_MS = 100;

    Log::i(TAG, "Scan ARP %d.%d.%d.%d – .%d (lots de %d, %d ms)",
           net[0], net[1], net[2], h_start, h_end, BATCH_SIZE, BATCH_DELAY_MS);

    int inBatch = 0;
    for (int h = h_start; h <= h_end; h++) {
        IPAddress target(net[0], net[1], net[2], h);
        if (target == local) continue;

        // Envoi d'un ARP Request broadcast via lwIP
        // L'équipement cible répondra avec son adresse MAC → peuplera la table ARP
        ip4_addr_t ip4target;
        IP4_ADDR(&ip4target, net[0], net[1], net[2], (uint8_t)h);
        etharp_request(netif_default, &ip4target);
        inBatch++;

        if (inBatch >= BATCH_SIZE) {
            // Attente des réponses ARP (la plupart arrivent en < 20 ms sur LAN)
            vTaskDelay(pdMS_TO_TICKS(BATCH_DELAY_MS));
            // Capture des entrées ARP avant qu'elles soient écrasées par le lot suivant
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _readArpTable();
            xSemaphoreGive(_mutex);
            inBatch = 0;
        }
    }

    // Traiter le dernier lot incomplet
    if (inBatch > 0) {
        vTaskDelay(pdMS_TO_TICKS(BATCH_DELAY_MS));
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _readArpTable();
        xSemaphoreGive(_mutex);
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

    // Dernière lecture ARP après le dernier lot
    vTaskDelay(pdMS_TO_TICKS(200));
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _readArpTable();
    xSemaphoreGive(_mutex);

    Log::i(TAG, "Scan terminé — %u équipement(s) détecté(s)", (unsigned)_results.size());
    _scanning   = false;
    _taskHandle = nullptr;
}

// ---------------------------------------------------------------------------
// Résolution DNS inverse (PTR) — non disponible sur lwIP ESP32
//
// gethostbyaddr() n'est pas compilé dans le lwIP fourni par le framework
// Arduino ESP32. La colonne "Nom" affiche "—" pour les équipements sans
// entrée mDNS visible. Une alternative (requête PTR manuelle via netdb ou
// mDNS) est documentée dans ROADMAP.md.
// ---------------------------------------------------------------------------
void NetworkScanner::_resolveHostnames() {
    // no-op : gethostbyaddr non disponible sur cette plateforme
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

std::vector<NetworkDevice> NetworkScanner::getResults() const {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    auto copy = _results;
    xSemaphoreGive(_mutex);
    return copy;
}

String NetworkScanner::resultsToJson() const {
    // lastSeen est envoyé comme durée écoulée (ms) — le navigateur n'a pas
    // à connaître l'epoch ESP32, il peut afficher directement "il y a Xs"
    uint32_t now = millis();
    xSemaphoreTake(_mutex, portMAX_DELAY);
    String json = "[";
    for (size_t i = 0; i < _results.size(); i++) {
        const auto& d = _results[i];
        if (i > 0) json += ',';
        json += "{\"ip\":\"" + d.ip + "\","
                "\"mac\":\"" + d.mac + "\","
                "\"manufacturer\":\"" + d.manufacturer + "\","
                "\"hostname\":\"" + d.hostname + "\","
                "\"type\":\"" + d.type + "\","
                "\"elapsedMs\":" + String(now - d.lastSeen) + ","
                "\"online\":" + (d.online ? "true" : "false") + "}";
    }
    json += "]";
    xSemaphoreGive(_mutex);
    return json;
}
