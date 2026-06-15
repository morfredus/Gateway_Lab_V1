#include <Arduino.h>
#include "app_config.h"
#include "board_config.h"
#include "utils/logger.h"
#include "modules/wifi_manager.h"
#include "modules/ota_manager.h"
#include "modules/web_server.h"
#include "modules/network_scanner.h"

void setup() {
    Serial.begin(115200);
    Log::i("Main", "=== %s v%s ===", PROJECT_NAME, PROJECT_VERSION);

    wifiMgr.begin([](bool connected) {
        if (!connected) return;

#ifdef ENABLE_OTA
        otaMgr.begin(MDNS_HOSTNAME);
#endif

        netScanner.begin();

#ifdef ENABLE_WEB_SERVER
        webSrv.registerScanProvider({
            .isScanning  = [] { return netScanner.isScanRunning(); },
            .getJson     = [] { return netScanner.resultsToJson(); },
            .triggerScan = [] { netScanner.startScan(); },
        });
        webSrv.begin(WEB_SERVER_PORT);
#endif
    });
}

void loop() {
    wifiMgr.loop();
#ifdef ENABLE_OTA
    otaMgr.loop();
#endif
#ifdef ENABLE_WEB_SERVER
    webSrv.loop();
#endif
}
