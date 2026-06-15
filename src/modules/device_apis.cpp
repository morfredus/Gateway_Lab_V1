/**
 * DeviceApis — Implémentation (Niveau 5)
 *
 * Bibliothèques utilisées :
 *   WiFiClient   — connexion TCP pour les appels HTTP sans dépendance externe
 *   ArduinoJson  — parsing des réponses JSON des APIs propriétaires
 *   FreeRTOS     — vTaskDelay() pour céder le CPU pendant les I/O réseau
 */

#include "device_apis.h"
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "../utils/logger.h"

static const char* TAG = "DevAPI";

// ---------------------------------------------------------------------------
// Requête HTTP GET minimaliste (pas de HTTPClient pour contrôler les timeouts)
// Retourne le corps de la réponse ou "" en cas d'erreur
// ---------------------------------------------------------------------------
String DeviceApis::_httpGet(const String& ip, uint16_t port, const String& path) {
    WiFiClient client;
    client.setTimeout(DEVICE_API_TIMEOUT_MS / 1000);

    if (!client.connect(ip.c_str(), port)) {
        Log::d(TAG, "Connect failed : %s:%u%s", ip.c_str(), port, path.c_str());
        return "";
    }

    client.print("GET " + path + " HTTP/1.0\r\n"
                 "Host: " + ip + ":" + String(port) + "\r\n"
                 "Connection: close\r\n\r\n");

    uint32_t t0 = millis();
    while (client.connected() && !client.available()) {
        if (millis() - t0 > (uint32_t)DEVICE_API_TIMEOUT_MS) { client.stop(); return ""; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Lecture limitée à 4 Ko pour économiser la RAM
    String resp;
    resp.reserve(2048);
    while (client.available() && resp.length() < 4096) {
        resp += (char)client.read();
    }
    client.stop();

    // Retourner uniquement le corps (après les en-têtes HTTP)
    int bodyIdx = resp.indexOf("\r\n\r\n");
    if (bodyIdx >= 0) return resp.substring(bodyIdx + 4);
    bodyIdx = resp.indexOf("\n\n");
    if (bodyIdx >= 0) return resp.substring(bodyIdx + 2);
    return resp;
}

// ---------------------------------------------------------------------------
// Philips Hue Bridge — GET /api/config
//
// Endpoint public (sans token) depuis Hue Bridge firmware 1.x et 2.x.
// Retourne un JSON plat avec : name, swversion, modelid, mac, bridgeid…
// ---------------------------------------------------------------------------
bool DeviceApis::enrichHueBridge(NetworkDevice& dev) {
    Log::i(TAG, "Hue Bridge API : %s", dev.ip.c_str());
    String body = _httpGet(dev.ip, 80, "/api/config");
    if (body.isEmpty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Log::w(TAG, "Hue /api/config JSON invalide");
        return false;
    }

    const char* name    = doc["name"]      | "";
    const char* swver   = doc["swversion"] | "";
    const char* modelid = doc["modelid"]   | "";

    dev.manufacturer = "Philips Hue";
    dev.category     = "SmartHub";
    dev.source       = "HueAPI";

    // Déduction du modèle depuis le modelid officiel Philips
    String mid = modelid;
    mid.toUpperCase();
    if (mid.startsWith("BSB002"))      dev.model = "Hue Bridge v2";
    else if (mid.startsWith("BSB001")) dev.model = "Hue Bridge v1";
    else if (!mid.isEmpty())           dev.model = String("Hue Bridge (") + modelid + ")";
    else                               dev.model = "Hue Bridge";

    if (strlen(swver) > 0) dev.os = String("FW ") + swver;
    if (strlen(name)  > 0 && dev.hostname.isEmpty()) dev.hostname = name;

    Log::i(TAG, "Hue enrichi : model=%s fw=%s name=%s",
           dev.model.c_str(), swver, name);
    return true;
}

// ---------------------------------------------------------------------------
// Synology DSM — API publique (sans authentification)
//
// Endpoints testés (ordre de préférence) :
//   1. /webapi/entry.cgi?api=SYNO.DSM.Info&version=7&method=getinfo   (DSM 7.x)
//   2. /webapi/query.cgi?api=SYNO.DSM.Info&version=2&method=getinfo   (DSM 6.x)
//
// Note : sur DSM récent ces endpoints peuvent exiger l'authentification.
//        En cas de refus ({"success":false}), on renseigne les champs
//        connus depuis la détection SSDP/OUI et on documente la limitation.
// ---------------------------------------------------------------------------
bool DeviceApis::enrichSynologyNas(NetworkDevice& dev) {
    Log::i(TAG, "Synology DSM API : %s", dev.ip.c_str());

    // Essai port 5000 (HTTP par défaut de DSM) puis port 80
    constexpr uint16_t PORTS[] = { 5000, 80 };
    constexpr const char* PATHS[] = {
        "/webapi/entry.cgi?api=SYNO.DSM.Info&version=7&method=getinfo",
        "/webapi/query.cgi?api=SYNO.DSM.Info&version=2&method=getinfo"
    };

    String body;
    for (uint16_t port : PORTS) {
        for (const char* path : PATHS) {
            body = _httpGet(dev.ip, port, path);
            if (!body.isEmpty()) break;
        }
        if (!body.isEmpty()) break;
    }

    dev.manufacturer = "Synology";
    dev.category     = "NAS";
    dev.source       = "SynologyAPI";

    if (!body.isEmpty()) {
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok && doc["success"]) {
            const char* m = doc["data"]["model"]              | "";
            const char* v = doc["data"]["version"]["string"]  | "";
            if (strlen(m) > 0) dev.model = m;
            if (strlen(v) > 0) dev.os    = String("DSM ") + v;
        }
        // Si l'API retourne {"success":false} → auth requise, on ne plante pas
    }

    // Valeurs par défaut si les endpoints ont refusé la requête
    if (dev.model.isEmpty()) dev.model = "DiskStation";
    if (dev.os.isEmpty())    dev.os    = "DSM";

    Log::i(TAG, "Synology enrichi : model=%s os=%s",
           dev.model.c_str(), dev.os.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Freebox — APIs publiques non authentifiées
//
// /api_version : toujours accessible, retourne modèle, version API, nom
// /api/vX/system/ : accessible sans auth sur FreeboxOS >= 4 (certains modèles)
// ---------------------------------------------------------------------------
bool DeviceApis::enrichFreebox(NetworkDevice& dev) {
    Log::i(TAG, "Freebox API : %s", dev.ip.c_str());

    // Endpoint /api_version : TOUJOURS public (aucune auth requise)
    String body = _httpGet(dev.ip, 80, "/api_version");
    if (body.isEmpty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Log::w(TAG, "Freebox /api_version JSON invalide");
        return false;
    }

    const char* boxModel   = doc["box_model"]      | "";
    const char* boxModelN  = doc["box_model_name"] | "";
    const char* apiVersion = doc["api_version"]    | "";
    const char* deviceName = doc["device_name"]    | "";
    const char* apiBaseUrl = doc["api_base_url"]   | "/api/";

    dev.manufacturer = "Free";
    dev.category     = "Router";
    dev.os           = "FreeboxOS";
    dev.source       = "FreeboxAPI";

    // Déduction du modèle depuis box_model_name ou box_model
    String mraw = strlen(boxModelN) > 0 ? String(boxModelN) : String(boxModel);
    String mlow = mraw; mlow.toLowerCase();
    if      (mlow.indexOf("ultra")     >= 0) dev.model = "Freebox Ultra";
    else if (mlow.indexOf("pop")       >= 0) dev.model = "Freebox Pop";
    else if (mlow.indexOf("revolution") >= 0 ||
             mlow.indexOf("révolution") >= 0) dev.model = "Freebox Révolution";
    else if (mlow.indexOf("delta")     >= 0) dev.model = "Freebox Delta";
    else if (mlow.indexOf("one")       >= 0) dev.model = "Freebox One";
    else if (!mraw.isEmpty())                dev.model = mraw;
    else                                     dev.model = "Freebox Server";

    if (strlen(deviceName) > 0 && dev.hostname.isEmpty()) dev.hostname = deviceName;
    if (strlen(apiVersion) > 0)
        dev.os = String("FreeboxOS (API v") + apiVersion + ")";

    // Tentative d'enrichissement via /api/vX/system/ (peut nécessiter auth)
    // On construit le chemin depuis api_base_url + "vX/system/"
    if (strlen(apiVersion) > 0 && strlen(apiBaseUrl) > 0) {
        String verMajor = String(apiVersion);
        int dot = verMajor.indexOf('.');
        if (dot > 0) verMajor = verMajor.substring(0, dot);

        String sysPath = String(apiBaseUrl) + "v" + verMajor + "/system/";
        String sysBody = _httpGet(dev.ip, 80, sysPath);
        if (!sysBody.isEmpty()) {
            JsonDocument sysDoc;
            if (deserializeJson(sysDoc, sysBody) == DeserializationError::Ok &&
                sysDoc["success"]) {
                const char* fw = sysDoc["result"]["firmware_version"] | "";
                if (strlen(fw) > 0) dev.os = String("FreeboxOS ") + fw;
            }
        }
    }

    Log::i(TAG, "Freebox enrichi : model=%s os=%s", dev.model.c_str(), dev.os.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Sélecteur automatique : applique le bon module selon le device détecté
// ---------------------------------------------------------------------------
bool DeviceApis::enrichIfApplicable(NetworkDevice& dev) {
    String cat = dev.category; cat.toLowerCase();
    String mfr = dev.manufacturer; mfr.toLowerCase();
    String mdl = dev.model; mdl.toLowerCase();
    String hn  = dev.hostname; hn.toLowerCase();

    // Philips Hue Bridge
    if (cat == "smarthub" ||
        mfr.indexOf("philips") >= 0 || mfr.indexOf("signify") >= 0 ||
        mdl.indexOf("hue") >= 0 || hn.indexOf("hue bridge") >= 0) {
        return enrichHueBridge(dev);
    }

    // Synology NAS
    if (cat == "nas" ||
        mfr.indexOf("synology") >= 0 ||
        mdl.indexOf("diskstation") >= 0 ||
        hn.indexOf("synology") >= 0) {
        return enrichSynologyNas(dev);
    }

    // Freebox
    if (mfr.indexOf("free") >= 0 ||
        mdl.indexOf("freebox") >= 0 ||
        hn.indexOf("freebox") >= 0) {
        return enrichFreebox(dev);
    }

    return false;
}
