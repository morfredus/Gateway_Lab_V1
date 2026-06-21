/**
 * BootLog — Implementation [MODULE TEMPORAIRE DE DEBOGAGE, voir boot_log.h]
 */

#include "boot_log.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include "../../include/app_config.h"   // MAX_BOOT_LOG_ENTRIES
#include "../utils/logger.h"

static const char* TAG = "BootLog";

static const int    kLines    = 24;    // Lignes conservees dans le buffer circulaire RTC
static const int    kLineLen  = 96;    // Longueur max par ligne (tronquee au-dela)
static const uint32_t kMagic  = 0xB007106Au;

struct BootLogRtcData {
    uint32_t magic;
    uint16_t count;                 // Nombre de lignes valides (<= kLines)
    uint16_t next;                  // Prochain index d'ecriture (ring)
    char     lines[kLines][kLineLen];
};

// RTC_NOINIT_ATTR : survit a un reboot logiciel/crash/watchdog, pas a une
// coupure d'alimentation ni a un reset franc (EN/bouton reset physique).
RTC_NOINIT_ATTR static BootLogRtcData _rtc;

BootLog bootLog;

static const char* _reasonText(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "Mise sous tension";
        case ESP_RST_EXT:       return "Reset externe (broche)";
        case ESP_RST_SW:        return "Redemarrage logiciel (ESP.restart)";
        case ESP_RST_PANIC:     return "Panic / exception non geree";
        case ESP_RST_INT_WDT:   return "Watchdog interrupt";
        case ESP_RST_TASK_WDT:  return "Watchdog tache (boucle bloquee)";
        case ESP_RST_WDT:       return "Watchdog (autre)";
        case ESP_RST_DEEPSLEEP: return "Reveil deep sleep";
        case ESP_RST_BROWNOUT:  return "Brownout (chute de tension)";
        case ESP_RST_SDIO:      return "Reset SDIO";
        default:                return "Inconnue";
    }
}

void BootLog::begin() {
    _mounted = LittleFS.begin(true);

    esp_reset_reason_t reason = esp_reset_reason();
    bool hadPreviousLines = (_rtc.magic == kMagic && _rtc.count > 0);

    if (_mounted) {
        JsonDocument doc;
        File f = LittleFS.open("/bootlog.json", "r");
        if (f) {
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (err) doc.to<JsonArray>();
        } else {
            doc.to<JsonArray>();
        }
        JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

        JsonObject entry = arr.add<JsonObject>();
        entry["bootMs"] = (uint32_t)millis();
        entry["reasonCode"] = (int)reason;
        entry["reasonText"] = _reasonText(reason);
        JsonArray lines = entry["lines"].to<JsonArray>();
        if (hadPreviousLines) {
            // Lit le ring buffer dans l'ordre chronologique (plus ancien -> plus recent)
            uint16_t start = (_rtc.count < kLines) ? 0 : _rtc.next;
            for (uint16_t i = 0; i < _rtc.count; i++) {
                uint16_t idx = (start + i) % kLines;
                lines.add(String(_rtc.lines[idx]));
            }
        }

        while ((int)arr.size() > MAX_BOOT_LOG_ENTRIES)
            arr.remove(0);

        File out = LittleFS.open("/bootlog.json", "w");
        if (out) {
            serializeJson(doc, out);
            out.close();
        }
    }

    uint16_t previousCount = hadPreviousLines ? _rtc.count : 0;

    // Reinitialise le buffer RTC pour le boot en cours avant le premier log,
    // pour que ce message inaugure bien le nouveau buffer
    _rtc.magic = kMagic;
    _rtc.count = 0;
    _rtc.next  = 0;
    memset(_rtc.lines, 0, sizeof(_rtc.lines));

    Log::i(TAG, "Raison du dernier reset : %s (%d) — %u lignes capturees au boot precedent",
           _reasonText(reason), (int)reason, previousCount);
}

void BootLog::capture(const char* level, const char* tag, const char* msg) {
    if (_rtc.magic != kMagic) {
        // Buffer pas encore initialise (capture appelee avant begin()) — ignore
        return;
    }
    char* dst = _rtc.lines[_rtc.next];
    snprintf(dst, kLineLen, "[%s][%s] %s", level, tag, msg);
    _rtc.next = (_rtc.next + 1) % kLines;
    if (_rtc.count < kLines) _rtc.count++;
}

String BootLog::getLogJson() const {
    if (!_mounted) return "[]";
    File f = LittleFS.open("/bootlog.json", "r");
    if (!f) return "[]";
    String json = f.readString();
    f.close();
    return json.length() ? json : "[]";
}

void BootLog::clear() {
    if (!_mounted) return;
    LittleFS.remove("/bootlog.json");
    Log::i(TAG, "Journal de demarrage vide");
}
