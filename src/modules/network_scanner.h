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

struct PortScanResult;   // Defini dans port_scanner.h
struct NetBiosInfo;      // Defini dans netbios_scanner.h
struct DnsSdInfo;        // Defini dans dns_sd_scanner.h

// Statistiques du scan : équipements connus / en ligne / hors ligne
struct ScanStats {
    int known   = 0;  // Total dans _results (online + offline)
    int online  = 0;  // Vus lors du dernier scan
    int offline = 0;  // Connus mais absents lors du dernier scan
};

// Avancement de la passe precise sur un seul equipement (rescanDevice) -
// permet a l'UI d'afficher une etape/pourcentage plutot qu'un bouton figé.
struct RescanStatus {
    bool   running = false;   // true pendant l'execution de la tache
    bool   ok      = false;   // resultat de la derniere passe terminee
    String ip;                // IP visee par la passe en cours/terminee
    String step;               // Etape courante, ex: "SSDP/UPnP"
    int    percent = 0;       // Avancement estime (0-100)
};

// Note libre datee, ajoutee par l'utilisateur sur un equipement de son
// inventaire (ex: "Cartouche d'encre noir changee le 12/05", "Firmware mis a jour")
struct DeviceNote {
    uint32_t ts = 0;   // Epoch NTP au moment de l'ajout (0 si horloge non synchronisee)
    String   text;
};

// Temps moyens/derniers d'execution, pour le cartouche diagnostics de l'UI
struct ScanTimings {
    uint32_t lastScanMs    = 0;
    uint32_t avgScanMs     = 0;
    uint32_t lastRescanMs  = 0;
    uint32_t avgRescanMs   = 0;
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
    String   type;          // Sous-type au sein de la catégorie : "Smart Speaker", "Smart Display"… ("" si non renseigné)
    String   model;         // Modèle détaillé si disponible (ex: "Freebox Ultra", "")
    String   os;            // Système d'exploitation — usage futur, vide en v0.0.7
    String   source;        // Source de résolution : "mDNS" | "PTR" | "MAC" | "Self" | ""
    String   services;      // Services DNS-SD détectés, séparés par '|' (ex: "HTTP|SSH|SMB")
    String   openPorts;    // Ports TCP ouverts, séparés par '|' (ex: "80|443|22")

    String   alias;         // Nom personnalise par l'utilisateur - prioritaire sur hostname a l'affichage
    uint32_t firstSeenEpoch = 0;   // Epoch NTP de la premiere detection (0 = inconnu, pas d'heure synchronisee)
    uint32_t lastSeenEpoch  = 0;   // Epoch NTP de la derniere detection (0 = inconnu)
    uint32_t seenCount      = 0;   // Nombre de scans ou l'equipement a ete vu en ligne

    bool     favorite = false;        // Equipement marque comme favori par l'utilisateur
    std::vector<DeviceNote> notes;    // Notes libres datees, saisies par l'utilisateur

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

    // Marque/demarque un equipement comme favori - retourne false si introuvable
    bool setFavorite(const String& macOrIp, bool favorite);

    // Ajoute une note datee (epoch NTP courant) a un equipement - retourne false si introuvable ou texte vide
    bool addNote(const String& macOrIp, const String& text);

    // Supprime une note par son timestamp - retourne false si equipement ou note introuvable
    bool deleteNote(const String& macOrIp, uint32_t ts);

    // Temps moyens/derniers (ms) d'un scan complet et d'une passe precise - cartouche diagnostics UI
    ScanTimings getTimings() const;

    // Etat memoire/stockage + temps de scan, serialise en JSON pour /api/diagnostics
    String diagnosticsToJson() const;

    // RAZ de la liste des equipements connus, pour repartir sur une base vide.
    // keepAlias        : conserve les equipements ayant un alias defini par l'utilisateur
    // keepManufacturer : conserve les equipements dont le fabricant a ete resolu (OUI)
    // Retourne le nombre d'equipements supprimes
    int resetDevices(bool keepAlias, bool keepManufacturer);

    // Sauvegarde JSON complete (devices + parametres) pour /api/backup
    String backupToJson() const;

    // Restauration depuis un JSON produit par backupToJson() - remplace les resultats actuels
    bool restoreFromJson(const String& json);

    // Export CSV de l'inventaire (un equipement par ligne) pour /api/devices/export.csv
    String devicesToCsv() const;

