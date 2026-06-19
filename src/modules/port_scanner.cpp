/**
 * PortScanner - Implementation
 *
 * Technique : sockets non-bloquants + select()
 *   1. Pour chaque IP, ouvrir MAX_BATCH sockets simultanement
 *   2. connect() non-bloquant -> EINPROGRESS immediat
 *   3. select() avec timeout -> collecte les sockets prets (connectes)
 *   4. getsockopt(SO_ERROR) == 0 -> port ouvert
 *   5. Repeter par lots jusqu'a epuisement des ports
 *
 * Contrainte lwIP : CONFIG_LWIP_MAX_SOCKETS = 16 par defaut sur ESP32.
 * Le Web Server occupe 1-2 sockets -> MAX_BATCH = 8 pour rester sur.
 *
 * Banner HTTP : WiFiClient bloquant avec timeout court (1 s).
 * Uniquement sur les ports HTTP ouverts (80, 8080, 8123, 5000).
 *
 * Banner SSH/FTP : ces services envoient leur banniere immediatement a
 * la connexion (avant toute requete) - une simple lecture suffit.
 *
 * Sondage IoT : apres identification d'un port HTTP ouvert, quelques
 * requetes GET ciblees permettent d'identifier precisement certains
 * equipements (Shelly, Tasmota, FritzBox) sans configuration prealable.
 */

#include "port_scanner.h"
#include "../utils/logger.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static const char* TAG = "Ports";

PortScanner portScanner;

// Ports sondés et leur nom court associé
static const uint16_t SCAN_PORTS[] = {
    21, 22, 23, 80, 443, 445, 554, 1883, 3389, 5000, 8080, 8123, 8443, 9100
};
static const int N_PORTS = sizeof(SCAN_PORTS) / sizeof(SCAN_PORTS[0]);

// Ports sur lesquels tenter un banner HTTP (non-TLS uniquement)
static bool isHttpPort(uint16_t p) {
    return (p == 80 || p == 8080 || p == 8123 || p == 5000);
}

// ---------------------------------------------------------------------------
// Scan d'un lot de ports avec sockets non-bloquants
// ---------------------------------------------------------------------------
void PortScanner::_scanBatch(const String& ip,
                              const uint16_t* ports, int nPorts,
                              uint32_t timeout_ms,
                              std::vector<uint16_t>& openPorts) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (inet_aton(ip.c_str(), &addr.sin_addr) == 0) return;

    static const int MAX_BATCH = 8;
    int   fds[MAX_BATCH];
    uint16_t fdPort[MAX_BATCH];
    int nFds = 0;

    // Ouvrir les sockets et initier les connexions
    for (int i = 0; i < nPorts && nFds < MAX_BATCH; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) continue;

        // Mode non-bloquant
        int fl = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, fl | O_NONBLOCK);

        addr.sin_port = htons(ports[i]);
        int r = connect(s, (struct sockaddr*)&addr, sizeof(addr));
        if (r == 0) {
            // Connexion immédiate (rare mais possible en loopback)
            openPorts.push_back(ports[i]);
            close(s);
            continue;
        }
        if (errno != EINPROGRESS) {
            close(s);
            continue;
        }
        fds[nFds]    = s;
        fdPort[nFds] = ports[i];
        nFds++;
    }

    if (nFds == 0) return;

    // Attente des connexions avec timeout
    uint32_t deadline = millis() + timeout_ms;

    while (nFds > 0 && millis() < deadline) {
        fd_set wfds, efds;
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        int maxFd = 0;
        for (int i = 0; i < MAX_BATCH; i++) {
            if (fds[i] < 0) continue;
            FD_SET(fds[i], &wfds);
            FD_SET(fds[i], &efds);
            if (fds[i] > maxFd) maxFd = fds[i];
        }

        uint32_t rem = deadline - millis();
        struct timeval tv;
        tv.tv_sec  = rem / 1000;
        tv.tv_usec = (rem % 1000) * 1000;

        int ret = select(maxFd + 1, nullptr, &wfds, &efds, &tv);
        if (ret <= 0) break;  // Timeout ou erreur

        for (int i = 0; i < MAX_BATCH; i++) {
            if (fds[i] < 0) continue;
            bool triggered = FD_ISSET(fds[i], &wfds) || FD_ISSET(fds[i], &efds);
            if (!triggered) continue;

            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0) {
                openPorts.push_back(fdPort[i]);
                Log::d(TAG, "%s:%u ouvert", ip.c_str(), fdPort[i]);
            }
            close(fds[i]);
            fds[i] = -1;
            nFds--;
        }
    }

    // Fermer les sockets restants (timeout)
    for (int i = 0; i < MAX_BATCH; i++) {
        if (fds[i] >= 0) { close(fds[i]); fds[i] = -1; }
    }
}

// ---------------------------------------------------------------------------
// Banner grabbing HTTP : GET / → lire l'en-tête Server:
// ---------------------------------------------------------------------------
String PortScanner::_httpBanner(const String& ip, uint16_t port, uint32_t timeout_ms) {
    WiFiClient client;
    client.setTimeout(1);
    if (!client.connect(ip.c_str(), port)) return "";

    client.printf("GET / HTTP/1.0\r\nHost: %s\r\nUser-Agent: GatewayLabV1\r\nConnection: close\r\n\r\n",
                  ip.c_str());

    String banner;
    unsigned long start = millis();
    while (client.connected() && millis() - start < timeout_ms) {
        if (!client.available()) { delay(5); continue; }
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) break;
        if (line.startsWith("Server:") || line.startsWith("server:")) {
            banner = line.substring(7);
            banner.trim();
            break;
        }
    }
    client.stop();
    return banner;
}

