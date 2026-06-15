/**
 * SSDPScanner — Implémentation (Niveau 4)
 *
 * Bibliothèques utilisées :
 *   WiFiUDP    — envoi/réception du M-SEARCH multicast UDP
 *   WiFiClient — connexion HTTP pour récupérer les descriptions XML
 *   FreeRTOS   — vTaskDelay() pour céder le CPU pendant les I/O
 */

#include "ssdp_scanner.h"
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include "../utils/logger.h"

static const char* TAG = "SSDP";

// Instance globale exportée
SSDPScanner ssdpScanner;

// Adresse multicast SSDP (RFC 2616 / UPnP DA 1.1)
static const char*    SSDP_ADDR = "239.255.255.250";
static const uint16_t SSDP_PORT = 1900;

// Message M-SEARCH UPnP 1.1 — MX:3 laisse 3 s aux devices pour répondre
static const char* MSEARCH =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 3\r\n"
    "ST: ssdp:all\r\n"
    "\r\n";

// ---------------------------------------------------------------------------
// M-SEARCH multicast + collecte des URLs LOCATION
// ---------------------------------------------------------------------------
std::vector<String> SSDPScanner::_sendMSearch() {
    std::vector<String> locations;
    WiFiUDP udp;

    // Ouvrir un socket UDP sur un port éphémère pour recevoir les réponses unicast
    if (!udp.begin(0)) {
        Log::e(TAG, "Impossible d'ouvrir socket UDP");
        return locations;
    }

    udp.beginPacket(SSDP_ADDR, SSDP_PORT);
    udp.print(MSEARCH);
    if (!udp.endPacket()) {
        Log::e(TAG, "Échec envoi M-SEARCH");
        udp.stop();
        return locations;
    }
    Log::i(TAG, "M-SEARCH envoyé — écoute %d ms", SSDP_LISTEN_TIMEOUT_MS);

    uint32_t deadline = millis() + SSDP_LISTEN_TIMEOUT_MS;
    char buf[1024];

    while (millis() < deadline) {
        int pktSize = udp.parsePacket();
        if (pktSize > 0) {
            int len = udp.read(buf, sizeof(buf) - 1);
            if (len <= 0) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
            buf[len] = '\0';
            String resp(buf);

            // Cherche le header LOCATION: (insensible à la casse)
            int idx = resp.indexOf("LOCATION:");
            if (idx < 0) idx = resp.indexOf("Location:");
            if (idx < 0) idx = resp.indexOf("location:");
            if (idx < 0) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }

            int eol = resp.indexOf('\r', idx);
            if (eol < 0) eol = resp.indexOf('\n', idx);
            String loc = resp.substring(idx + 9, eol);
            loc.trim();

            if (loc.isEmpty() || !loc.startsWith("http")) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }

            // Déduplication
            bool known = false;
            for (const auto& l : locations) {
                if (l == loc) { known = true; break; }
            }
            if (!known && (int)locations.size() < SSDP_MAX_DEVICES) {
                locations.push_back(loc);
                Log::d(TAG, "LOCATION: %s", loc.c_str());
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    udp.stop();
    Log::i(TAG, "%u réponse(s) SSDP unique(s)", (unsigned)locations.size());
    return locations;
}

// ---------------------------------------------------------------------------
// Extraction robuste d'une valeur entre balises XML
// Supporte : <tag>val</tag>, <ns:tag>val</ns:tag>, attributs ignorés
// ---------------------------------------------------------------------------
String SSDPScanner::_xmlTag(const String& xml, const String& tag) {
    // Tentative directe : <tag>
    String open  = "<"  + tag + ">";
    String close = "</" + tag + ">";
    int s = xml.indexOf(open);
    if (s >= 0) {
        s += open.length();
        int e = xml.indexOf(close, s);
        if (e >= 0) return xml.substring(s, e);
    }

    // Tentative avec namespace : <ns:tag>
    int ns = xml.indexOf(":" + tag + ">");
    if (ns >= 0) {
        int tagStart = ns - 1;
        while (tagStart > 0 && xml[tagStart] != '<') tagStart--;
        String openNs  = xml.substring(tagStart, ns + tag.length() + 2);
        int    prefEnd = openNs.indexOf(':');
        if (prefEnd > 0) {
            String prefix  = openNs.substring(1, prefEnd);
            String closeNs = "</" + prefix + ":" + tag + ">";
            int    s2      = xml.indexOf(openNs);
            if (s2 >= 0) {
                s2 += openNs.length();
                int e2 = xml.indexOf(closeNs, s2);
                if (e2 >= 0) return xml.substring(s2, e2);
            }
        }
    }

    // Tentative avec attributs dans la balise : <tag attr="...">
    open = "<" + tag + " ";
    s = xml.indexOf(open);
    if (s >= 0) {
        int gt = xml.indexOf('>', s);
        if (gt >= 0 && xml[gt - 1] != '/') {
            int s2 = gt + 1;
            close  = "</" + tag + ">";
            int e2 = xml.indexOf(close, s2);
            if (e2 >= 0) return xml.substring(s2, e2);
        }
    }

    return "";
}

// ---------------------------------------------------------------------------
// Catégorisation automatique des équipements UPnP
// Ordre de priorité : fabricant/modèle connu > deviceType générique
// ---------------------------------------------------------------------------
void SSDPScanner::_categorize(NetworkDevice& dev,
                               const String& deviceType,
                               const String& manufacturer,
                               const String& modelName,
                               const String& friendlyName) {
    // Copies minuscules pour comparaisons insensibles à la casse
    String dt  = deviceType;   dt.toLowerCase();
    String mfr = manufacturer; mfr.toLowerCase();
    String mdl = modelName;    mdl.toLowerCase();
    String fn  = friendlyName; fn.toLowerCase();

    // ---- Freebox (Free) ----
    if (fn.indexOf("freebox") >= 0 || mfr.indexOf("free sas") >= 0 ||
        mdl.indexOf("freebox") >= 0) {
        dev.category     = "Router";
        dev.manufacturer = "Free";
        dev.os           = "FreeboxOS";
        if      (fn.indexOf("ultra") >= 0 || mdl.indexOf("ultra") >= 0)
            dev.model = "Freebox Ultra";
        else if (fn.indexOf("pop") >= 0 || mdl.indexOf("pop") >= 0)
            dev.model = "Freebox Pop";
        else if (fn.indexOf("revolution") >= 0 || fn.indexOf("révolution") >= 0)
            dev.model = "Freebox Révolution";
        else if (fn.indexOf("delta") >= 0)
            dev.model = "Freebox Delta";
        else
            dev.model = "Freebox Server";
        return;
    }

    // ---- Livebox (Orange / Sagemcom) ----
    if (fn.indexOf("livebox") >= 0 || mfr.indexOf("sagemcom") >= 0 ||
        mfr.indexOf("orange") >= 0) {
        dev.category     = "Router";
        dev.manufacturer = "Orange";
        dev.model        = "Livebox";
        return;
    }

    // ---- Bbox (Bouygues) ----
    if (fn.indexOf("bbox") >= 0 || mfr.indexOf("bouygues") >= 0 ||
        mfr.indexOf("sfr") >= 0) {
        dev.category     = "Router";
        dev.manufacturer = fn.indexOf("bbox") >= 0 ? "Bouygues" : "SFR";
        dev.model        = fn.indexOf("bbox") >= 0 ? "Bbox" : "SFR Box";
        return;
    }

    // ---- Philips Hue Bridge ----
    if (fn.indexOf("hue bridge") >= 0 || fn.indexOf("philips hue") >= 0 ||
        mfr.indexOf("philips") >= 0 || mfr.indexOf("signify") >= 0) {
        dev.category     = "SmartHub";
        dev.manufacturer = "Philips Hue";
        dev.model        = "Hue Bridge";
        return;
    }

    // ---- Sonos ----
    if (mfr.indexOf("sonos") >= 0 || fn.indexOf("sonos") >= 0) {
        dev.category     = "Speaker";
        dev.manufacturer = "Sonos";
        return;
    }

    // ---- Synology NAS ----
    if (mfr.indexOf("synology") >= 0 || mdl.indexOf("diskstation") >= 0 ||
        fn.indexOf("diskstation") >= 0 || fn.indexOf("synology") >= 0) {
        dev.category     = "NAS";
        dev.manufacturer = "Synology";
        return;
    }

    // ---- Samsung TV ----
    if (mfr.indexOf("samsung") >= 0 &&
        (dt.indexOf("tv") >= 0 || fn.indexOf("tv") >= 0 || fn.indexOf("[tv]") >= 0)) {
        dev.category     = "TV";
        dev.manufacturer = "Samsung";
        return;
    }

    // ---- Google Chromecast ----
    if (fn.indexOf("chromecast") >= 0 || mfr.indexOf("google") >= 0) {
        dev.category     = "Streaming";
        dev.manufacturer = "Google";
        return;
    }

    // ---- Catégorisation générique par deviceType UPnP ----
    if (dt.indexOf("mediarenderer") >= 0) {
        dev.category = "Speaker";
    } else if (dt.indexOf("mediasystem") >= 0 || dt.indexOf("mediacenter") >= 0 ||
               dt.indexOf("digitaltvdevice") >= 0) {
        dev.category = "TV";
    } else if (dt.indexOf("nas") >= 0) {
        dev.category = "NAS";
    } else if (dt.indexOf("wan") >= 0 || dt.indexOf("router") >= 0 ||
               dt.indexOf("gateway") >= 0 || dt.indexOf("internetgatewaydevice") >= 0) {
        dev.category = "Router";
    } else if (dt.indexOf("printer") >= 0) {
        dev.category = "Printer";
    } else if (dt.indexOf("camera") >= 0 || dt.indexOf("securitycamera") >= 0) {
        dev.category = "Camera";
    } else if (dt.indexOf("lightingcontrols") >= 0 || dt.indexOf("dimmer") >= 0 ||
               dt.indexOf("switch") >= 0 || dt.indexOf("sensor") >= 0) {
        dev.category = "IoT";
    } else {
        // Fallback : tout device UPnP non identifié = IoT
        dev.category = "IoT";
    }
}

// ---------------------------------------------------------------------------
// Requête HTTP + parsing XML de la description UPnP
// ---------------------------------------------------------------------------
bool SSDPScanner::_fetchAndParse(const String& locationUrl, NetworkDevice& out) {
    if (!locationUrl.startsWith("http://")) return false;

    // Découpage de l'URL : http://host:port/path
    String rest     = locationUrl.substring(7);
    int    slashIdx = rest.indexOf('/');
    String hostPort = (slashIdx >= 0) ? rest.substring(0, slashIdx) : rest;
    String path     = (slashIdx >= 0) ? rest.substring(slashIdx)    : "/";

    String   host;
    uint16_t port = 80;
    int      col  = hostPort.indexOf(':');
    if (col >= 0) {
        host = hostPort.substring(0, col);
        port = (uint16_t)hostPort.substring(col + 1).toInt();
    } else {
        host = hostPort;
    }
    if (host.isEmpty()) return false;

    WiFiClient client;
    client.setTimeout(SSDP_HTTP_TIMEOUT_MS / 1000);

    if (!client.connect(host.c_str(), port)) {
        Log::d(TAG, "HTTP connect failed : %s:%u", host.c_str(), port);
        return false;
    }

    // HTTP/1.0 — fermeture automatique après la réponse (pas de keep-alive)
    String req = "GET " + path + " HTTP/1.0\r\n"
                 "Host: " + hostPort + "\r\n"
                 "Connection: close\r\n\r\n";
    client.print(req);

    // Attente des données (timeout court)
    uint32_t t0 = millis();
    while (client.connected() && !client.available()) {
        if (millis() - t0 > (uint32_t)SSDP_HTTP_TIMEOUT_MS) { client.stop(); return false; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Lecture limitée à SSDP_MAX_XML_SIZE pour économiser la RAM
    String resp;
    resp.reserve(SSDP_MAX_XML_SIZE);
    while (client.available() && (int)resp.length() < SSDP_MAX_XML_SIZE) {
        resp += (char)client.read();
    }
    client.stop();

    // Séparer les en-têtes HTTP du corps XML
    int bodyIdx = resp.indexOf("\r\n\r\n");
    if (bodyIdx < 0) bodyIdx = resp.indexOf("\n\n");
    String xml = (bodyIdx >= 0) ? resp.substring(bodyIdx + 4) : resp;

    if (xml.length() < 20) { Log::d(TAG, "XML trop court (%u o)", xml.length()); return false; }

    // Extraction des champs UPnP
    String friendlyName    = _xmlTag(xml, "friendlyName");
    String modelName       = _xmlTag(xml, "modelName");
    String manufacturer    = _xmlTag(xml, "manufacturer");
    String deviceType      = _xmlTag(xml, "deviceType");
    String presentationURL = _xmlTag(xml, "presentationURL");

    if (friendlyName.isEmpty() && manufacturer.isEmpty() && modelName.isEmpty()) {
        Log::d(TAG, "XML sans métadonnées utiles depuis %s", host.c_str());
        return false;
    }

    // Remplissage du NetworkDevice
    out.ip     = host;
    out.source = "SSDP";
    if (!friendlyName.isEmpty() && out.hostname.isEmpty()) out.hostname = friendlyName;
    if (!manufacturer.isEmpty())  out.manufacturer = manufacturer;
    if (!modelName.isEmpty())     out.model        = modelName;
    out.lastSeen = millis();
    out.online   = true;

    // Catégorisation automatique (enrichit out.category, out.manufacturer, out.model, out.os)
    _categorize(out, deviceType, manufacturer, modelName, friendlyName);

    Log::i(TAG, "[%s] %s / %s / %s @ %s",
           out.category.c_str(), out.manufacturer.c_str(),
           out.model.c_str(), friendlyName.c_str(), host.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Point d'entrée public : M-SEARCH → fetch XML → liste de NetworkDevice
// ---------------------------------------------------------------------------
std::vector<NetworkDevice> SSDPScanner::discover() {
    Log::i(TAG, "Démarrage scan SSDP");
    std::vector<NetworkDevice> results;

    std::vector<String> locations = _sendMSearch();
    if (locations.empty()) {
        Log::i(TAG, "Aucune réponse SSDP reçue");
        return results;
    }

    for (const String& loc : locations) {
        NetworkDevice dev;
        if (_fetchAndParse(loc, dev) && !dev.ip.isEmpty()) {
            // Déduplication par IP dans les résultats SSDP eux-mêmes
            bool dup = false;
            for (const auto& r : results) {
                if (r.ip == dev.ip) { dup = true; break; }
            }
            if (!dup) results.push_back(dev);
        }
    }

    Log::i(TAG, "SSDP : %u device(s) enrichis", (unsigned)results.size());
    return results;
}
