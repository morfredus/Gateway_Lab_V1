/**
 * DeviceHistory - Implementation
 *
 * Le fichier est entierement relu/reecrit a chaque ajout (FIFO simple).
 * MAX_ENTRIES (app_config.h) borne le volume relu/reecrit a chaque appel —
 * en mode degrade (systemHealth.isDegraded()), les nouveaux evenements sont
 * refuses pour ne pas aggraver la pression memoire.
 */

#include "device_history.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <algorithm>
#include "time_sync.h"
#include "system_health.h"
#include "../utils/logger.h"

static const char* TAG = "History";

DeviceHistory deviceHistory;

bool DeviceHistory::begin() {
    _mounted = LittleFS.begin(true);
    return _mounted;
}

std::vector<HistoryEntry> DeviceHistory::_readAll() const {
    std::vector<HistoryEntry> result;
    if (!_mounted) return result;

    File f = LittleFS.open(PATH, "r");
    if (!f) return result;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return result;

    for (JsonObject obj : doc.as<JsonArray>()) {
        HistoryEntry e;
        e.epoch    = obj["epoch"]    | 0;
        e.mac      = obj["mac"]      | "";
        e.ip       = obj["ip"]       | "";
        e.label    = obj["label"]    | "";
        e.event    = obj["event"]    | "";
        e.field    = obj["field"]    | "";
        e.oldValue = obj["oldValue"] | "";
        e.newValue = obj["newValue"] | "";
        result.push_back(e);
    }
    return result;
}

void DeviceHistory::_writeAll(const std::vector<HistoryEntry>& entries) {
    if (!_mounted) return;

    File f = LittleFS.open(PATH, "w");
    if (!f) {
        Log::e(TAG, "Impossible d'ecrire %s", PATH);
        return;
    }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& e : entries) {
        JsonObject obj = arr.add<JsonObject>();
        obj["epoch"]    = e.epoch;
        obj["mac"]      = e.mac;
        obj["ip"]       = e.ip;
        obj["label"]    = e.label;
        obj["event"]    = e.event;
        obj["field"]    = e.field;
        obj["oldValue"] = e.oldValue;
        obj["newValue"] = e.newValue;
    }
    serializeJson(doc, f);
    f.close();
}

void DeviceHistory::addEvent(const String& mac, const String& ip, const String& label,
                              const String& event, const String& field,
                              const String& oldValue, const String& newValue) {
    if (!_mounted) return;
    if (systemHealth.isDegraded()) {
        Log::w(TAG, "Evenement ignore — mode degrade (%s)", systemHealth.reason().c_str());
        return;
    }

    auto entries = _readAll();

    HistoryEntry e;
    e.epoch    = timeSync.nowEpoch();
    e.mac      = mac;
    e.ip       = ip;
    e.label    = label;
    e.event    = event;
    e.field    = field;
    e.oldValue = oldValue;
    e.newValue = newValue;
    entries.push_back(e);

    // FIFO : retire les plus anciennes si la limite est depassee
    while ((int)entries.size() > MAX_ENTRIES)
        entries.erase(entries.begin());

    _writeAll(entries);
}

std::vector<HistoryEntry> DeviceHistory::load(int maxEntries) const {
    auto entries = _readAll();
    // Plus recent en premier
    std::reverse(entries.begin(), entries.end());
    if (maxEntries > 0 && (int)entries.size() > maxEntries)
        entries.resize(maxEntries);
    return entries;
}

void DeviceHistory::clear() {
    if (!_mounted) return;
    LittleFS.remove(PATH);
    Log::i(TAG, "Journal d'historique vide");
}
