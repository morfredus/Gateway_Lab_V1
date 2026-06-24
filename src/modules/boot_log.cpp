/**
 * BootLog — Implementation [MODULE TEMPORAIRE DE DEBOGAGE, voir boot_log.h]
 */

#include "boot_log.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "../../include/app_config.h"   // MAX_BOOT_LOG_ENTRIES, LOG_BUFFER_SIZE, LOG_LINE_MAX_LEN, BOOT_LOG_STATS_INTERVAL_MS
#include "../utils/logger.h"

static const char* TAG = "BootLog";

// IMPORTANT : ce magic doit etre incremente a chaque fois que la structure
// BootLogRtcData change de layout (champs ajoutes/retires/reordonnes ou
// dimensions kLines/kLineLen modifiees). RTC_NOINIT_ATTR survit a un flash
// de firmware (la RAM RTC n'est pas touchee par l'ecriture de la partition
// app) : si le magic reste identique apres une mise a jour qui change le
// layout, begin() relira les anciens octets bruts a travers le nouveau
// layout et produira des donnees corrompues (cf. incident Patch 7 -> 8).
static const uint32_t kMagic = 0xB007106Bu;  // v2 (Patch 8 : layout RuntimeStats/lastTask/wifiIp)

// Taille du buffer circulaire de logs — configurable via app_config.h
static const int kLines   = LOG_BUFFER_SIZE;
static const int kLineLen = LOG_LINE_MAX_LEN;

// Longueurs des petits champs texte conservés en RTC
static const int kTaskLen = 40;
static const int kIpLen   = 16;

struct BootLogRtcData {
    uint32_t magic;
    uint16_t count;                 // Nombre de lignes valides (<= kLines)
    uint16_t next;                  // Prochain index d'ecriture (ring)
    char     lines[kLines][kLineLen];

    char     lastTask[kTaskLen];    // Derniere tache signalee via setLastTask()

    uint32_t lastUptimeMs;          // Heartbeat le plus recent avant le prochain reboot
    RuntimeStats stats;              // Dernier instantane periodique (toutes les 30s)

    int8_t   wifiStatus;            // wl_status_t au dernier heartbeat
    int8_t   wifiRssi;
    char     wifiIp[kIpLen];

    float    temperatureC;          // Derniere lecture du capteur interne
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
        case ESP_RST_PANIC:     return "PANIC / exception non geree";
        case ESP_RST_INT_WDT:   return "WATCHDOG (interrupt)";
        case ESP_RST_TASK_WDT:  return "WATCHDOG (tache bloquee)";
        case ESP_RST_WDT:       return "WATCHDOG (autre)";
        case ESP_RST_DEEPSLEEP: return "Reveil deep sleep";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (chute de tension)";
        case ESP_RST_SDIO:      return "Reset SDIO";
        default:                return "Inconnue";
    }
}

// Reset "anormal" = a comptabiliser dans crash_count (par opposition a une
// mise sous tension normale ou un redemarrage volontaire demande par l'UI)
static bool _isCrashReason(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}

