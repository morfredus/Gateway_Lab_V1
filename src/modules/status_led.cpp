/**
 * StatusLed — Implémentation
 *
 * Bibliothèque utilisée : Adafruit_NeoPixel (un seul pixel, board_config.h::NEOPIXEL_PIN)
 */

#include "status_led.h"
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <math.h>
#include "../../include/board_config.h"
#include "../utils/logger.h"

static const char* TAG           = "Led";
static const char* NVS_NAMESPACE = "led";
static const char* NVS_KEY       = "brightness";

static Adafruit_NeoPixel _pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

StatusLed statusLed;

// Couleurs (GRB géré par la bibliothèque — on raisonne en RGB)
static const uint32_t COLOR_BLUE   = 0x1040FF;
static const uint32_t COLOR_GREEN  = 0x00C040;
static const uint32_t COLOR_YELLOW = 0xC0A000;
static const uint32_t COLOR_VIOLET = 0x8000C0;
static const uint32_t COLOR_CYAN   = 0x00C0C0;
static const uint32_t COLOR_OFF    = 0x000000;

static inline uint32_t _scaleColor(uint32_t color, uint8_t pct) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b =  color        & 0xFF;
    r = (uint16_t)r * pct / 100;
    g = (uint16_t)g * pct / 100;
    b = (uint16_t)b * pct / 100;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void StatusLed::begin() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    _brightnessPct = prefs.getUChar(NVS_KEY, 15);
    prefs.end();
    if (_brightnessPct > 100) _brightnessPct = 15;

    _pixel.begin();
    _pixel.setPixelColor(0, COLOR_OFF);
    _pixel.show();

    Log::i(TAG, "NeoPixel initialisée — luminosité %u %%", (unsigned)_brightnessPct);
}

void StatusLed::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    _brightnessPct = percent;

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar(NVS_KEY, percent);
    prefs.end();
}

void StatusLed::setState(LedState state, uint32_t holdMs) {
    _state          = state;
    _stateExpiresAt = holdMs ? (millis() + holdMs) : 0;
}

void StatusLed::_render() {
    uint32_t now = millis();

    if (_stateExpiresAt && now >= _stateExpiresAt) {
        _state          = LedState::Ready;
        _stateExpiresAt = 0;
    }

    switch (_state) {
        case LedState::Boot: {
            // Pulse bleu — respiration sinusoïdale, période 1500 ms
            float phase = (now % 1500) / 1500.0f;
            float level = (sinf(phase * 2 * PI) + 1.0f) / 2.0f;   // 0..1
            uint8_t pct = (uint8_t)(_brightnessPct * level);
            _pixel.setPixelColor(0, _scaleColor(COLOR_BLUE, pct));
            break;
        }
        case LedState::Ready:
            _pixel.setPixelColor(0, _scaleColor(COLOR_BLUE, _brightnessPct));
            break;
        case LedState::Scanning: {
            bool on = (now / 350) % 2 == 0;
            _pixel.setPixelColor(0, on ? _scaleColor(COLOR_GREEN, _brightnessPct) : COLOR_OFF);
            break;
        }
        case LedState::NewDevice: {
            bool on = (now / 600) % 2 == 0;
            _pixel.setPixelColor(0, on ? _scaleColor(COLOR_YELLOW, _brightnessPct) : COLOR_OFF);
            break;
        }
        case LedState::WifiPortal:
            _pixel.setPixelColor(0, _scaleColor(COLOR_VIOLET, _brightnessPct));
            break;
        case LedState::Saving:
            _pixel.setPixelColor(0, _scaleColor(COLOR_CYAN, _brightnessPct));
            break;
    }
    _pixel.show();
}

void StatusLed::loop() {
    _render();
}
