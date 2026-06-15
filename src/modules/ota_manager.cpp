#include "ota_manager.h"
#include <ArduinoOTA.h>
#include <Update.h>
#include "app_config.h"
#include "../../include/web_interface_ota.h"
#include "../utils/logger.h"

static const char* TAG = "OTA";

OtaManager otaMgr;

void OtaManager::begin(const char* hostname) {
#ifdef ENABLE_OTA
    ArduinoOTA.setHostname(hostname);

    ArduinoOTA.onStart([]() {
        Log::i(TAG, "ArduinoOTA: début");
    });
    ArduinoOTA.onEnd([]() {
        Log::i(TAG, "ArduinoOTA: terminé");
    });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        Log::d(TAG, "Progression: %u%%", p * 100 / t);
    });
    ArduinoOTA.onError([](ota_error_t err) {
        Log::e(TAG, "Erreur [%u]", (unsigned)err);
    });

    ArduinoOTA.begin();
    Log::i(TAG, "ArduinoOTA actif (hostname: %s)", hostname);
#endif
}

void OtaManager::loop() {
#ifdef ENABLE_OTA
    ArduinoOTA.handle();
#endif
}

void OtaManager::registerRoutes(WebServer& server) {
    // GET /update — page de mise à jour
    server.on("/update", HTTP_GET, [&server]() {
        server.send_P(200, "text/html", OTA_PAGE);
    });

    // POST /update — upload du firmware
    server.on("/update", HTTP_POST,
        [&server]() {
            server.sendHeader("Connection", "close");
            server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
            delay(500);
            ESP.restart();
        },
        [&server]() {
            HTTPUpload& upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                Log::i(TAG, "Web OTA: %s", upload.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    Log::i(TAG, "Web OTA: %u octets écrits", upload.totalSize);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );
}
