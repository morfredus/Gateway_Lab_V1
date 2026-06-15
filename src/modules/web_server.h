#pragma once
#include <Arduino.h>
#include <functional>

// ---------------------------------------------------------------------------
// WebServerModule — WebServer HTTP port 80 + mDNS
// Découplé des autres modules via ScanProvider
// Portable : dépend de web_interface*.h, app_config.h, ArduinoJson
// ---------------------------------------------------------------------------

struct ScanProvider {
    std::function<bool()>   isScanning;      // scanner occupé ?
    std::function<String()> getJson;         // résultats JSON
    std::function<void()>   triggerScan;     // déclencher un scan
};

class WebServerModule {
public:
    // Enregistre le fournisseur de données scanner — avant begin()
    void registerScanProvider(ScanProvider p);

    // Démarre le WebServer et mDNS
    void begin(uint16_t port = 80);

    // À appeler dans loop()
    void loop();

private:
    void _handleRoot();
    void _handleApiStatus();
    void _handleApiDevices();
    void _handleApiScanTrigger();
    void _handleNotFound();

    ScanProvider _scan;
    bool         _hasScan = false;
};

extern WebServerModule webSrv;
