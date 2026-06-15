#pragma once
#include <WebServer.h>

// ---------------------------------------------------------------------------
// OtaManager — ArduinoOTA (réseau) + upload web OTA (/update)
// Portable : dépend uniquement de app_config.h (ENABLE_OTA)
// ---------------------------------------------------------------------------

class OtaManager {
public:
    // Configure et démarre ArduinoOTA
    void begin(const char* hostname);

    // À appeler dans loop() — ArduinoOTA.handle()
    void loop();

    // Enregistre les routes /update (GET + POST) sur le WebServer fourni
    void registerRoutes(WebServer& server);
};

extern OtaManager otaMgr;