    // Lance la passe precise (asynchrone, tache FreeRTOS dediee) sur un seul
    // equipement identifie par IP : ARP/ICMP, hostname, ports, NetBIOS,
    // SSDP/UPnP, DNS-SD, SNMP. Retourne immediatement (true si la tache a
    // demarre) - suivre l'avancement via getRescanStatus().
    // Retourne false si l'IP est inconnue ou si un scan/rescan est deja en cours.
    bool rescanDevice(const String& ip);

    // Avancement courant de la passe precise (thread-safe) - pour le polling UI
    RescanStatus getRescanStatus() const;

    // Serialisation JSON de getRescanStatus() pour l'API /api/devices/rescan/status
    String rescanStatusToJson() const;

    // Sauvegarde immediate de _results dans DeviceStore (appui 5s du bouton BOOT)
    void saveNow();

    // true si le dernier scan a revele au moins un equipement absent du scan precedent
    bool hasNewDevices() const;

    // Acquitte les nouveaux equipements (visite de la page /scan) - remet hasNewDevices() a false
    void acknowledgeNewDevices();

private:
    // Point d'entrée de la tâche FreeRTOS (signature imposée par xTaskCreate)
    static void _task(void* self);

    // Corps du scan complet : sweep ARP + lecture finale + résolution hostnames
    void _run();

    // Point d'entree de la tache FreeRTOS dediee a la passe precise (rescanDevice)
    static void _rescanTask(void* self);

    // Corps de la passe precise sur _rescanStatus.ip - met a jour _rescanStatus
    // a chaque etape pour que l'UI puisse afficher un avancement reel
    void _runRescan(const String& ip);

    // Met a jour l'etape/pourcentage courant de la passe precise (thread-safe)
    void _setRescanProgress(const String& step, int percent);

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

    // Applique un equipement decouvert par SSDP a une entree existante
    // (extrait de _mergeSsdp() pour etre reutilisable par rescanDevice())
    void _applySsdpResult(NetworkDevice& d, const NetworkDevice& sdev);

    // Fusionne les résultats du scan DNS-SD dans _results
    // Renseigne services, model (si vide), hostname (si vide), category (si vide/IoT)
    void _mergeDnsSd();

    // Applique un resultat DNS-SD a une entree existante
    // (extrait de _mergeDnsSd() pour etre reutilisable par rescanDevice())
    void _applyDnsSdResult(NetworkDevice& d, const DnsSdInfo& info);

    // Charge les devices connus depuis DeviceStore et les injecte offline dans _results
    void _mergePersistedDevices();

    // Sauvegarde _results dans DeviceStore (fin de scan)
    void _saveToStore();

    // Scan TCP des ports communs + banner HTTP -> remplit openPorts, os, manufacturer
    void _scanPorts();

    // Fusionne le resultat d'un scan de ports dans un equipement (extrait de _scanPorts)
    void _applyPortScanResult(NetworkDevice& d, const PortScanResult& pr);

    // Requete NetBIOS Node Status sur les IP sans hostname -> renseigne hostname/source
    void _scanNetBios();

    // Fusionne le resultat d'une requete NetBIOS dans un equipement (extrait de _scanNetBios)
    void _applyNetBiosResult(NetworkDevice& d, const NetBiosInfo& info);

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

    // Calcule un score de confiance (0-100) et son libelle pour l'UI -
    // explique a l'utilisateur quelle source a permis l'identification
    static int _confidenceFor(const NetworkDevice& d, String& label);

    SemaphoreHandle_t           _mutex        = nullptr;
    TaskHandle_t                _taskHandle   = nullptr;
    TaskHandle_t                _rescanTaskHandle = nullptr;
    std::vector<NetworkDevice>  _results;
    volatile bool               _scanning   = false;
    RescanStatus                _rescanStatus;   // Protege par _mutex (lecture/ecriture)
    volatile bool               _newDevicesPending = false;   // true si le dernier scan a revele un nouvel equipement

    // Cumuls pour le calcul des temps moyens (cartouche diagnostics) - proteges par _mutex
    uint32_t _lastScanMs      = 0;
    uint32_t _scanMsTotal     = 0;
    uint32_t _scanCount       = 0;
    uint32_t _lastRescanMs    = 0;
    uint32_t _rescanMsTotal   = 0;
    uint32_t _rescanCount     = 0;
};

// Instance globale
extern NetworkScanner netScanner;
