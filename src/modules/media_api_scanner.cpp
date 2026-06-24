#include "media_api_scanner.h"
#include "../utils/logger.h"
#include <WiFiClient.h>

static const char* TAG = "MediaApi";

MediaApiScanner mediaApiScanner;

// ---------------------------------------------------------------------------
// GET HTTP générique — lit jusqu'à maxLen octets de corps de réponse
// ---------------------------------------------------------------------------
static String _httpGet(const String& ip, uint16_t port, const String& path,
                        uint32_t timeout_ms, size_t maxLen = 2048) {
    WiFiClient client;
    client.setTimeout(1);
    if (!client.connect(ip.c_str(), port, timeout_ms)) return "";

    client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: GatewayLab\r\nConnection: close\r\n\r\n",
                  path.c_str(), ip.c_str());

    String body;
    unsigned long start = millis();
    bool inBody = false;
    while (client.connected() && millis() - start < timeout_ms) {
        if (!client.available()) { delay(5); continue; }
        String line = client.readStringUntil('\n');
        if (!inBody) {
            String trimmed = line; trimmed.trim();
            if (trimmed.isEmpty()) inBody = true;
            continue;
        }
        body += line;
        if (body.length() > maxLen) break;
    }
    client.stop();
    return body;
}

static String _extractJsonField(const String& body, const char* key) {
    String needle = String("\"") + key + "\"";
    int kpos = body.indexOf(needle);
    if (kpos < 0) return "";
    int colon = body.indexOf(':', kpos + needle.length());
    if (colon < 0) return "";
    int start = colon + 1;
    while (start < (int)body.length() && (body[start] == ' ' || body[start] == '"')) start++;
    int end = start;
    while (end < (int)body.length() && body[end] != '"' && body[end] != ',' && body[end] != '}') end++;
    String val = body.substring(start, end);
    val.trim();
    return val;
}

static String _extractXmlTag(const String& body, const char* tag) {
    String open  = String("<") + tag + ">";
    String close = String("</") + tag + ">";
    int s = body.indexOf(open);
    if (s < 0) return "";
    s += open.length();
    int e = body.indexOf(close, s);
    if (e < 0) return "";
    String val = body.substring(s, e);
    val.trim();
    return val;
}

MediaApiResult MediaApiScanner::probe(const String& ip, uint32_t timeout_ms) {
    MediaApiResult res;

    // ── Google Cast / Chromecast : :8008/setup/eureka_info ───────────────
    String body = _httpGet(ip, 8008, "/setup/eureka_info", timeout_ms);
    if (body.indexOf("\"cast_build_revision\"") >= 0 || body.indexOf("\"ssdp_udn\"") >= 0) {
        res.apiType      = "Cast";
        res.manufacturer = "Google";
        res.model        = _extractJsonField(body, "model_name");
        res.friendlyName = _extractJsonField(body, "name");
        res.category      = "Streaming";
        Log::i(TAG, "%s : Cast détecté (%s)", ip.c_str(), res.model.c_str());
        return res;
    }

    // ── Sonos : :1400/xml/device_description.xml ─────────────────────────
    body = _httpGet(ip, 1400, "/xml/device_description.xml", timeout_ms);
    if (body.indexOf("Sonos") >= 0 || body.indexOf("<manufacturer>") >= 0) {
        res.apiType      = "Sonos";
        res.manufacturer = "Sonos";
        res.model        = _extractXmlTag(body, "modelName");
        res.friendlyName = _extractXmlTag(body, "roomName");
        if (res.friendlyName.isEmpty()) res.friendlyName = _extractXmlTag(body, "friendlyName");
        res.category      = "Speaker";
        Log::i(TAG, "%s : Sonos détecté (%s)", ip.c_str(), res.model.c_str());
        return res;
    }

    // ── Roku : :8060/query/device-info ────────────────────────────────────
    body = _httpGet(ip, 8060, "/query/device-info", timeout_ms);
    if (body.indexOf("<device-info>") >= 0 || body.indexOf("roku") >= 0) {
        res.apiType      = "Roku";
        res.manufacturer = "Roku";
        res.model        = _extractXmlTag(body, "model-name");
        res.friendlyName = _extractXmlTag(body, "friendly-device-name");
        res.category      = "Streaming";
        Log::i(TAG, "%s : Roku détecté (%s)", ip.c_str(), res.model.c_str());
        return res;
    }

    // ── Samsung Smart TV (Tizen) : :8001/api/v2/ ──────────────────────────
    body = _httpGet(ip, 8001, "/api/v2/", timeout_ms);
    if (body.indexOf("\"device\"") >= 0 && body.indexOf("Samsung") >= 0) {
        res.apiType      = "SamsungTV";
        res.manufacturer = "Samsung";
        res.model        = _extractJsonField(body, "modelName");
        res.friendlyName = _extractJsonField(body, "name");
        res.category      = "TV";
        Log::i(TAG, "%s : Samsung TV détecté (%s)", ip.c_str(), res.model.c_str());
        return res;
    }

    return res;
}
