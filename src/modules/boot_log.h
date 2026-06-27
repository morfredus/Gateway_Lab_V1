/**
 * BootLog — Journal de débogage des redémarrages [MODULE TEMPORAIRE DE DEBOGAGE]
 *
 * Objectif : capturer le maximum d'informations avant un reboot (volontaire,
 * crash, watchdog, brownout...) sans avoir besoin d'un moniteur série branché
 * au moment des faits.
 *
 * Fonctionnement général :
 *   - Un buffer circulaire des derniers logs (Log::i/w/e/d) est conservé en
 *     RTC_NOINIT_ATTR (RAM qui survit à un reboot logiciel/crash/watchdog,
 *     mais pas à une coupure d'alimentation ni à un reset franc du bouton).
 *     Chaque ligne du buffer est un objet JSON compact (timestamp, niveau,
 *     tag, message, heap libre, plus gros bloc libre au moment du log).
 *   - Le même bloc RTC_NOINIT_ATTR conserve aussi un instantané périodique
 *     de l'état système (RuntimeStats), le dernier "task" connu (voir
 *     setLastTask()) et le dernier état WiFi observé — mis à jour par
 *     service(), à appeler depuis loop().
 *   - Au démarrage suivant, begin() lit la raison du reset
 *     (esp_reset_reason) et, si le buffer précédent est valide, persiste
 *     tout cela dans /bootlog.json sur LittleFS (borné à
 *     MAX_BOOT_LOG_ENTRIES, FIFO).
 *   - boot_count / crash_count sont stockés en NVS (Preferences), seule
 *     mémoire du projet qui survit aussi à une coupure d'alimentation.
 *   - Le buffer RTC est ensuite réinitialisé pour le boot en cours.
 *
 * Suppression facile en fin de débogage :
 *   1. Retirer l'inclusion et l'appel à bootLog.begin()/capture()/service()
 *      ici et dans logger.h / main.cpp.
 *   2. Retirer les routes /api/bootlog (GET/DELETE) de web_server.h/.cpp.
 *   3. Retirer la page /debug (web_src/debug.html, debug.js) et son lien
 *      dans web_src/menu.html.
 *   4. Supprimer ce fichier et boot_log.cpp.
 *   Aucune autre partie du projet ne dépend de ce module.
 *
 * Limite connue (trace d'appel / stack trace au PANIC) :
 *   Le framework Arduino n'expose pas de hook applicatif execute pendant un
 *   PANIC ESP-IDF — a cet instant, le code utilisateur (donc ce module) ne
 *   tourne plus. Une vraie capture de backtrace necessiterait le composant
 *   ESP-IDF "esp_core_dump" (zone flash/RTC dediee + outil decodeur cote PC),
 *   hors de portee d'un module Arduino autonome a fichier unique. La trace
 *   reelle du PANIC reste donc visible uniquement sur le moniteur serie
 *   (comportement par defaut de l'ESP-IDF) ; ce module se contente de
 *   capturer la raison du reset (ESP_RST_PANIC, etc.) et les derniers logs
 *   applicatifs avant la coupure, ce qui couvre l'essentiel des besoins de
 *   debogage sans moniteur serie.
 */

#pragma once
#include <Arduino.h>
#include <functional>

// Instantané périodique de l'état système, rafraîchi toutes les
// BOOT_LOG_STATS_INTERVAL_MS millisecondes par service() — survit en RTC.
struct RuntimeStats {
    uint32_t uptime       = 0;   // millis() au moment de l'instantané
    uint32_t freeHeap     = 0;   // ESP.getFreeHeap()
    uint32_t largestBlock = 0;   // heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
    uint32_t devicesCount = 0;   // Fourni par setDevicesCountProvider() (optionnel)
    uint32_t pagesServed  = 0;   // Compteur cumulé, voir notePageServed()
    uint32_t apiCalls     = 0;   // Compteur cumulé, voir noteApiCall()
};

class BootLog {
public:
    // A appeler le plus tôt possible dans setup(), avant tout autre module
    // (et avant le premier Log::*). Persiste le boot précédent (raison,
    // logs, stats, dernier état WiFi/tâche) sur LittleFS, incrémente
    // boot_count/crash_count en NVS, puis réinitialise le buffer RTC pour
    // le boot en cours.
    void begin();

    // A appeler dans loop() — mise à jour peu coûteuse de l'uptime/heap a
    // chaque appel, et instantané complet (RuntimeStats + WiFi) toutes les
    // BOOT_LOG_STATS_INTERVAL_MS millisecondes. force=true ignore le délai
    // (à utiliser juste avant un redémarrage volontaire, ex. fin d'OTA).
    void service(bool force = false);

    // Ajoute une ligne au buffer circulaire courant (appelé par Log::*) —
    // chaque ligne est un JSON compact incluant heap/bloc libre courants.
    void capture(const char* level, const char* tag, const char* msg);

    // Trace de la dernière "tâche" en cours (ex: "Scan réseau"), conservée
    // en RTC et incluse dans l'entrée persistée au boot suivant — utile
    // pour savoir ce que faisait le firmware juste avant un crash muet.
    void setLastTask(const String& task);

    // Fournisseur optionnel du nombre d'équipements connus, inclus dans
    // les RuntimeStats périodiques (ex: depuis main.cpp,
    // bootLog.setDevicesCountProvider([] { return netScanner.deviceCount(); });)
    void setDevicesCountProvider(std::function<uint32_t()> provider);

    // Compteurs cumulés exposés dans RuntimeStats — à appeler depuis
    // WebServerModule (une page servie / un appel API traité).
    void notePageServed();
    void noteApiCall();

    // Historique des boots persistés, du plus récent au plus ancien (JSON) —
    // équivalent de LogManager::Dump() dans la nomenclature habituelle.
    String getLogJson() const;

    // Vide l'historique persisté (le fichier LittleFS ; ne touche pas aux
    // compteurs NVS boot_count/crash_count)
    void clear();

private:
    bool     _mounted          = false;
    uint32_t _lastStatsSnapMs  = 0;
    std::function<uint32_t()> _devicesCountProvider;
};

extern BootLog bootLog;
