/**
 * SsdpScanner — Implémentation v0.0.8
 *
 * Bibliothèques utilisées :
 *   WiFiUdp    — socket UDP multicast pour M-SEARCH / réponses SSDP
 *   WiFiClient — HTTP GET des descripteurs XML et APIs spécifiques
 *   ArduinoJson — parsing JSON Hue / Freebox (petit doc, stack safe)
 *   FreeRTOS   — vTaskDelay pour les attentes non bloquantes
 */

#include "ssdp_scanner.h"
#include <ArduinoJson.h>
#include "../utils/logger.h"

static const char* TAG = "SSDP";

// Adresse multicast et port SSDP standard
static constexpr const char* SSDP_ADDR = "239.255.255.250";
static constexpr uint16_t    SSDP_PORT = 1900;

// Instance globale
SsdpScanner ssdpScanner;

// ════════════════════════════════════════════════════════════════════════════
// Helpers — décomposition d'URL
// ════════════════════════════════════════════════════════════════════════════

String SsdpScanner::_urlIp(const String& url) {
    int start = url.indexOf("//");
    if (start < 0) return "";
    start += 2;
    // Fin : soit ':' (port), soit '/' (path), soit fin de chaîne
    int colon = url.indexOf(':', start);
    int slash  = url.indexOf('/', start);
    int end = (int)url.length();
    if (slash  > 0 && slash  < end) end = slash;
    if (colon  > 0 && colon  < end) end = colon;
    return url.substring(start, end);
}

uint16_t SsdpScanner::_urlPort(const String& url) {
    int start = url.indexOf("//");
    if (start < 0) return 80;
    start += 2;
    int colon = url.indexOf(':', start);
    int slash  = url.indexOf('/', start);
    if (colon < 0) return 80;
    if (slash > 0 && slash < colon) return 80;   // ':' dans le path — pas un port
    int portEnd = (slash > 0) ? slash : (int)url.length();
    int p = url.substring(colon + 1, portEnd).toInt();
    return (p > 0 && p < 65536) ? (uint16_t)p : 80;
}

String SsdpScanner::_urlPath(const String& url) {
    int start = url.indexOf("//");
    if (start < 0) return "/";
    start += 2;
    int slash = url.indexOf('/', start);
    return (slash >= 0) ? url.substring(slash) : String("/");
}

// ════════════════════════════════════════════════════════════════════════════
// Helper — extraction d'un header HTTP depuis une réponse brute
// ════════════════════════════════════════════════════════════════════════════

String SsdpScanner::_httpHeader(const String& resp, const String& header) {
    String lresp = resp;
    lresp.toLowerCase();
    String lhdr = header;
    lhdr.toLowerCase();
    lhdr += ":";

    int pos = lresp.indexOf(lhdr);
    if (pos < 0) return "";
    int valStart = pos + lhdr.length();
    while (valStart < (int)resp.length() && resp[valStart] == ' ') valStart++;
    int valEnd = resp.indexOf('\r', valStart);
    if (valEnd < 0) valEnd = resp.indexOf('\n', valStart);
    if (valEnd < 0) valEnd = resp.length();
    String val = resp.substring(valStart, valEnd);
    val.trim();
    return val;
}

// ════════════════════════════════════════════════════════════════════════════
// Helper — extraction robuste d'un tag XML
//
// Gère :
//   <tag>value</tag>          (standard)
//   <ns:tag>value</ns:tag>    (namespace)
//   <tag attr="x">value</tag> (attributs ignorés)
// Ne plante pas sur du XML mal formé — retourne "" si absent.
// ════════════════════════════════════════════════════════════════════════════

String SsdpScanner::_xmlTag(const String& xml, const String& tag) {
    // Deux patterns : "<tag>" et ":tag>" (namespace)
    const String patterns[2] = { "<" + tag + ">", ":" + tag + ">" };

    for (int ns = 0; ns < 2; ns++) {
        int pos = xml.indexOf(patterns[ns]);
        if (pos < 0) continue;

        // Pour "<tag attr=...>", le '>' peut ne pas suivre immédiatement
        // On cherche le '>' de fermeture de la balise ouvrante
        int tagClose = xml.indexOf('>', pos);
        if (tagClose < 0) continue;
        int valStart = tagClose + 1;

        // Valeur = tout ce qui précède le prochain "</"
        int valEnd = xml.indexOf("</", valStart);
        if (valEnd < 0) continue;

        String val = xml.substring(valStart, valEnd);
        val.trim();
        if (!val.isEmpty()) return val;
    }
    return "";
}

