/**
 * DeviceStore — Persistance LittleFS des équipements connus
 *
 * Sauvegarde les NetworkDevice dans /devices.json entre les boots.
 * Au démarrage d'un scan, les devices connus sont chargés avec online=false.
 * En fin de scan, la liste complète (online + offline) est re-sauvegardée.
 */

#pragma once
#include <Arduino.h>
#include <vector>
#include "network_scanner.h"

class DeviceStore {
public:
    bool begin();
    std::vector<NetworkDevice> load();
    void save(const std::vector<NetworkDevice>& devices);
    bool isMounted() const { return _mounted; }

private:
    static constexpr const char* PATH = "/devices.json";
    bool _mounted = false;
};

extern DeviceStore deviceStore;
