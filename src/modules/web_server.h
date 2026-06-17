/**
 * WebServerModule — Serveur HTTP et interface web de Gateway Lab V1
 *
 * Routes exposées :
 *   GET  /             — Page d'accueil (HTML embarqué en PROGMEM)
 *   GET  /api/status   — État du système en JSON (WiFi, version, uptime...)
 *   GET  /api/devices  — Liste des équipements découverts + état du scan
 *   POST /api/scan     — Déclenchement d'un scan réseau
 *   POST /api/alias    — Definit l'alias utilisateur d'un equipement
 *   GET  /history       — Page vue chronologique (HTML embarque en PROGMEM)
 *   GET  /api/history   — Journal chronologique des evenements en JSON
 *   GET  /api/backup    — Telechargement de la sauvegarde complete (JSON)
 *   POST /api/restore   — Restauration depuis une sauvegarde JSON
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
    std::function<String()> getHistoryJson;   // Journal chronologique (evenements) en JSON
    std::function<String()> getBackupJson;    // Sauvegarde complete en JSON
    std::function<bool(const String& json)>   restoreFromJson;   // Restauration depuis JSON
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
    void _handleApiHistory();       // Retourne le journal chronologique en JSON
    void _handleApiBackup();        // Retourne la sauvegarde complete en JSON
    void _handleApiRestore();       // Restaure depuis une sauvegarde JSON envoyee
    void _handleApiWifiGet();       // Retourne l'etat WiFi + reseaux enregistres
    void _handleApiWifiPost();      // Ajoute ou met a jour un reseau enregistre
    void _handleApiWifiDelete();    // Supprime un reseau enregistre
    void _handleNotFound();         // Réponse 404 pour les routes inconnues

    ScanProvider _scan;
    bool         _hasScan  = false;  // true si registerScanProvider() a été appelé
    bool         _started  = false;  // true si begin() a déjà été appelé
};

// Instance globale
extern WebServerModule webSrv;
