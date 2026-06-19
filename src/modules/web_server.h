/**
 * WebServerModule — Serveur HTTP et interface web de Gateway Lab V1
 *
 * Routes exposées :
 *   GET  /             — Page d'accueil (HTML embarqué en PROGMEM)
 *   GET  /api/status   — État du système en JSON (WiFi, version, uptime...)
 *   GET  /api/devices  — Liste des équipements découverts + état du scan
 *   POST /api/scan     — Déclenchement d'un scan réseau
 *   POST /api/alias    — Definit l'alias utilisateur d'un equipement
 *   POST /api/devices/reset — RAZ des equipements connus (base vide)
 *   POST /api/devices/rescan — Rafraichit un seul equipement (parametre ip), asynchrone
 *   GET  /api/devices/rescan/status — Avancement de la passe precise en cours (polling)
 *   DELETE /api/history — Vide le journal chronologique
 *   GET  /history       — Page vue chronologique (HTML embarque en PROGMEM)
 *   GET  /topology      — Page topologie / cartographie reseau (HTML embarque en PROGMEM, vue simplifiee)
 *   GET  /api/history   — Journal chronologique des evenements en JSON
 *   GET  /api/backup    — Telechargement de la sauvegarde complete (JSON)
 *   POST /api/restore   — Restauration depuis une sauvegarde JSON
 *   GET  /api/devices/export.csv — Telechargement de l'inventaire au format CSV
 *   GET  /api/system/backup  — Sauvegarde des parametres de fonctionnement (JSON) :
 *                              reseaux WiFi enregistres, luminosite NeoPixel, nom mDNS
 *   POST /api/system/restore — Restauration des parametres de fonctionnement depuis une
 *                              sauvegarde JSON generee par /api/system/backup
 *   POST /api/favorite  — Marque/demarque un equipement comme favori
 *   POST /api/notes     — Ajoute une note datee a un equipement
 *   DELETE /api/notes   — Supprime une note d'un equipement (parametre ts)
 *   GET  /api/diagnostics — Heap/PSRAM/LittleFS + temps de scan moyens (JSON)
 *   GET  /api/led/brightness  — Luminosite NeoPixel courante (JSON)
 *   POST /api/led/brightness  — Definit la luminosite NeoPixel (parametre value, 0-100)
 *   GET  /wifi          — Page Parametres > Reseau WiFi (HTML embarque en PROGMEM)
 *   GET  /api/wifi      — Etat de connexion + reseaux enregistres (sans mots de passe)
 *   POST /api/wifi      — Ajoute ou met a jour un reseau (ssid + password)
 *   DELETE /api/wifi    — Supprime un reseau enregistre (parametre ssid)
 *   GET  /update       — Page de mise à jour OTA (formulaire upload)
 *   POST /update       — Réception et installation d'un firmware .bin
 *
 * Découplage via ScanProvider :
 *   WebServerModule ne connaît pas NetworkScanner directement.
 *   Il reçoit un ensemble de fonctions lambda (ScanProvider) qui font le lien.
 *   Cela permet de remplacer le scanner sans modifier le serveur.
 */

#pragma once
#include <Arduino.h>
#include <functional>

// Interface de communication entre le serveur web et le scanner réseau.
// Chaque champ est une fonction lambda fournie par main.cpp.
struct ScanProvider {
    std::function<bool()>   isScanning;   // Le scan est-il en cours ?
    std::function<String()> getJson;      // Résultats sérialisés en JSON
    std::function<void()>   triggerScan;  // Lancement d'un nouveau scan
    std::function<String()> getStats;     // Stats JSON : {"known":X,"online":Y,"offline":Z}

    std::function<bool(const String& macOrIp, const String& alias)> setAlias;  // Alias utilisateur
    std::function<int(bool keepAlias, bool keepManufacturer)> resetDevices;    // RAZ des equipements connus
    std::function<bool(const String& ip, bool deep)> rescanDevice;   // Rafraichit un seul equipement (sans scan complet) - deep=true pour le scan approfondi
    std::function<String()> getRescanStatusJson;          // Avancement de la passe precise en cours (polling UI)
    std::function<String()> getHistoryJson;   // Journal chronologique (evenements) en JSON
    std::function<void()>   clearHistory;     // Vide le journal d'historique
    std::function<String()> getBackupJson;    // Sauvegarde complete en JSON
    std::function<bool(const String& json)>   restoreFromJson;   // Restauration depuis JSON
    std::function<String()> getDevicesCsv;    // Export CSV de l'inventaire

    std::function<bool(const String& macOrIp, bool favorite)> setFavorite;          // Favori utilisateur
    std::function<bool(const String& macOrIp, const String& text)> addNote;         // Ajoute une note datee
    std::function<bool(const String& macOrIp, uint32_t ts)> deleteNote;             // Supprime une note par timestamp
    std::function<String()> getDiagnosticsJson;   // Heap/PSRAM/LittleFS/temps de scan en JSON
    std::function<void()>   acknowledgeNewDevices; // Acquitte les nouveaux equipements (visite de /scan)
};

class WebServerModule {
public:
    // Enregistrement du fournisseur de données scanner (avant begin())
    void registerScanProvider(ScanProvider p);

    // Démarrage du serveur HTTP sur le port spécifié
    void begin(uint16_t port = 80);

    // Traitement des requêtes HTTP en attente — appeler dans loop()
    void loop();

private:
    void _handleRoot();             // Sert la page HTML principale
    void _handleApiStatus();        // Retourne l'état WiFi/système en JSON
    void _handleApiDevices();       // Retourne la liste des équipements en JSON
    void _handleApiScanTrigger();   // Démarre un scan réseau
    void _handleApiSetAlias();      // Definit l'alias utilisateur d'un equipement
    void _handleApiDevicesReset();  // RAZ des equipements connus (base vide)
    void _handleApiDeviceRescan();  // Rafraichit un seul equipement (sans scan complet)
    void _handleApiDeviceRescanStatus();  // Avancement de la passe precise en cours
    void _handleApiHistory();       // Retourne le journal chronologique en JSON
    void _handleApiHistoryClear();  // Vide le journal chronologique
    void _handleApiBackup();        // Retourne la sauvegarde complete en JSON
    void _handleApiRestore();       // Restaure depuis une sauvegarde JSON envoyee
    void _handleApiDevicesExportCsv(); // Retourne l'inventaire au format CSV (telechargement)
    void _handleApiSetFavorite();   // Marque/demarque un equipement comme favori
    void _handleApiAddNote();       // Ajoute une note datee a un equipement
    void _handleApiDeleteNote();    // Supprime une note d'un equipement
    void _handleApiDiagnostics();   // Heap/PSRAM/LittleFS/temps de scan en JSON
    void _handleApiWifiGet();       // Retourne l'etat WiFi + reseaux enregistres
    void _handleApiWifiPost();      // Ajoute ou met a jour un reseau enregistre
    void _handleApiWifiDelete();    // Supprime un reseau enregistre
    void _handleApiSystemBackup();  // Sauvegarde des parametres de fonctionnement (JSON)
    void _handleApiSystemRestore(); // Restaure les parametres de fonctionnement depuis JSON
    void _handleNotFound();         // Réponse 404 pour les routes inconnues

    ScanProvider _scan;
    bool         _hasScan  = false;  // true si registerScanProvider() a été appelé
    bool         _started  = false;  // true si begin() a déjà été appelé
};

// Instance globale
extern WebServerModule webSrv;
