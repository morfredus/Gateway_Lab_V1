#pragma once
#include <Arduino.h>
#include <vector>

// ---------------------------------------------------------------------------
// NetworkScanner — découverte des équipements LAN
// Sweep UDP du sous-réseau + lecture table ARP lwIP, tâche FreeRTOS async
// Portable : dépend uniquement de WiFi.h et lwIP (esp32 Arduino)
// ---------------------------------------------------------------------------

struct HostInfo {
    String   ip;
    String   mac;
    String   vendor;        // dérivé des 3 premiers octets MAC (OUI)
    uint32_t lastSeenMs;    // millis() au moment de la découverte
};

class NetworkScanner {
public:
    // Initialise le mutex et la liste. Appeler après WiFi connecté.
    void begin();

    // Lance un scan async (FreeRTOS task Core 0). Sans effet si déjà en cours.
    void startScan();

    bool isScanRunning() const;

    // Retourne une copie thread-safe des résultats courants
    std::vector<HostInfo> getResults() const;

    // Sérialise en JSON (thread-safe)
    String resultsToJson() const;

private:
    static void _task(void* self);
    void        _run();             // corps de la tâche
    void        _sweepSubnet();     // envoi UDP pour déclencher ARP
    void        _readArpTable();    // lecture table ARP lwIP

    SemaphoreHandle_t      _mutex      = nullptr;
    TaskHandle_t           _taskHandle = nullptr;
    std::vector<HostInfo>  _results;
    volatile bool          _scanning   = false;
};

extern NetworkScanner netScanner;
