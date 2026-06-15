/**
 * NetworkScanner — Découverte des équipements connectés au réseau local
 *
 * Fonctionnement du scan :
 *   1. Envoi d'un paquet UDP vide sur le port 9 (discard) à chaque adresse
 *      IP du sous-réseau → déclenche la résolution ARP par lwIP
 *   2. Lecture de la table ARP lwIP toutes les 16 sondes pour capturer
 *      les réponses avant que la table (10 entrées max) ne soit écrasée
 *   3. Déduplication par adresse MAC — un équipement garde son entrée
 *      même s'il change d'IP entre deux scans
 *   4. Identification du fabricant via les 3 premiers octets MAC (OUI)
 *
 * Le scan s'exécute dans une tâche FreeRTOS sur le Core 0 (même core que
 * le stack TCP/IP) pour ne pas bloquer l'interface web pendant l'opération.
 *
 * Durée typique sur un réseau /24 : 5 à 15 secondes selon le nombre d'hôtes.
 */

#pragma once
#include <Arduino.h>
#include <vector>

// Informations collectées pour chaque équipement découvert
struct HostInfo {
    String   ip;           // Adresse IPv4 (ex: "192.168.1.10")
    String   mac;          // Adresse MAC (ex: "B8:27:EB:AA:BB:CC")
    String   vendor;       // Fabricant déduit du MAC (ex: "Raspberry Pi")
    String   hostname;     // Nom DNS résolu (ex: "mon-pc.local"), vide si inconnu
    uint32_t lastSeenMs;   // Horodatage de la dernière détection (millis())
};

class NetworkScanner {
public:
    // Initialisation du mutex de protection et de la liste des résultats
    void begin();

    // Lancement du scan asynchrone (tâche FreeRTOS)
    // Sans effet si un scan est déjà en cours
    void startScan();

    // État du scan : true pendant l'exécution de la tâche FreeRTOS
    bool isScanRunning() const;

    // Copie thread-safe des résultats (protégée par mutex)
    std::vector<HostInfo> getResults() const;

    // Sérialisation JSON des résultats pour l'API web /api/devices
    String resultsToJson() const;

private:
    // Point d'entrée de la tâche FreeRTOS (signature imposée par xTaskCreate)
    static void _task(void* self);

    // Corps du scan : sweep + résolution hostnames + lecture ARP finale
    void _run();

    // Envoi de requêtes ARP natives sur tout le sous-réseau
    void _sweepSubnet();

    // Résolution DNS inverse (PTR) pour chaque équipement découvert
    void _resolveHostnames();

    // Lecture et intégration des entrées de la table ARP lwIP
    void _readArpTable();

    SemaphoreHandle_t      _mutex      = nullptr;  // Protection concurrente de _results
    TaskHandle_t           _taskHandle = nullptr;  // Handle de la tâche FreeRTOS
    std::vector<HostInfo>  _results;               // Liste accumulée des équipements
    volatile bool          _scanning   = false;    // Indicateur d'état du scan
};

// Instance globale
extern NetworkScanner netScanner;