// ════════════════════════════════════════════════════════════════════════════
// HTTP GET minimal (sans HTTPClient pour économiser la stack FreeRTOS)
// ════════════════════════════════════════════════════════════════════════════

String SsdpScanner::_httpGet(const String& ip, uint16_t port,
                              const String& path, uint32_t timeout_ms) {
    WiFiClient client;
    client.setTimeout(timeout_ms / 1000 + 1);

    if (!client.connect(ip.c_str(), port)) {
        Log::d(TAG, "Connexion %s:%d échouée", ip.c_str(), port);
        return "";
    }

    // Requête HTTP/1.0 — pas de chunked transfer, connection fermée après body
    client.print("GET " + path + " HTTP/1.0\r\n"
                 "Host: " + ip + "\r\n"
                 "Connection: close\r\n"
                 "User-Agent: GatewayLab/0.0.8\r\n"
                 "\r\n");

    uint32_t deadline = millis() + timeout_ms;
    String response;
    response.reserve(4096);
    bool headersDone = false;
    String line;

    while (millis() < deadline) {
        if (client.available()) {
            char c = (char)client.read();
            if (!headersDone) {
                // Accumule ligne par ligne jusqu'à la ligne vide (séparateur)
                if (c == '\n') {
                    if (line.length() <= 1) { headersDone = true; }
                    line = "";
                } else if (c != '\r') {
                    line += c;
                }
            } else {
                response += c;
                if (response.length() > 16384) break;   // Sécurité mémoire
            }
        } else if (!client.connected()) {
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    client.stop();
    return response;
}

// ════════════════════════════════════════════════════════════════════════════
// Découverte SSDP — M-SEARCH multicast + collecte des réponses
// ════════════════════════════════════════════════════════════════════════════

std::vector<SsdpScanner::SsdpResponse> SsdpScanner::_discover(uint32_t timeout_ms) {
    WiFiUDP udp;
    // Bind sur un port éphémère pour recevoir les réponses unicast
    if (!udp.begin(0)) {
        Log::e(TAG, "Impossible d'ouvrir le socket UDP");
        return {};
    }

    // Requête M-SEARCH — ST: ssdp:all pour découvrir tous les types UPnP
    const char* msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: ssdp:all\r\n"
        "\r\n";

    udp.beginPacket(SSDP_ADDR, SSDP_PORT);
    udp.print(msearch);
    if (!udp.endPacket()) {
        Log::e(TAG, "Envoi M-SEARCH échoué");
        udp.stop();
        return {};
    }
    Log::i(TAG, "M-SEARCH envoyé → %s:%d", SSDP_ADDR, SSDP_PORT);

    std::vector<SsdpResponse> responses;
    // Déduplication par LOCATION pour éviter les doublons (un device peut
    // renvoyer plusieurs réponses pour différents services UPnP)
    std::vector<String> seenLocations;

    uint32_t deadline = millis() + timeout_ms;

    while (millis() < deadline) {
        int pktSize = udp.parsePacket();
        if (pktSize > 0) {
            String body;
            body.reserve(pktSize);
            while (udp.available()) body += (char)udp.read();

            String loc = _httpHeader(body, "LOCATION");
            if (loc.isEmpty()) continue;

            // Déduplique par URL
            bool seen = false;
            for (const auto& s : seenLocations) {
                if (s == loc) { seen = true; break; }
            }
            if (seen) continue;
            seenLocations.push_back(loc);

            SsdpResponse resp;
            resp.location = loc;
            resp.ip   = _urlIp(loc);
            resp.port = _urlPort(loc);
            resp.path = _urlPath(loc);

            if (resp.ip.isEmpty()) {
                continue;
            }

            // Certains équipements UPnP mal configurés annoncent une LOCATION
            // pointant vers une adresse inutilisable depuis l'ESP32.
            //
            // Cas rencontrés :
            // - 127.0.0.0/8   : boucle locale
            // - 0.0.0.0       : adresse non initialisée
            // - 169.254.0.0/16: APIPA / lien-local Windows
            //
            // Ces adresses conduisent à des requêtes HTTP vouées à l'échec
            // ou ne correspondent pas à un équipement réellement joignable
            // sur le réseau local scanné.
            if (resp.ip.startsWith("127.")) {
                Log::w(TAG, "LOCATION rejetée (boucle locale) : %s", loc.c_str());
                continue;
            }

            if (resp.ip == "0.0.0.0") {
                Log::w(TAG, "LOCATION rejetée (adresse invalide) : %s", loc.c_str());
                continue;
            }

            if (resp.ip.startsWith("169.254.")) {
                Log::w(TAG, "LOCATION rejetée (APIPA) : %s", loc.c_str());
                continue;
            }

            responses.push_back(resp);

            Log::d(TAG, "LOCATION trouvée : %s (ip=%s port=%d)",
                   loc.c_str(),
                   resp.ip.c_str(),
                   resp.port);
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    udp.stop();
    Log::i(TAG, "%d device(s) UPnP détecté(s)", (int)responses.size());
    return responses;
}

// ════════════════════════════════════════════════════════════════════════════
// Catégorisation automatique des équipements UPnP courants
// ════════════════════════════════════════════════════════════════════════════

void SsdpScanner::_categorize(NetworkDevice& dev, const String& deviceType) {
    String mfr   = dev.manufacturer;
    String model = dev.model;
    mfr.toLowerCase();
    model.toLowerCase();

    // Sonos
    if (mfr.indexOf("sonos") >= 0) {
        dev.category = "Speaker";
        dev.os = "Sonos";
        return;
    }
    // Philips Hue Bridge
    if (mfr.indexOf("philips") >= 0 && model.indexOf("hue") >= 0) {
        dev.category = "SmartHub";
        return;
    }
    if (mfr.indexOf("signify") >= 0) {
        dev.category = "SmartHub";
        return;
    }
    // Freebox (Free SAS)
    if (mfr.indexOf("free sas") >= 0 || mfr.indexOf("freebox") >= 0 ||
        model.indexOf("freebox") >= 0) {
        dev.category = "Router";
        dev.os = "FreeboxOS";
        return;
    }
    // Synology
    if (mfr.indexOf("synology") >= 0 || model.indexOf("synology") >= 0 ||
        model.indexOf("diskstation") >= 0 || model.indexOf("rackstation") >= 0) {
        dev.category = "NAS";
        return;
    }
    // Samsung TV / Smart TV
    if (mfr.indexOf("samsung") >= 0) {
        String dtype = deviceType;
        dtype.toLowerCase();
        if (dtype.indexOf("tv") >= 0 || model.indexOf("tv") >= 0) {
            dev.category = "TV";
        } else {
            dev.category = "Computer";
        }
        return;
    }
    // Google Chromecast
    if (mfr.indexOf("google") >= 0 &&
        (model.indexOf("chromecast") >= 0 || model.indexOf("cast") >= 0)) {
        dev.category = "Streaming";
        return;
    }
    // Amazon Fire TV / Fire Stick (protocole DIAL)
    if (mfr.indexOf("amazon") >= 0) {
        dev.category = "Streaming";
        dev.os = "Fire OS";
        return;
    }
    // Livebox / Orange
    if (mfr.indexOf("sagemcom") >= 0 || mfr.indexOf("orange") >= 0 ||
        model.indexOf("livebox") >= 0) {
        dev.category = "Router";
        return;
    }
    // Bbox / Bouygues
    if (model.indexOf("bbox") >= 0 || mfr.indexOf("bouygues") >= 0 ||
        mfr.indexOf("sagem") >= 0) {
        dev.category = "Router";
        return;
    }
    // SFR Box
    if (model.indexOf("sfr box") >= 0 || mfr.indexOf("sfr") >= 0) {
        dev.category = "Router";
        return;
    }
    // Inférence depuis deviceType UPnP
    String dtype = deviceType;
    dtype.toLowerCase();
    if (dtype.indexOf("mediarenderer") >= 0) { dev.category = "Streaming"; return; }
    if (dtype.indexOf("mediaserver")   >= 0) { dev.category = "NAS";       return; }
    if (dtype.indexOf("internetgateway") >= 0 ||
        dtype.indexOf("router")        >= 0) { dev.category = "Router";    return; }
    if (dtype.indexOf("tv")            >= 0) { dev.category = "TV";        return; }
    if (dtype.indexOf("bridge")        >= 0) { dev.category = "SmartHub";  return; }
    if (dtype.indexOf("dial")          >= 0) { dev.category = "Streaming"; return; }

    // Fallback générique
    dev.category = "IoT";
}

// ════════════════════════════════════════════════════════════════════════════
// Détecteurs de famille de produits
// ════════════════════════════════════════════════════════════════════════════

bool SsdpScanner::_isHue(const NetworkDevice& dev) {
    String mfr   = dev.manufacturer; mfr.toLowerCase();
    String model = dev.model;        model.toLowerCase();
    return (mfr.indexOf("philips") >= 0 && model.indexOf("hue") >= 0)
        || mfr.indexOf("signify") >= 0
        || model.indexOf("hue bridge") >= 0;
}

bool SsdpScanner::_isSynology(const NetworkDevice& dev) {
    String mfr   = dev.manufacturer; mfr.toLowerCase();
    String model = dev.model;        model.toLowerCase();
    return mfr.indexOf("synology") >= 0
        || model.indexOf("diskstation") >= 0
        || model.indexOf("rackstation") >= 0;
}

bool SsdpScanner::_isFreebox(const NetworkDevice& dev) {
    String mfr   = dev.manufacturer; mfr.toLowerCase();
    String model = dev.model;        model.toLowerCase();
    return mfr.indexOf("free sas") >= 0
        || mfr.indexOf("freebox") >= 0
        || model.indexOf("freebox") >= 0;
}

// ════════════════════════════════════════════════════════════════════════════
// Enrichissement Hue Bridge — GET http://<ip>/api/config
// Champs extraits (sans authentification) : name, modelid, swversion, apiversion
// ════════════════════════════════════════════════════════════════════════════

void SsdpScanner::_enrichHue(NetworkDevice& dev) {
    Log::i(TAG, "Hue Bridge détecté sur %s → appel /api/config", dev.ip.c_str());
    String body = _httpGet(dev.ip, 80, "/api/config", 2000);
    if (body.isEmpty()) return;

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return;

    String name     = doc["name"]       | "";
    String modelid  = doc["modelid"]    | "";
    String swver    = doc["swversion"]  | "";
    String apiver   = doc["apiversion"] | "";

    dev.manufacturer = "Philips Hue";
    if (!modelid.isEmpty()) {
        // modelid ex: "BSB002" → Bridge v2, "BSB001" → Bridge v1
        if (modelid == "BSB002" || modelid.startsWith("BSB")) {
            dev.model = "Hue Bridge v2";
        } else {
            dev.model = "Hue Bridge";
        }
    }
    if (!name.isEmpty())   dev.hostname = name;
    if (!swver.isEmpty())  dev.os = "Hue FW " + swver;
    dev.category = "SmartHub";
    dev.source   = "HueAPI";

    Log::i(TAG, "Hue Bridge : %s (model=%s fw=%s)",
           name.c_str(), modelid.c_str(), swver.c_str());
}

// ════════════════════════════════════════════════════════════════════════════
// Enrichissement Synology DSM — GET http://<ip>:5000/webapi/query.cgi
// Endpoint non authentifié retournant les infos API de base.
// Le modèle est en général déjà dans le XML UPnP (modelName).
// ════════════════════════════════════════════════════════════════════════════

void SsdpScanner::_enrichSynology(NetworkDevice& dev) {
    Log::i(TAG, "Synology détecté sur %s → appel DSM API", dev.ip.c_str());
    // Endpoint non authentifié : retourne la liste des APIs disponibles
    String body = _httpGet(dev.ip, 5000,
        "/webapi/query.cgi?api=SYNO.API.Info&version=1&method=query", 2000);
    if (body.isEmpty()) {
        // Essai sur port 80 (certaines configs Synology redirigent)
        body = _httpGet(dev.ip, 80,
            "/webapi/query.cgi?api=SYNO.API.Info&version=1&method=query", 2000);
    }

    dev.manufacturer = "Synology";
    dev.category     = "NAS";
    dev.source       = "SynologyAPI";

    if (!body.isEmpty()) {
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            // La réponse ne contient pas directement le modèle dans ce endpoint,
            // mais la présence d'une réponse valide confirme que c'est un DSM.
            bool success = doc["success"] | false;
            if (success) {
                Log::i(TAG, "Synology DSM confirmé sur %s", dev.ip.c_str());
            }
        }
    }

    // Modèle depuis le XML UPnP (ex: "DiskStation DS224+") — conserver
    // Si le modèle contient "DS" ou "RS", l'extraire proprement
    String model = dev.model;
    if (model.indexOf("DiskStation") >= 0) {
        int idx = model.indexOf("DS");
        if (idx >= 0) dev.model = model.substring(idx);
    } else if (model.indexOf("RackStation") >= 0) {
        int idx = model.indexOf("RS");
        if (idx >= 0) dev.model = model.substring(idx);
    }

    // DSM version sera dans le os si détectable via hostname
    if (dev.os.isEmpty()) dev.os = "DSM";

    Log::i(TAG, "Synology : model=%s", dev.model.c_str());
}

// ════════════════════════════════════════════════════════════════════════════
// Enrichissement Freebox — GET http://<ip>/api_version
// Retourne sans authentification : device_name, device_type, firmware_version
// ════════════════════════════════════════════════════════════════════════════

void SsdpScanner::_enrichFreebox(NetworkDevice& dev) {
    Log::i(TAG, "Freebox détectée sur %s → appel /api_version", dev.ip.c_str());
    String body = _httpGet(dev.ip, 80, "/api_version", 2000);
    if (body.isEmpty()) return;

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return;

    String deviceName = doc["device_name"]     | "";   // ex: "Freebox Ultra"
    String deviceType = doc["device_type"]     | "";   // ex: "FreeboxV9/8.3.6"
    String fwVersion  = doc["firmware_version"]| "";   // ex: "4.8.3"
    String apiVersion = doc["api_version"]     | "";

    dev.manufacturer = "Free";
    dev.category     = "Router";
    dev.os           = "FreeboxOS";
    dev.source       = "FreeboxAPI";

    if (!deviceName.isEmpty()) {
        dev.model = deviceName;   // ex: "Freebox Ultra", "Freebox Pop"
        // Affiner le modèle depuis device_type si device_name est générique
    } else if (!deviceType.isEmpty()) {
        // device_type ex: "FreeboxV9/8.3.6" → "Freebox Ultra"
        if      (deviceType.indexOf("V9") >= 0) dev.model = "Freebox Ultra";
        else if (deviceType.indexOf("V8") >= 0) dev.model = "Freebox Pop";
        else if (deviceType.indexOf("V7") >= 0) dev.model = "Freebox Révolution";
        else if (deviceType.indexOf("V6") >= 0) dev.model = "Freebox Mini 4K";
        else                                    dev.model = "Freebox";
    }

    if (!fwVersion.isEmpty()) {
        dev.os = "FreeboxOS " + fwVersion;
    }

    Log::i(TAG, "Freebox : %s (type=%s fw=%s)",
           deviceName.c_str(), deviceType.c_str(), fwVersion.c_str());
}

// ════════════════════════════════════════════════════════════════════════════
// Parsing du descripteur XML UPnP
// ════════════════════════════════════════════════════════════════════════════

NetworkDevice SsdpScanner::_parseDescription(const SsdpResponse& resp,
                                              const String& xml) {
    NetworkDevice dev;
    dev.ip       = resp.ip;
    dev.source   = "SSDP";
    dev.online   = true;
    dev.lastSeen = millis();

    dev.hostname     = _xmlTag(xml, "friendlyName");
    dev.manufacturer = _xmlTag(xml, "manufacturer");
    dev.model        = _xmlTag(xml, "modelName");
    String devType   = _xmlTag(xml, "deviceType");
    String presUrl   = _xmlTag(xml, "presentationURL");

    // Nettoyer le hostname s'il contient un caractère nul ou espaces parasites
    dev.hostname.trim();
    dev.manufacturer.trim();
    dev.model.trim();

    // Catégorisation automatique
    _categorize(dev, devType);

    // Enrichissements spécifiques par famille de produits
    if      (_isHue(dev))      _enrichHue(dev);
    else if (_isSynology(dev)) _enrichSynology(dev);
    else if (_isFreebox(dev))  _enrichFreebox(dev);

    return dev;
}

// ════════════════════════════════════════════════════════════════════════════
// Point d'entrée public
// ════════════════════════════════════════════════════════════════════════════

std::vector<NetworkDevice> SsdpScanner::scan(uint32_t timeout_ms) {
    Log::i(TAG, "Démarrage scan SSDP (timeout M-SEARCH=%ums)", timeout_ms);

    // Phase 1 — Découverte multicast
    auto responses = _discover(timeout_ms);

    // Phase 2 — Récupération et parsing des descripteurs XML
    std::vector<NetworkDevice> results;
    results.reserve(responses.size());

    for (const auto& resp : responses) {
        Log::d(TAG, "Fetch descripteur %s:%d%s", resp.ip.c_str(), resp.port, resp.path.c_str());
        String xml = _httpGet(resp.ip, resp.port, resp.path, 2000);
        if (xml.isEmpty()) {
            Log::w(TAG, "Pas de réponse XML de %s", resp.ip.c_str());
            // Entrée minimale sans description XML
            NetworkDevice dev;
            dev.ip       = resp.ip;
            dev.source   = "SSDP";
            dev.online   = true;
            dev.lastSeen = millis();
            results.push_back(dev);
            continue;
        }
        NetworkDevice dev = _parseDescription(resp, xml);
        results.push_back(dev);

        Log::i(TAG, "UPnP %s — %s (%s) [%s]",
               dev.ip.c_str(),
               dev.hostname.isEmpty() ? "-" : dev.hostname.c_str(),
               dev.manufacturer.isEmpty() ? "-" : dev.manufacturer.c_str(),
               dev.category.c_str());
    }

    Log::i(TAG, "Scan SSDP terminé — %d équipement(s) identifié(s)", (int)results.size());
    return results;
}
