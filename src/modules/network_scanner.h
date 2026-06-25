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
#include "../../include/app_config.h"   // MONITOR_INTERVAL_*, MOBILE_AWAY_*

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
    String mode;               // "quick" ou "deep" - passe en cours/terminee
    String profile;            // Profil d'equipement deduit, ex: "NAS", "Printer"
    std::vector<String> log;   // Journal des enrichissements de la derniere passe
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

    // Surveillance continue / score de stabilite (v1.0.0) ------------------
    uint32_t presenceCount       = 0;   // Nombre de transitions absent -> present (presence tick/scan)
    uint32_t absenceCount        = 0;   // Nombre de transitions present -> absent (hors penalite mobile courte)
    uint32_t reconnectionCount   = 0;   // Nombre de reconnexions (present -> absent -> present)
    uint32_t lastDisconnectEpoch = 0;   // Epoch NTP de la derniere deconnexion detectee (0 = inconnu)
    uint32_t totalOnlineSeconds  = 0;   // Cumul du temps observe en ligne (epoch deltas)
    uint32_t totalOfflineSeconds = 0;   // Cumul du temps observe hors ligne (epoque deltas, hors absences mobiles courtes)
    String   mobilityOverride;          // "" = auto-detection, "fixed" ou "mobile" (force par l'utilisateur)
    bool     mobileAwayNotified  = false; // true si l'evenement "mobile_left" a deja ete journalise pour l'absence en cours

    // Topologie reseau (v0.4.x) -----------------------------------------
    String   topologyParent;            // MAC de l'equipement en amont (AP/repeteur/switch) - declare par l'utilisateur OU deduit par decouverte SNMP - "" = inconnu/direct sur la passerelle
    bool     topologyParentAuto = false; // true si topologyParent vient de la decouverte SNMP (table de pontage) plutot que d'un glisser-depose manuel - permet de le rafraichir/corriger automatiquement sans jamais ecraser un choix manuel
    uint8_t  topologyParentConfidence = 0; // 0 = non applicable (rattachement manuel ou inconnu) ; 1-100 = confiance dans le rattachement automatique (FDB Bridge MIB directe = elevee)

    uint32_t lastSeen;      // millis() du dernier scan — converti en elapsed côté client
    bool     online;        // true si détecté lors du dernier scan
};

// Categorie "generique", c'est-a-dire pas encore suffisamment qualifiee pour
// etre consideree comme definitive : peut toujours etre ecrasee par une
// categorie plus specifique deduite par une source plus fiable (SSDP, API,
// enricher, classification par signaux). Sans ce test partage, certains
// equipements restaient bloques indefiniment en "Identification en cours"
// car cette valeur n'etait reconnue generique par aucun appelant.
inline bool isGenericCategory(const String& category) {
    return category.isEmpty() || category == "IoT" || category == "Identification en cours";
}

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
    // equipement identifie par IP — pose des questions ciblees a CET
    // equipement, jamais une decouverte reseau globale : aucun module SSDP,
    // DNS-SD ou WS-Discovery (multicast, non ciblable par IP) n'est jamais
    // lance depuis une passe precise.
    //   - deep=false (scan rapide, 1-3s)  : ARP/ICMP, PTR DNS, mise a jour du
    //     hostname, verification de presence. Rien d'autre.
    //   - deep=true  (scan approfondi, vise <3s si rien d'exploitable, sinon
    //     quelques secondes) : scan rapide, puis scan des ports de la cible
    //     uniquement (22,53,80,135,139,443,445,515,554,631,8080,8443,9100,5000).
    //     Si aucun port/service exploitable n'est trouve, la passe s'arrete
    //     immediatement. Sinon, un profil d'equipement (Computer, NAS,
    //     Printer, Streaming, SmartHome, Mobile, Unknown) est deduit des
    //     ports ouverts + informations deja connues, et seuls les modules
    //     cibles pertinents pour ce profil sont lances (NetBIOS, API
    //     multimedia Cast/Sonos/Roku/Samsung, SNMP) — toujours en requete
    //     unicast directe sur l'IP visee.
    // Retourne immediatement (true si la tache a demarre) - suivre
    // l'avancement via getRescanStatus().
    // Retourne false si l'IP est inconnue ou si un scan/rescan est deja en cours.
    bool rescanDevice(const String& ip, bool deep = false);

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

    // ------------------------------------------------------------------
    // Surveillance continue (v1.0.0) — Niveau 1
    // ------------------------------------------------------------------

    // A appeler a chaque iteration de loop() — gere elle-meme la frequence
    // (millis(), bornee par getMonitorInterval()). Tick leger : sweep ARP
    // seul (_sweepSubnet()) + mise a jour de presence/absence + bookkeeping
    // stabilite. Aucune decouverte SSDP/DNS-SD/WS-Discovery/SNMP/API n'est
    // jamais lancee depuis cette methode. Ignore le tick si un scan complet
    // ou une passe precise est en cours (_scanning) - retente au tick suivant.
    // Draine egalement une entree de la file d'attente differee (scan rapide
    // ou approfondi) si aucun scan n'est en cours.
    void serviceMonitor();

    // Frequence du tick de surveillance, en minutes (1-60) - persiste en NVS
    void setMonitorInterval(int minutes);
    int  getMonitorInterval() const;

    // Active/desactive la surveillance continue - persiste en NVS. Quand
    // desactivee, serviceMonitor() ne fait plus rien (aucun tick, aucun
    // drainage de la file differee).
    void setMonitorEnabled(bool enabled);
    bool getMonitorEnabled() const;

    // Force/annule la classification mobile/fixe d'un equipement
    // ("", "fixed", "mobile") - retourne false si introuvable
    bool setMobility(const String& macOrIp, const String& mode);

    // Declare manuellement le parent reseau (AP/repeteur/switch en amont)
    // d'un equipement, identifie par sa MAC - "" pour effacer (inconnu/direct
    // sur la passerelle). Retourne false si l'equipement ou le parent
    // (quand non vide) est introuvable, ou si parentMac == macOrIp.
    bool setTopologyParent(const String& macOrIp, const String& parentMac);

    // Definit la racine de l'arbre de topologie (MAC d'un equipement connu) -
    // persiste en NVS. "" = automatique : la box operateur (categorie
    // "Router") plutot que l'ESP32 lui-meme (categorie "Gateway", qui n'est
    // qu'un equipement parmi d'autres sur le reseau de l'utilisateur).
    void setTopologyRoot(const String& mac);
    String getTopologyRoot() const;

    // Donnees de sante reseau pour le tableau de bord (compteurs 24h,
    // equipements presents/connus, equipements les moins stables)
    String networkHealthToJson() const;

