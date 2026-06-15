#pragma once
#include <Arduino.h>
#include <functional>

// ---------------------------------------------------------------------------
// WiFiManager — gestion WiFiMulti + reconnexion automatique
// Portable : dépend uniquement de secrets.h et app_config.h
// ---------------------------------------------------------------------------

class WiFiManager {
public:
    using Callback = std::function<void(bool connected)>;

    // Initialise et tente la connexion ; appelle cb(true/false) à l'issue
    void begin(Callback cb = nullptr);

    // À appeler dans loop() — gère la reconnexion automatique
    void loop();

    bool    isConnected() const;
    String  ssid()        const;
    String  localIP()     const;
    int8_t  rssi()        const;
    String  hostname()    const;
};

extern WiFiManager wifiMgr;
