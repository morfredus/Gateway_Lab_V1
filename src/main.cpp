#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <ArduinoJson.h>

#include "secrets.h"
#include "app_config.h"
#include "board_config.h"

// ---------------------------------------------------------------------------
// OTA web page (embedded, served even if SPIFFS is empty)
// ---------------------------------------------------------------------------
static const char OTA_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="fr"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gateway Lab V1 - OTA</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',sans-serif;background:#0f172a;color:#e2e8f0;
     display:flex;flex-direction:column;align-items:center;padding:2rem 1rem}
h1{color:#06b6d4;margin-bottom:.5rem}
p.sub{color:#475569;font-size:.8rem;margin-bottom:2rem}
.card{background:#1e293b;border-radius:12px;padding:1.5rem;width:100%;
      max-width:420px;text-align:center}
label{display:block;color:#94a3b8;font-size:.875rem;margin-bottom:.75rem}
input[type=file]{width:100%;padding:.5rem;background:#0f172a;border:1px solid #334155;
                  border-radius:8px;color:#e2e8f0;margin-bottom:1rem}
.btn{display:block;width:100%;padding:.875rem;background:#0891b2;color:#fff;
     border:none;border-radius:10px;font-size:1rem;font-weight:600;cursor:pointer}
.btn:hover{background:#06b6d4}
.progress{width:100%;background:#334155;border-radius:8px;height:12px;
           display:none;margin-top:1rem;overflow:hidden}
.bar{height:100%;background:#06b6d4;width:0;transition:width .2s}
#msg{margin-top:1rem;font-size:.875rem;color:#94a3b8}
a{color:#06b6d4;font-size:.8rem;margin-top:1.5rem;display:block}
</style></head><body>
<h1>Gateway Lab V1</h1>
<p class="sub">Mise à jour OTA</p>
<div class="card">
  <form id="f">
    <label>Sélectionnez le firmware (.bin)</label>
    <input type="file" id="fw" accept=".bin">
    <button type="submit" class="btn">Mettre à jour</button>
  </form>
  <div class="progress" id="pg"><div class="bar" id="bar"></div></div>
  <p id="msg"></p>
</div>
<a href="/">← Retour</a>
<script>
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  var file=document.getElementById('fw').files[0];
  if(!file){document.getElementById('msg').textContent='Aucun fichier sélectionné.';return;}
  var fd=new FormData();fd.append('firmware',file);
  var xhr=new XMLHttpRequest();
  xhr.upload.onprogress=function(e){
    document.getElementById('pg').style.display='block';
    document.getElementById('bar').style.width=(e.loaded/e.total*100)+'%';
  };
  xhr.onload=function(){
    var m=document.getElementById('msg');
    if(xhr.status===200){m.style.color='#10b981';m.textContent='Succès — redémarrage en cours…';}
    else{m.style.color='#ef4444';m.textContent='Erreur : '+xhr.responseText;}
  };
  xhr.open('POST','/update');xhr.send(fd);
});
</script>
</body></html>
)HTML";

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WiFiMulti  wifiMulti;
WebServer  server(WEB_SERVER_PORT);

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
static void setupWiFi() {
    WiFi.mode(WIFI_STA);
    constexpr size_t N = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
    for (size_t i = 0; i < N; i++) {
        wifiMulti.addAP(WIFI_NETWORKS[i][0], WIFI_NETWORKS[i][1]);
    }

    Serial.print("WiFi: connexion en cours");
    unsigned long start = millis();
    while (wifiMulti.run() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.isConnected()) {
        Serial.printf("WiFi: connecté à \"%s\"\n", WiFi.SSID().c_str());
        Serial.printf("WiFi: IP = %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("WiFi: RSSI = %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("WiFi: échec de connexion");
    }
}

// ---------------------------------------------------------------------------
// ArduinoOTA (firmware update via PlatformIO / IDE)
// ---------------------------------------------------------------------------
#ifdef ENABLE_OTA
static void setupArduinoOTA() {
    ArduinoOTA.setHostname("gateway-lab-v1");

    ArduinoOTA.onStart([]() {
        Serial.println("OTA: début");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA: terminé");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA: %u%%\r", progress * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("OTA: erreur [%u]\n", err);
    });

    ArduinoOTA.begin();
    Serial.println("OTA: ArduinoOTA actif");
}
#endif

// ---------------------------------------------------------------------------
// Web server routes
// ---------------------------------------------------------------------------
static void handleRoot() {
    if (SPIFFS.exists("/index.html")) {
        File f = SPIFFS.open("/index.html", "r");
        server.streamFile(f, "text/html");
        f.close();
    } else {
        server.send(503, "text/plain",
            "SPIFFS vide. Lancez 'Upload Filesystem Image' depuis PlatformIO.");
    }
}

static void handleApiStatus() {
    JsonDocument doc;
    doc["ssid"]     = WiFi.SSID();
    doc["ip"]       = WiFi.localIP().toString();
    doc["rssi"]     = WiFi.RSSI();
    doc["uptime"]   = millis();
    doc["version"]  = PROJECT_VERSION;
    doc["hostname"] = "gateway-lab-v1";

    String json;
    serializeJson(doc, json);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
}

static void handleOtaGet() {
    server.send_P(200, "text/html", OTA_PAGE);
}

static void handleOtaUploadDone() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(500);
    ESP.restart();
}

static void handleOtaUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("OTA Web: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("OTA Web: %u octets écrits\n", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    }
}

static void setupWebServer() {
    server.on("/",        HTTP_GET,  handleRoot);
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/update",  HTTP_GET,  handleOtaGet);
    server.on("/update",  HTTP_POST, handleOtaUploadDone, handleOtaUpload);

    server.begin();
    Serial.printf("Web: serveur démarré sur le port %d\n", WEB_SERVER_PORT);
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.printf("\n=== %s v%s ===\n", PROJECT_NAME, PROJECT_VERSION);

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS: erreur de montage");
    }

    setupWiFi();

#ifdef ENABLE_OTA
    if (WiFi.isConnected()) setupArduinoOTA();
#endif

#ifdef ENABLE_MDNS
    if (WiFi.isConnected()) {
        if (MDNS.begin("gateway-lab-v1")) {
            Serial.println("mDNS: gateway-lab-v1.local actif");
        }
    }
#endif

#ifdef ENABLE_WEB_SERVER
    if (WiFi.isConnected()) setupWebServer();
#endif
}

void loop() {
#ifdef ENABLE_OTA
    ArduinoOTA.handle();
#endif

    // Reconnexion automatique via WiFiMulti
    if (WiFi.status() != WL_CONNECTED) {
        wifiMulti.run();
    }

    server.handleClient();
}
