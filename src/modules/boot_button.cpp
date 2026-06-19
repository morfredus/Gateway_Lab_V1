/**
 * BootButton — Implémentation
 */

#include "boot_button.h"
#include "../../include/board_config.h"
#include "../utils/logger.h"

static const char* TAG = "Button";

static constexpr uint32_t DEBOUNCE_MS = 30;
static constexpr uint32_t HOLD_MS     = 3000;

BootButton bootButton;

// État interne (machine à états simple, pilotée par millis())
static bool     _pressed     = false;   // État debouncé courant
static bool     _rawPressed  = false;
static uint32_t _lastChangeMs = 0;
static uint32_t _pressStartMs = 0;
static bool     _holdFired    = false;

void BootButton::begin(BootButtonCallbacks cb) {
    _cb = cb;
    pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);
    Log::i(TAG, "Bouton BOOT initialisé (GPIO %d)", BUTTON_BOOT_PIN);
}

void BootButton::loop() {
    uint32_t now = millis();
    bool raw = digitalRead(BUTTON_BOOT_PIN) == LOW;   // Actif à LOW (pull-up)

    if (raw != _rawPressed) {
        _rawPressed   = raw;
        _lastChangeMs = now;
    } else if (now - _lastChangeMs >= DEBOUNCE_MS && raw != _pressed) {
        _pressed = raw;

        if (_pressed) {
            // Front descendant — début d'un appui
            _pressStartMs = now;
            _holdFired    = false;
        } else if (!_holdFired) {
            // Front montant — appui court relâché avant le seuil de maintien
            if (_cb.onShortPress) _cb.onShortPress();
        }
    }

    // Maintien : declenche immediatement pendant l'appui (pas d'attente du relachement)
    if (_pressed && !_holdFired) {
        if (now - _pressStartMs >= HOLD_MS) {
            _holdFired = true;
            if (_cb.onHold) _cb.onHold();
        }
    }
}
