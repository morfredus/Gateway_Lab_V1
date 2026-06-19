/**
 * TimeSync - Synchronisation de l'horloge systeme via NTP
 *
 * L'ESP32 ne dispose d'aucune horloge temps reel persistante : sans
 * synchronisation, time(nullptr) renvoie un epoch proche de 0. Ce module
 * lance la synchronisation NTP une fois le WiFi connecte et expose un
 * helper pour savoir si l'heure est fiable - utilise par l'historique des
 * equipements (firstSeen/lastSeen en epoch reel plutot qu'en millis()).
 *
 * Sans synchronisation reussie (reseau sans acces Internet), les
 * fonctionnalites d'historique se degradent proprement : firstSeen/lastSeen
 * restent a 0 et ne sont pas affiches, sans bloquer le reste du scan.
 */

#pragma once
#include <Arduino.h>

class TimeSync {
public:
    // Lance la synchronisation NTP (non bloquant, plusieurs serveurs)
    void begin();

    // true si l'heure systeme a ete synchronisee avec succes
    bool isSynced();

    // Epoch courant en secondes (0 si non synchronise)
    uint32_t nowEpoch();
};

extern TimeSync timeSync;
