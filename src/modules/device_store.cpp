/**
 * DeviceStore — Implémentation
 *
 * Format de stockage : tableau JSON dans /devices.json sur LittleFS
 * Chaque objet : ip, mac, manufacturer, hostname, category, type, model, os, source, services
 *
 * Limitation mémoire : ArduinoJson v7 alloue dynamiquement.
 * Un réseau /24 complet (254 devices) = environ 50 Ko de JSON.
 * L'ESP32-S3 avec 8 Mo PSRAM supporte largement.
 */

#include "device_store.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../utils/logger.h"

static const char* TAG = "Store";

DeviceStore deviceStore;

bool DeviceStore::begin() {
    if (!LittleFS.begin(true)) {
        Log::e(TAG, "LittleFS : montage échoué (formatage tenté)");
        _mounted = false;
        return false;
    }
    _mounted = true;
    Log::i(TAG, "LittleFS monté — espace total %u Ko, libre %u Ko",
           (unsigned)(LittleFS.totalBytes() / 1024),
           (unsigned)(LittleFS.usedBytes() == 0 ? LittleFS.totalBytes() / 1024
                                                 : (LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024));
    return true;
}

std::vector<NetworkDevice> DeviceStore::load() {
    std::vector<NetworkDevice> result;
    if (!_mounted) return result;

    File f = LittleFS.open(PATH, "r");
    if (!f) {
        Log::i(TAG, "Pas de fichier %s — premier démarrage", PATH);
        return result;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Log::w(TAG, "JSON invalide dans %s : %s", PATH, err.c_str());
        return result;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        NetworkDevice d;
        d.ip           = obj["ip"]           | "";
        d.mac          = obj["mac"]          | "";
        d.manufacturer = obj["manufacturer"] | "";
        d.hostname     = obj["hostname"]     | "";
        d.category     = obj["category"]     | "";
        d.type         = obj["type"]         | "";
        d.model        = obj["model"]        | "";
        d.os           = obj["os"]           | "";
        d.source       = obj["source"]       | "";
        d.services     = obj["services"]     | "";
        d.openPorts    = obj["openPorts"]    | "";
        d.alias            = obj["alias"]        | "";
        d.firstSeenEpoch   = obj["firstSeen"]     | 0;
        d.lastSeenEpoch    = obj["lastSeenAt"]    | 0;
        d.seenCount        = obj["seenCount"]     | 0;
        d.favorite         = obj["favorite"]      | false;
        JsonArray notesArr = obj["notes"].as<JsonArray>();
        for (JsonObject no : notesArr) {
            DeviceNote n;
            n.ts   = no["ts"]   | 0;
            n.text = no["text"] | "";
            d.notes.push_back(n);
        }
        // Surveillance continue / stabilite (v1.0.0)
        d.presenceCount       = obj["presenceCount"]       | 0;
        d.absenceCount        = obj["absenceCount"]        | 0;
        d.reconnectionCount   = obj["reconnectionCount"]   | 0;
        d.lastDisconnectEpoch = obj["lastDisconnectEpoch"] | 0;
        d.totalOnlineSeconds  = obj["totalOnlineSeconds"]  | 0;
        d.totalOfflineSeconds = obj["totalOfflineSeconds"] | 0;
        d.mobilityOverride    = obj["mobilityOverride"]    | "";
        d.mobileAwayNotified  = obj["mobileAwayNotified"]  | false;
        d.topologyParent      = obj["topologyParent"]      | "";
        d.topologyParentAuto  = obj["topologyParentAuto"]  | false;
        d.topologyParentConfidence = obj["topologyParentConfidence"] | 0;
        d.online       = false;
        d.lastSeen     = 0;   // Inconnu — sera affiché comme "hors ligne"
        if (!d.ip.isEmpty() || !d.mac.isEmpty())
            result.push_back(d);
    }

    Log::i(TAG, "%u équipement(s) chargés depuis %s", (unsigned)result.size(), PATH);
    return result;
}

void DeviceStore::save(const std::vector<NetworkDevice>& devices) {
    if (!_mounted) return;

    File f = LittleFS.open(PATH, "w");
    if (!f) {
        Log::e(TAG, "Impossible d'écrire %s", PATH);
        return;
    }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& d : devices) {
        if (d.ip.isEmpty() && d.mac.isEmpty()) continue;
        JsonObject obj = arr.add<JsonObject>();
        obj["ip"]           = d.ip;
        obj["mac"]          = d.mac;
        obj["manufacturer"] = d.manufacturer;
        obj["hostname"]     = d.hostname;
        obj["category"]     = d.category;
        obj["type"]         = d.type;
        obj["model"]        = d.model;
        obj["os"]           = d.os;
        obj["source"]       = d.source;
        obj["services"]     = d.services;
        obj["openPorts"]    = d.openPorts;
        obj["alias"]        = d.alias;
        obj["firstSeen"]    = d.firstSeenEpoch;
        obj["lastSeenAt"]   = d.lastSeenEpoch;
        obj["seenCount"]    = d.seenCount;
        obj["online"]       = d.online;
        obj["favorite"]     = d.favorite;
        JsonArray notesArr = obj["notes"].to<JsonArray>();
        for (const auto& n : d.notes) {
            JsonObject no = notesArr.add<JsonObject>();
            no["ts"]   = n.ts;
            no["text"] = n.text;
        }
        // Surveillance continue / stabilite (v1.0.0)
        obj["presenceCount"]       = d.presenceCount;
        obj["absenceCount"]        = d.absenceCount;
        obj["reconnectionCount"]   = d.reconnectionCount;
        obj["lastDisconnectEpoch"] = d.lastDisconnectEpoch;
        obj["totalOnlineSeconds"]  = d.totalOnlineSeconds;
        obj["totalOfflineSeconds"] = d.totalOfflineSeconds;
        obj["mobilityOverride"]    = d.mobilityOverride;
        obj["mobileAwayNotified"]  = d.mobileAwayNotified;
        obj["topologyParent"]      = d.topologyParent;
        obj["topologyParentAuto"]  = d.topologyParentAuto;
        obj["topologyParentConfidence"] = d.topologyParentConfidence;
    }

    serializeJson(doc, f);
    f.close();
    Log::i(TAG, "%u équipement(s) sauvegardés dans %s", (unsigned)devices.size(), PATH);
}
