/**
 * StatusLed — Pilotage de la NeoPixel d'état (board_config.h::NEOPIXEL_PIN)
 *
 * États (un seul actif à la fois) :
 *   Boot       Bleu pulsé       Démarrage / connexion WiFi
 *   Ready      Bleu fixe        Gateway Lab prêt
 *   Scanning   Vert clignotant  Scan en cours
 *   NewDevice  Jaune clignotant Nouvel équipement détecté, jusqu'à consultation de /scan
 *   WifiPortal Violet           Portail WiFi actif (première configuration)
 *   Saving     Cyan fixe        Sauvegarde en cours (retour automatique à Ready)
 *
 * Luminosité (0-100 %) réglable par l'utilisateur (page Paramètres),
 * persistée en NVS (Preferences, namespace "led") — 15 % par défaut au
 * premier démarrage.
 *
 * loop() doit être appelée sans interruption (boucle principale) pour faire
 * progresser les animations — ne bloque jamais (pas de delay()).
 */

#pragma once
#include <Arduino.h>

enum class LedState {
    Boot,
    Ready,
    Scanning,
    NewDevice,
    WifiPortal,
    Saving,
};

class StatusLed {
public:
    // Initialise la NeoPixel et restaure la luminosité depuis NVS
    void begin();

    // Anime l'état courant — à appeler dans loop()
    void loop();

    // Definit l'etat courant. holdMs > 0 : retour automatique a Ready apres
    // ce delai (sauf si l'etat a ete change entre-temps) — utilise pour les
    // etats transitoires (ex: Saving).
    void setState(LedState state, uint32_t holdMs = 0);
    LedState state() const { return _state; }

    // Luminosite utilisateur (0-100 %), persistee en NVS
    void    setBrightness(uint8_t percent);
    uint8_t getBrightness() const { return _brightnessPct; }

private:
    void _render();

    LedState _state         = LedState::Boot;
    uint32_t _stateExpiresAt = 0;   // 0 = pas d'expiration
    uint8_t  _brightnessPct  = 15;
};

extern StatusLed statusLed;
