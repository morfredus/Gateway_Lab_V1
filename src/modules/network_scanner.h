/**
 * NetworkScanner — Découverte des équipements connectés au réseau local
 *
 * Fonctionnement du scan (v0.1.0) :
 *   1. Chargement des devices connus depuis LittleFS (DeviceStore)
 *   2. ARP sweep en 3 passes :
 *      - Passe 1 : sweep complet par lots de 5
 *      - Passe 2 : re-sonde les IP non trouvées
 *      - Passe 3 : lecture finale après 500 ms
 *   3. ICMP sweep sur les IP manquantes (complément ARP)
 *   4. Écoute mDNS passive pendant le sweep (HostnameResolver)
 *   5. Résolution des noms par PTR DNS batch
 *   6. Détection boxes FAI (Free, Orange, SFR, Bouygues)
 *   7. SSDP/UPnP — descripteur XML + APIs Hue/Synology/Freebox
 *   8. DNS-SD — 22 types de services (HTTP, SSH, AirPlay…)
 *   9. Sauvegarde dans LittleFS
 *
 * Le scan s'exécute dans une tâche FreeRTOS sur le Core 0 (même core que
 * le stack TCP/IP) pour ne pas bloquer l'interface web pendant l'opération.
 */

#pragma once
#include <Arduino.h>
#include <vector>

// Statistiques du scan : équipements connus / en ligne / hors ligne
struct ScanStats {
    int known   = 0;  // Total dans _results (online + offline)
    int online  = 0;  // Vus lors du dernier scan
    int offline = 0;  // Connus mais absents lors du dernier scan
};

// ---------------------------------------------------------------------------
// Informations collectées pour chaque équipement découvert
// ---------------------------------------------------------------------------
struct NetworkDevice {
    String   ip;            // Adresse IPv4 (ex: "192.168.1.10")
    String   mac;           // Adresse MAC  (ex: "B8:27:EB:AA:BB:CC")

    String   manufacturer;  // Fabricant déduit du MAC OUI ou de la détection FAI
    String   hostname;      // Nom résolu (ex: "mon-pc", "livebox") — vide si inconnu

    String   category;      // Type d'équipement : "Router", "IoT", "Mobile", "SBC"…
    String   model;         // Modèle détaillé si disponible (ex: "Freebox Ultra", "")
    String   os;            // Système d'exploitation — usage futur, vide en v0.0.7
    String   source;        // Source de résolution : "mDNS" | "PTR" | "MAC" | "Self" | ""
    String   services;      // Services DNS-SD détectés, séparés par '|' (ex: "HTTP|SSH|SMB")
    String   openPorts;    // Ports TCP ouverts, séparés par '|' (ex: "80|443|22")

    String   alias;         // Nom personnalise par l'utilisateur - prioritaire sur hostname a l'affichage
    uint32_t firstSeenEpoch = 0;   // Epoch NTP de la premiere detection (0 = inconnu, pas d'heure synchronisee)
    uint32_t lastSeenEpoch  = 0;   // Epoch NTP de la derniere detection (0 = inconnu)
    uint32_t seenCount      = 0;   // Nombre de scans ou l'equipement a ete vu en ligne

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

    // Statistiques pour l'UI (connus / en ligne / hors ligne)
    ScanStats getStats() const;

    // Definit l'alias utilisateur d'un equipement identifie par MAC ou IP
    // Retourne false si aucun equipement correspondant n'a ete trouve
    bool setAlias(const String& macOrIp, const String& alias);

    // Sauvegarde JSON complete (devices + parametres) pour /api/backup
    String backupToJson() const;

    // Restauration depuis un JSON produit par backupToJson() - remplace les resultats actuels
    bool restoreFromJson(const String& json);

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

    // Injecte l'ESP32 lui-même dans _results
    // L'ARP ne peut pas découvrir sa propre adresse — on l'ajoute manuellement
    void _addSelfEntry();

    // Fusionne les résultats du scan SSDP/UPnP dans _results
    // Enrichit les équipements existants et ajoute les nouveaux
    void _mergeSsdp();

    // Fusionne les résultats du scan DNS-SD dans _results
    // Renseigne services, model (si vide), hostname (si vide), category (si vide/IoT)
    void _mergeDnsSd();

    // Charge les devices connus depuis DeviceStore et les injecte offline dans _results
    void _mergePersistedDevices();

    // Sauvegarde _results dans DeviceStore (fin de scan)
    void _saveToStore();

    // Scan TCP des ports communs + banner HTTP -> remplit openPorts, os, manufacturer
    void _scanPorts();

    // Requete NetBIOS Node Status sur les IP sans hostname -> renseigne hostname/source
    void _scanNetBios();

    // Enrichissement final : pattern matching sur le hostname (manufacturer/category/os)
    void _enrichDevices();

    // Classification intelligente : combine manufacturer/services/ports/hostname
    // pour affiner ou completer la categorie quand elle est encore vide/generique
    void _classifyDevices();

    // Met a jour firstSeen/lastSeen/seenCount et journalise les nouveautes/changements
    // detectes par rapport a l'etat precedent (avant le merge du scan courant)
    void _updateHistory(const std::vector<NetworkDevice>& previous);

    // Deduit l'OS depuis la valeur TTL ICMP et l'injecte dans os (si vide)
    static String _osFromTtl(uint8_t ttl);

    SemaphoreHandle_t           _mutex      = nullptr;
    TaskHandle_t                _taskHandle = nullptr;
    std::vector<NetworkDevice>  _results;
    volatile bool               _scanning   = false;
};

// Instance globale
extern NetworkScanner netScanner;
