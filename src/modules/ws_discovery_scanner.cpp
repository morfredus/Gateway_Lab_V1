#include "ws_discovery_scanner.h"
#include "../utils/logger.h"
#include <WiFiUdp.h>

static const char* TAG = "WSDisco";

static const char* WSD_ADDR = "239.255.255.250";
static constexpr uint16_t WSD_PORT = 3702;

WsDiscoveryScanner wsDiscoveryScanner;

// ---------------------------------------------------------------------------
// Extraction d'un tag XML, tolerante aux namespaces (<d:Types>, <wsa:Address>…)
// ---------------------------------------------------------------------------
String WsDiscoveryScanner::_xmlTag(const String& xml, const char* tag) {
    String needle = String("<") + tag;
    int s = xml.indexOf(needle);
    if (s < 0) {
        // Essai avec namespace générique : cherche ":tag>" pour ignorer le préfixe
        String alt = String(":") + tag + ">";
        int a = xml.indexOf(alt);
        if (a < 0) return "";
        s = xml.lastIndexOf('<', a) ;
        if (s < 0) return "";
    }
    int gt = xml.indexOf('>', s);
    if (gt < 0) return "";
    int closeStart = xml.indexOf("</", gt);
    if (closeStart < 0) return "";
    String val = xml.substring(gt + 1, closeStart);
    val.trim();
    return val;
}

// Extrait la première IPv4 trouvée dans une liste d'URLs XAddrs
String WsDiscoveryScanner::_ipFromXAddrs(const String& xaddrs) {
    int httpPos = xaddrs.indexOf("http://");
    if (httpPos < 0) return "";
    int start = httpPos + 7;
    int end = start;
    while (end < (int)xaddrs.length() && xaddrs[end] != ':' && xaddrs[end] != '/' && xaddrs[end] != ' ') end++;
    String host = xaddrs.substring(start, end);
    // Validation simple : 4 segments numeriques
    int dots = 0;
    for (size_t i = 0; i < host.length(); i++) if (host[i] == '.') dots++;
    if (dots != 3) return "";
    return host;
}

String WsDiscoveryScanner::_inferCategory(const String& types) {
    String t = types; t.toLowerCase();
    if (t.indexOf("networkvideotransmitter") >= 0 || t.indexOf("videosource") >= 0)
        return "Camera";
    if (t.indexOf("printdevicetype") >= 0 || t.indexOf("print") >= 0)
        return "Printer";
    if (t.indexOf("scandevicetype") >= 0)
        return "Printer";
    if (t.indexOf("networkvideostorage") >= 0)
        return "NAS";
    return "";
}

std::map<String, WsDiscoveryInfo> WsDiscoveryScanner::scan(uint32_t timeout_ms) {
    std::map<String, WsDiscoveryInfo> results;

    WiFiUDP udp;
    if (!udp.begin(0)) {
        Log::e(TAG, "Impossible d'ouvrir le socket UDP");
        return results;
    }

    // Requete SOAP "Probe" — minimaliste, sans filtre de types pour
    // recuperer tout appareil ONVIF/WS-Discovery compatible (camera,
    // imprimante, NAS…) quel que soit son role.
    const char* probe =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
        "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">"
        "<e:Header>"
        "<w:MessageID>uuid:gatewaylab-0001</w:MessageID>"
        "<w:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
        "<w:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
        "</e:Header>"
        "<e:Body><d:Probe/></e:Body>"
        "</e:Envelope>";

    udp.beginPacket(WSD_ADDR, WSD_PORT);
    udp.print(probe);
    if (!udp.endPacket()) {
        Log::e(TAG, "Envoi Probe WS-Discovery échoué");
        udp.stop();
        return results;
    }
    Log::i(TAG, "Probe WS-Discovery envoyé → %s:%d", WSD_ADDR, WSD_PORT);

    uint32_t deadline = millis() + timeout_ms;
    while (millis() < deadline) {
        int pktSize = udp.parsePacket();
        if (pktSize > 0) {
            String body;
            body.reserve(pktSize);
            while (udp.available()) body += (char)udp.read();

            if (body.indexOf("ProbeMatch") < 0) continue;

            String xaddrs = _xmlTag(body, "XAddrs");
            String ip = _ipFromXAddrs(xaddrs);
            if (ip.isEmpty()) {
                // Repli : adresse source du paquet UDP lui-meme
                ip = udp.remoteIP().toString();
            }
            if (ip.isEmpty()) continue;

            WsDiscoveryInfo info;
            info.types    = _xmlTag(body, "Types");
            info.scopes   = _xmlTag(body, "Scopes");
            info.category = _inferCategory(info.types);

            results[ip] = info;
            Log::i(TAG, "ProbeMatch %s — types=%s", ip.c_str(), info.types.c_str());
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    udp.stop();
    return results;
}