private:
    // Point d'entrée de la tâche FreeRTOS (signature imposée par xTaskCreate)
    static void _task(void* self);

    // Corps du scan complet : sweep ARP + lecture finale + résolution hostnames
    void _run();

    // Point d'entree de la tache FreeRTOS dediee a la passe precise (rescanDevice)
    static void _rescanTask(void* self);

    // Corps de la passe precise sur _rescanStatus.ip - met a jour _rescanStatus
    // a chaque etape pour que l'UI puisse afficher un avancement reel.
    // deep=false : modules limites au profil deduit (scan rapide).
    // deep=true  : tous les modules pertinents pour le profil (scan approfondi).
    void _runRescan(const String& ip, bool deep);

    // Deduit un profil d'equipement probable a partir des informations deja
    // connues (fabricant, hostname, modele, categorie, services, ports) -
    // simple hypothese servant a choisir les modules de decouverte les plus
    // pertinents pour la passe precise. Ne modifie jamais le NetworkDevice.
    static String _profileFor(const NetworkDevice& d);

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

    // Borne _results à MAX_TRACKED_DEVICES (évince les + anciens hors-ligne non favoris)
    // Appelée avec _mutex déjà acquis.
    void _evictOldestLocked();

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
    // detectes par rapport a l'etat precedent (avant le merge du scan courant).
    // allowRequeue=false depuis une passe precise (_runRescan) : evite qu'un
    // equipement dont la confiance reste durablement < 35% (cas frequent pour
    // un scan rapide, volontairement minimal) se re-mette en file a l'infini.
    void _updateHistory(const std::vector<NetworkDevice>& previous, bool allowRequeue = true);

    // Deduit l'OS depuis la valeur TTL ICMP et l'injecte dans os (si vide)
    static String _osFromTtl(uint8_t ttl);

    // Calcule un score de confiance (0-100) et son libelle pour l'UI -
    // explique a l'utilisateur quelle source a permis l'identification
    static int _confidenceFor(const NetworkDevice& d, String& label);

    // ------------------------------------------------------------------
    // Surveillance continue / stabilite (v1.0.0)
    // ------------------------------------------------------------------

    // Sweep ARP leger + mise a jour presence/absence/compteurs de stabilite
    // pour tous les equipements connus. Appele par serviceMonitor() - jamais
    // de SSDP/DNS-SD/SNMP/API ici. Detecte aussi les equipements inconnus
    // (nouvelles entrees ARP) et les place en file d'attente de scan rapide.
    void _monitorTick();

    // Deduit si l'equipement est probablement mobile (smartphone, tablette,
    // montre connectee, portable) a partir de mobilityOverride (priorite) ou
    // de category/type. Par defaut, considere l'equipement comme fixe
    // (hypothese conservatrice : ne jamais escamoter une instabilite reelle).
    static bool _isMobileDevice(const NetworkDevice& d);

    // Score de stabilite 0-100% pour les equipements fixes (ratio temps en
    // ligne / temps observe total, attenue par les reconnexions frequentes ;
    // 100 par defaut si l'historique est encore trop court pour juger).
    // Retourne -1 ("N/A — non penalise") pour les equipements mobiles.
    static int _stabilityScoreFor(const NetworkDevice& d);

    // Met en file d'attente differee un scan rapide/approfondi sur une IP
    // (deduplique) - draine par serviceMonitor() quand _scanning est libre.
    // Appele avec _mutex deja acquis.
    void _queueQuickScanLocked(const String& ip);
    void _queueDeepScanLocked(const String& ip);

    // Sweep periodique (RESCAN_SWEEP_INTERVAL_MINUTES) qui met en file un
    // scan approfondi pour chaque equipement en ligne reste sur une categorie
    // generique ("IoT" ou "Identification en cours") — sans cela, un
    // equipement qui n'a jamais ete revisite par l'utilisateur peut rester
    // bloque indefiniment, meme une fois le bug de categorie generique
    // (isGenericCategory) corrige. Appele depuis serviceMonitor().
    void _sweepUnidentified();

    // Decouverte automatique de la topologie par SNMP (table de pontage,
    // dot1dTpFdbTable) — v1.4.0. Interroge chaque equipement de type
    // routeur/point d'acces/repeteur qui expose un agent SNMP en lecture
    // publique, recupere la liste des MAC qu'il pontage (donc rattachees a
    // lui), et complete topologyParent pour ces equipements quand il n'a pas
    // ete fixe manuellement (topologyParentAuto). Best-effort : ne fait rien
    // si l'equipement ne repond pas (agent SNMP absent ou desactive — le cas
    // de la plupart des repeteurs mesh grand public type Deco/Orbi/eero).
    // Appele depuis serviceMonitor(), independamment de _sweepUnidentified().
    void _discoverTopologyViaSnmp();

    // Draine une seule entree de la file d'attente differee (scan rapide en
    // priorite, puis approfondi) - ne fait rien si un scan est deja en cours.
    void _drainPendingScans();

    SemaphoreHandle_t           _mutex        = nullptr;
    TaskHandle_t                _taskHandle   = nullptr;
    TaskHandle_t                _rescanTaskHandle = nullptr;
    std::vector<NetworkDevice>  _results;
    volatile bool               _scanning   = false;
    RescanStatus                _rescanStatus;   // Protege par _mutex (lecture/ecriture)
    bool                         _rescanDeep  = false;   // Mode de la passe en cours, lu une seule fois par _rescanTask
    volatile bool               _newDevicesPending = false;   // true si le dernier scan a revele un nouvel equipement

    // Cumuls pour le calcul des temps moyens (cartouche diagnostics) - proteges par _mutex
    uint32_t _lastScanMs      = 0;
    uint32_t _scanMsTotal     = 0;
    uint32_t _scanCount       = 0;
    uint32_t _lastRescanMs    = 0;
    uint32_t _rescanMsTotal   = 0;
    uint32_t _rescanCount     = 0;

    // Surveillance continue (v1.0.0) - proteges par _mutex sauf mention contraire
    uint32_t _monitorIntervalMinutes = MONITOR_INTERVAL_DEFAULT_MINUTES;  // Lu/ecrit via NVS, hors mutex (idem status_led)
    bool     _monitorEnabled         = true;  // Lu/ecrit via NVS, hors mutex (idem status_led)
    uint32_t _lastMonitorTickMs      = 0;     // millis() du dernier tick execute (0 = jamais)
    uint32_t _lastUnidentifiedSweepMs = 0;    // millis() du dernier sweep "non identifies" execute (0 = jamais)
    uint32_t _lastTopologySnmpSweepMs = 0;    // millis() de la derniere decouverte de topologie par SNMP (0 = jamais)
    std::vector<String>          _pendingQuickScan;   // File d'attente differee (IP), dedupliquee
    std::vector<String>          _pendingDeepScan;    // File d'attente differee (IP), dedupliquee

    // Topologie reseau (v0.4.x) - lu/ecrit via NVS, hors mutex (idem _monitorEnabled)
    String _topologyRootMac;   // "" = automatique (box operateur)
};

// Instance globale
extern NetworkScanner netScanner;