static uint32_t _largestFreeBlock() {
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

void BootLog::begin() {
    _mounted = LittleFS.begin(true);
    if (!_mounted) {
        Log::e(TAG, "Echec de montage LittleFS — journal de redemarrage indisponible (rien ne sera ecrit dans /bootlog.json)");
    }

    esp_reset_reason_t reason = esp_reset_reason();
    bool hadPreviousLines = (_rtc.magic == kMagic);   // count peut etre 0 si rien capture

    // --- Compteurs persistants (NVS — survivent aussi a une coupure secteur) ---
    Preferences prefs;
    prefs.begin("bootlog", false);
    uint32_t bootCount  = prefs.getUInt("boot_count", 0) + 1;
    uint32_t crashCount = prefs.getUInt("crash_count", 0);
    if (hadPreviousLines && _isCrashReason(reason)) crashCount++;
    prefs.putUInt("boot_count",  bootCount);
    prefs.putUInt("crash_count", crashCount);
    prefs.end();

    // --- Temperature interne (lue tot, avant que d'autres modules ne demarrent) ---
    float temperatureC = temperatureRead();

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
        entry["bootMs"]            = (uint32_t)millis();
        entry["resetReason"]       = _reasonText(reason);
        entry["resetReasonCode"]   = (int)reason;
        entry["bootCount"]         = bootCount;
        entry["crashCount"]        = crashCount;
        entry["temperature"]       = temperatureC;

        if (hadPreviousLines) {
            // Dernieres informations connues AVANT ce reboot (heartbeat le
            // plus recent du boot precedent) — le plus proche que l'on
            // puisse avoir de "l'etat au moment du crash"
            entry["uptimeAtResetMs"]   = _rtc.lastUptimeMs;
            entry["freeHeapAtReset"]   = _rtc.stats.freeHeap;
            entry["largestBlockAtReset"] = _rtc.stats.largestBlock;
            entry["lastTask"]          = _rtc.lastTask[0] ? String(_rtc.lastTask) : String("");
            entry["wifiStatus"]        = (int)_rtc.wifiStatus;
            entry["wifiRssi"]          = (int)_rtc.wifiRssi;
            entry["wifiIp"]            = String(_rtc.wifiIp);

            JsonObject stats = entry["lastStats"].to<JsonObject>();
            stats["uptime"]       = _rtc.stats.uptime;
            stats["freeHeap"]     = _rtc.stats.freeHeap;
            stats["largestBlock"] = _rtc.stats.largestBlock;
            stats["devicesCount"] = _rtc.stats.devicesCount;
            stats["pagesServed"]  = _rtc.stats.pagesServed;
            stats["apiCalls"]     = _rtc.stats.apiCalls;

            // Lit le ring buffer de logs dans l'ordre chronologique
            JsonArray lines = entry["lines"].to<JsonArray>();
            uint16_t start = (_rtc.count < kLines) ? 0 : _rtc.next;
            for (uint16_t i = 0; i < _rtc.count; i++) {
                uint16_t idx = (start + i) % kLines;
                lines.add(String(_rtc.lines[idx]));
            }
        } else {
            entry["lines"].to<JsonArray>();
        }

        while ((int)arr.size() > MAX_BOOT_LOG_ENTRIES)
            arr.remove(0);

        File out = LittleFS.open("/bootlog.json", "w");
        if (out) {
            serializeJson(doc, out);
            out.close();
        }
    }

    // Reinitialise entierement le buffer RTC pour le boot en cours, avant
    // le premier Log::* pour que ce message inaugure bien le nouveau buffer
    memset(&_rtc, 0, sizeof(_rtc));
    _rtc.magic        = kMagic;
    _rtc.temperatureC  = temperatureC;
    _rtc.wifiStatus    = (int8_t)WL_IDLE_STATUS;

    Log::i(TAG, "Raison du dernier reset : %s (%d) — boot #%u, crash #%u, %.1f C",
           _reasonText(reason), (int)reason, (unsigned)bootCount, (unsigned)crashCount, temperatureC);
}

void BootLog::service() {
    if (_rtc.magic != kMagic) return;   // begin() pas encore appele

    _rtc.lastUptimeMs = millis();

    if (_rtc.lastUptimeMs - _lastStatsSnapMs < BOOT_LOG_STATS_INTERVAL_MS && _lastStatsSnapMs != 0)
        return;
    _lastStatsSnapMs = _rtc.lastUptimeMs;

    _rtc.stats.uptime       = _rtc.lastUptimeMs;
    _rtc.stats.freeHeap     = ESP.getFreeHeap();
    _rtc.stats.largestBlock = _largestFreeBlock();
    _rtc.stats.devicesCount = _devicesCountProvider ? _devicesCountProvider() : 0;
    // pagesServed / apiCalls sont incrementes en continu par notePageServed()/noteApiCall()

    _rtc.wifiStatus = (int8_t)WiFi.status();
    _rtc.wifiRssi   = (WiFi.status() == WL_CONNECTED) ? (int8_t)WiFi.RSSI() : 0;
    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
    strncpy(_rtc.wifiIp, ip.c_str(), kIpLen - 1);

    _rtc.temperatureC = temperatureRead();
}

void BootLog::capture(const char* level, const char* tag, const char* msg) {
    if (_rtc.magic != kMagic) {
        // Buffer pas encore initialise (capture appelee avant begin()) — ignore
        return;
    }
    char* dst = _rtc.lines[_rtc.next];
    // Chaque ligne est un JSON compact auto-suffisant (timestamp, niveau,
    // tag, heap libre, plus gros bloc libre, message) — la mise en
    // securite des guillemets est faite automatiquement par ArduinoJson
    // lors de la serialisation finale de cette chaine dans getLogJson().
    snprintf(dst, kLineLen,
             "{\"t\":%lu,\"lvl\":\"%s\",\"tag\":\"%s\",\"heap\":%u,\"blk\":%u,\"msg\":\"%s\"}",
             (unsigned long)millis(), level, tag,
             (unsigned)ESP.getFreeHeap(), (unsigned)_largestFreeBlock(), msg);
    _rtc.next = (_rtc.next + 1) % kLines;
    if (_rtc.count < kLines) _rtc.count++;
}

void BootLog::setLastTask(const String& task) {
    if (_rtc.magic != kMagic) return;
    strncpy(_rtc.lastTask, task.c_str(), kTaskLen - 1);
    _rtc.lastTask[kTaskLen - 1] = '\0';
}

void BootLog::setDevicesCountProvider(std::function<uint32_t()> provider) {
    _devicesCountProvider = provider;
}

void BootLog::notePageServed() {
    if (_rtc.magic == kMagic) _rtc.stats.pagesServed++;
}

void BootLog::noteApiCall() {
    if (_rtc.magic == kMagic) _rtc.stats.apiCalls++;
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
