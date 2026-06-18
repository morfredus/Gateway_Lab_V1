/**
 * BootButton — Bouton BOOT (board_config.h::BUTTON_BOOT_PIN), actif à LOW
 *
 * Détection non bloquante (loop() à appeler sans interruption) :
 *   Appui court (< 3 s)   -> onShortPress  (lance un scan)
 *   Maintien >= 3 s       -> onHold        (sauvegarde immediate)
 */

#pragma once
#include <Arduino.h>
#include <functional>

struct BootButtonCallbacks {
    std::function<void()> onShortPress;
    std::function<void()> onHold;
};

class BootButton {
public:
    void begin(BootButtonCallbacks cb);
    void loop();   // À appeler sans interruption dans loop()

private:
    BootButtonCallbacks _cb;
};

extern BootButton bootButton;
