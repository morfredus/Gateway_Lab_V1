/**
 * NetworkScanner — Découverte des équipements connectés au réseau local
 *
 * Fonctionnement du scan (v0.0.7) :
 *   1. Envoi de requêtes ARP natives (etharp_request) sur tout le sous-réseau
 *   2. Lecture de la table ARP lwIP par lots pour capturer les réponses
 *      avant que la table (10 entrées max) ne soit écrasée
 *   3. Écoute mDNS multicast passive pendant le sweep (HostnameResolver)
 *   4. Déduplication par adresse MAC — un équipement garde son entrée
 *      même s'il change d'IP entre deux scans
 *   5. Identification du fabricant via les 3 premiers octets MAC (OUI)
 *   6. Résolution des noms d'hôtes par requêtes PTR DNS batch
 *   7. Détection des boxes FAI françaises (Free, Orange, SFR, Bouygues)
 *
 * Le scan s'exécute dans une tâche FreeRTOS sur le Core 0 (même core que
 * le stack TCP/IP) pour ne pas bloquer l'interface web pendant l'opération.
 *
 * Durée typique sur un réseau /24 : 5 à 20 secondes selon le nombre d'hôtes
 * et la disponibilité du DNS (PTR queries ajoutent ≤ 500 ms au total).
 */

#pragma once
#include <Arduino.h>
#include <vector>

// ---------------------------------------------------------------------------
// Informations collectées pour chaque équipement découvert
//
// Champs v0.0.7 :
//   manufacturer  — Fabricant (OUI ou ISP override, ex: "Orange", "Free")
//   hostname      — Meilleur nom disponible (mDNS > PTR DNS), ex: "raspberrypi"
//   category      — Catégorie d'équipement (ex: "Router", "IoT", "Mobile")
//   model         — Modèle précis si détecté (ex: "Livebox 6", "Freebox Pop")
//   os            — Système d'exploitation (usage futur — vide en v0.0.7)
//   source        — Méthode de résolution : "mDNS", "PTR", "MAC", ou ""
// ---------------------------------------------------------------------------
struct NetworkDevice {
    String   ip;            // Adresse IPv4 (ex: "192.168.1.10")
    String   mac;           // Adresse MAC  (ex: "B8:27:EB:AA:BB:CC")

    String   manufacturer;  // Fabricant déduit du MAC OUI ou de la détection FAI
    String   hostname;      // Nom résolu (ex: "mon-pc", "livebox") — vide si inconnu

    String   category;      // Type d'équipement : "Router", "IoT", "Mobile", "SBC"…
    String   model;         // Modèle détaillé si disponible (ex: "Freebox Ultra", "")
    String   os;            // Système d'exploitation — usage futur, vide en v0.0.7
    String   source;        // Source de résolution : "mDNS" | "PTR" | "MAC" | ""

    uint32_t lastSeen;      // millis() du dernier scan — converti en elapsed côté client
    bool     online;        // true si détecté lors du dernier scan
};

class NetworkScanner {
public:
    // Initialisation du mutex de protection (idempotente — guard sur _mutex)
    void begin();

    // Lancement du scan asynchrone (tâche FreeRTOS)
    // Sans effet si un scan est déjà en cours
    void startScan();

    // État du scan : true pendant l'exécution de la tâche FreeRTOS
    bool isScanRunning() const;

    // Copie thread-safe des résultats (protégée par mutex)
    std::vector<NetworkDevice> getResults() const;

    // Sérialisation JSON des résultats pour l'API /api/devices
    String resultsToJson() const;

private:
    // Point d'entrée de la tâche FreeRTOS (signature imposée par xTaskCreate)
    static void _task(void* self);

    // Corps du scan complet : sweep ARP + lecture finale + résolution hostnames
    void _run();

    // Envoi de requêtes ARP natives sur tout le sous-réseau (lots de BATCH_SIZE)
    void _sweepSubnet();

    // Lecture et intégration des entrées de la table ARP lwIP dans _results
    void _readArpTable();

    // Résolution des noms d'hôtes et détection ISP pour tous les équipements online
    // Appelle HostnameResolver (mDNS cache + PTR DNS batch) puis IspDetector
    void _resolveHostnames();

    SemaphoreHandle_t           _mutex      = nullptr;
    TaskHandle_t                _taskHandle = nullptr;
    std::vector<NetworkDevice>  _results;
    volatile bool               _scanning   = false;
};

// Instance globale
extern NetworkScanner netScanner;
