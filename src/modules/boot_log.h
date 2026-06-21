/**
 * BootLog — Journal de débogage des redémarrages [MODULE TEMPORAIRE DE DEBOGAGE]
 *
 * Objectif : capturer le maximum d'informations avant un reboot (volontaire,
 * crash, watchdog, brownout...) sans avoir besoin d'un moniteur série branché
 * au moment des faits.
 *
 * Fonctionnement :
 *   - Un buffer circulaire des derniers logs (Log::i/w/e/d) est conserve en
 *     RTC_NOINIT_ATTR (RAM qui survit a un reboot logiciel/crash/watchdog,
 *     mais pas a une coupure d'alimentation ni a un reset franc du bouton).
 *   - Au demarrage suivant, begin() lit la raison du reset (esp_reset_reason)
 *     et, si le buffer precedent est valide, persiste le tout dans
 *     /bootlog.json sur LittleFS (borne a MAX_BOOT_LOG_ENTRIES, FIFO).
 *   - Le buffer RTC est ensuite reinitialise pour le boot en cours.
 *
 * Suppression facile en fin de débogage :
 *   1. Retirer l'inclusion et l'appel a bootLog.begin()/bootLog.capture() ici
 *      et dans logger.h / main.cpp.
 *   2. Retirer les routes /api/bootlog (GET/DELETE) de web_server.h/.cpp.
 *   3. Retirer la page /debug (web_src/debug.html, debug.js) et son lien
 *      dans web_src/menu.html.
 *   4. Supprimer ce fichier et boot_log.cpp.
 *   Aucune autre partie du projet ne depend de ce module.
 */

#pragma once
#include <Arduino.h>

class BootLog {
public:
    // A appeler le plus tot possible dans setup(), avant tout autre module.
    // Persiste le boot precedent (raison + logs captures) sur LittleFS puis
    // reinitialise le buffer pour le boot en cours.
    void begin();

    // Ajoute une ligne au buffer circulaire courant (appele par Log::*)
    void capture(const char* level, const char* tag, const char* msg);

    // Historique des boots persistes, du plus recent au plus ancien (JSON)
    String getLogJson() const;

    // Vide l'historique persiste
    void clear();

private:
    bool _mounted = false;
};

extern BootLog bootLog;
