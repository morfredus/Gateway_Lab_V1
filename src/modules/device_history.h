/**
 * DeviceHistory - Journal chronologique des evenements equipements
 *
 * Enregistre les evenements detectes a chaque scan :
 *   - "new"      : nouvel equipement jamais vu
 *   - "online"   : equipement reapparu apres une absence
 *   - "offline"  : equipement absent depuis le dernier scan
 *   - "changed"  : un champ a change de valeur (ip, manufacturer, category,
 *                  hostname, openPorts...) entre deux scans
 *
 * Persistance : tableau JSON dans /history.json sur LittleFS, capped a
 * MAX_ENTRIES pour rester dans les limites flash/RAM raisonnables - les
 * entrees les plus anciennes sont supprimees en premier (FIFO).
 *
 * Sert de base a la fois pour la "Vue chronologique" (page web /history)
 * et pour la "Detection des changements" (mise en evidence des nouveautes).
 */

#pragma once
#include <Arduino.h>
#include <vector>

struct HistoryEntry {
    uint32_t epoch;       // Horodatage (0 si l'heure n'etait pas synchronisee)
    String   mac;
    String   ip;
    String   label;       // Nom affiche (alias > hostname > ip)
    String   event;        // "new" | "online" | "offline" | "changed"
    String   field;        // Champ modifie (vide sauf event == "changed")
    String   oldValue;
    String   newValue;
};

class DeviceHistory {
public:
    bool begin();

    // Ajoute un evenement et persiste immediatement (best-effort)
    void addEvent(const String& mac, const String& ip, const String& label,
                  const String& event, const String& field = "",
                  const String& oldValue = "", const String& newValue = "");

    // Charge les evenements, du plus recent au plus ancien (limite a maxEntries, 0 = tous)
    std::vector<HistoryEntry> load(int maxEntries = 0) const;

    // Vide entierement le journal (utilise par la restauration)
    void clear();

private:
    static constexpr const char* PATH        = "/history.json";
    static constexpr int         MAX_ENTRIES = 300;
    bool _mounted = false;

    std::vector<HistoryEntry> _readAll() const;
    void _writeAll(const std::vector<HistoryEntry>& entries);
};

extern DeviceHistory deviceHistory;