// ---------------------------------------------------------------------------
// Banniere brute (SSH/FTP/Telnet) : lue immediatement apres connexion
// ---------------------------------------------------------------------------
String PortScanner::_tcpBanner(const String& ip, uint16_t port, uint32_t timeout_ms) {
    WiFiClient client;
    client.setTimeout(1);
    if (!client.connect(ip.c_str(), port)) return "";

    String banner;
    unsigned long start = millis();
    while (client.connected() && millis() - start < timeout_ms) {
        if (!client.available()) { delay(5); continue; }
        banner = client.readStringUntil('\n');
        banner.trim();
        break;
    }
    client.stop();
    return banner;
}

// ---------------------------------------------------------------------------
// Sondage des API HTTP propres aux equipements IoT connus
// ---------------------------------------------------------------------------
static String _httpGet(const String& ip, uint16_t port, const String& path,
                        uint32_t timeout_ms) {
    WiFiClient client;
    client.setTimeout(1);
    if (!client.connect(ip.c_str(), port)) return "";

    client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: GatewayLabV1\r\nConnection: close\r\n\r\n",
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
        if (body.length() > 1024) break;   // suffisant pour identifier le modele
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

void PortScanner::_probeIoTApis(const String& ip, uint16_t httpPort, uint32_t timeout_ms,
                                 PortScanResult& res) {
    // Shelly : GET /shelly -> {"type":"SHSW-1","fw":"...","mac":"..."}
    String body = _httpGet(ip, httpPort, "/shelly", timeout_ms);
    if (body.indexOf("\"type\"") >= 0 && body.indexOf("\"mac\"") >= 0) {
        res.iotType     = "Shelly";
        res.iotModel    = _extractJsonField(body, "type");
        res.iotFirmware = _extractJsonField(body, "fw");
        return;
    }

    // Tasmota : GET /cm?cmnd=Status%200 -> JSON avec "Version" et "Module"
    body = _httpGet(ip, httpPort, "/cm?cmnd=Status%200", timeout_ms);
    if (body.indexOf("\"Version\"") >= 0) {
        res.iotType     = "Tasmota";
        res.iotModel    = _extractJsonField(body, "Module");
        res.iotFirmware = _extractJsonField(body, "Version");
        return;
    }

    // FritzBox : GET /jason_boxinfo.xml -> XML avec <j:Name> et <j:Version>
    body = _httpGet(ip, httpPort, "/jason_boxinfo.xml", timeout_ms);
    if (body.indexOf("<j:Name>") >= 0) {
        res.iotType  = "FritzBox";
        int s = body.indexOf("<j:Name>");
        if (s >= 0) {
            s += 8;
            int e = body.indexOf("</j:Name>", s);
            if (e > s) res.iotModel = body.substring(s, e);
        }
        s = body.indexOf("<j:Version>");
        if (s >= 0) {
            s += 11;
            int e = body.indexOf("</j:Version>", s);
            if (e > s) res.iotFirmware = body.substring(s, e);
        }
    }
}

// ---------------------------------------------------------------------------
// Scan principal : tous les ports sur chaque IP
// ---------------------------------------------------------------------------
std::map<String, PortScanResult> PortScanner::scan(
        const std::vector<String>& ips, uint32_t timeout_ms) {

    std::map<String, PortScanResult> results;
    if (ips.empty()) return results;

    Log::i(TAG, "Scan de %u ports sur %u équipements",
           (unsigned)N_PORTS, (unsigned)ips.size());

    static const int MAX_BATCH = 8;

    for (const auto& ip : ips) {
        PortScanResult res;

        // Balayer les ports par lots de MAX_BATCH
        for (int start = 0; start < N_PORTS; start += MAX_BATCH) {
            int count = min(MAX_BATCH, N_PORTS - start);
            _scanBatch(ip, SCAN_PORTS + start, count, timeout_ms, res.openPorts);
            // Petit délai entre lots pour laisser respirer le stack lwIP
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Banner HTTP + sondage API IoT sur le premier port HTTP ouvert
        for (uint16_t p : res.openPorts) {
            if (isHttpPort(p)) {
                res.httpBanner = _httpBanner(ip, p, 1000);
                _probeIoTApis(ip, p, 800, res);
                break;
            }
        }

        // Banniere brute SSH (port 22)
        for (uint16_t p : res.openPorts) {
            if (p == 22) { res.sshBanner = _tcpBanner(ip, p, 500); break; }
        }

        // Banniere brute FTP (port 21)
        for (uint16_t p : res.openPorts) {
            if (p == 21) { res.ftpBanner = _tcpBanner(ip, p, 500); break; }
        }

        bool hasInfo = !res.openPorts.empty() || !res.httpBanner.isEmpty() ||
                       !res.sshBanner.isEmpty() || !res.ftpBanner.isEmpty();
        if (hasInfo) {
            String portList;
            for (size_t i = 0; i < res.openPorts.size(); i++) {
                if (i) portList += "|";
                portList += String(res.openPorts[i]);
            }
            Log::i(TAG, "%s - ports ouverts: [%s] banner: \"%s\" iot: \"%s\"",
                   ip.c_str(), portList.c_str(), res.httpBanner.c_str(), res.iotType.c_str());
            results[ip] = res;
        }
    }

    Log::i(TAG, "Port scan termine - %u equipement(s) avec ports ouverts",
           (unsigned)results.size());
    return results;
}
