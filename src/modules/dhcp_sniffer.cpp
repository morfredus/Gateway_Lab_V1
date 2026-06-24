#include "dhcp_sniffer.h"
#include "../utils/logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static const char* TAG = "DHCP";

DhcpSniffer dhcpSniffer;

// Borne haute de la table MAC -> empreinte : un reseau domestique compte
// rarement plus de quelques dizaines de baux actifs simultanement ; au-dela,
// l'entree la plus ancienne est evincee (FIFO approximatif par lastSeen).
static const size_t MAX_DHCP_FINGERPRINTS = 64;

// ---------------------------------------------------------------------------
// Table de signatures Vendor Class Identifier (option 60) -> OS/famille.
// Volontairement limitee aux prefixes les plus stables et documentes
// (clients DHCP standards) : pas d'heuristique sur la liste de parametres
// demandes (option 55), trop ambigue/instable pour etre fiable ici.
// ---------------------------------------------------------------------------
String DhcpSniffer::_guessOs(const String& vendorClass) {
    if (vendorClass.isEmpty()) return "";
    if (vendorClass.startsWith("MSFT"))                return "Windows";
    if (vendorClass.startsWith("android-dhcp"))        return "Android";
    if (vendorClass.startsWith("dhcpcd"))               return "Linux";
    if (vendorClass.startsWith("udhcp"))                return "Linux/Embarque";
    return "";
}

String DhcpSniffer::_macToString(const uint8_t* mac6) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
              mac6[0], mac6[1], mac6[2], mac6[3], mac6[4], mac6[5]);
    return String(buf);
}

void DhcpSniffer::begin() {
    if (!_mutex) _mutex = xSemaphoreCreateMutex();

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { Log::w(TAG, "Impossible de creer le socket UDP/67"); return; }

    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(67);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        // Port deja utilise (ex: serveur DHCP du portail de configuration) -
        // echec silencieux, le firmware continue sans fingerprinting DHCP.
        Log::w(TAG, "bind UDP/67 echoue (errno=%d) - ecoute passive desactivee", errno);
        close(s);
        return;
    }

    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);

    _sock = s;
    Log::i(TAG, "Ecoute passive DHCP active (UDP/67)");
}

// ---------------------------------------------------------------------------
// Parsing minimal d'un paquet BOOTP/DHCP : on ne s'interesse qu'au champ
// chaddr (MAC client, offset 28, 6 octets) et aux options 12/53/60, situees
// apres l'en-tete fixe de 236 octets + le cookie magique de 4 octets.
// ---------------------------------------------------------------------------
void DhcpSniffer::_handlePacket(const uint8_t* buf, int len) {
    static const int CHADDR_OFFSET  = 28;
    static const int OPTIONS_OFFSET = 240;   // 236 (en-tete fixe) + 4 (cookie magique)
    if (len < OPTIONS_OFFSET + 1) return;
    if (buf[0] != 0x01) return;   // op != BOOTREQUEST (on ignore les reponses du serveur)
    // Cookie magique 63.82.53.63
    if (buf[236] != 0x63 || buf[237] != 0x82 || buf[238] != 0x53 || buf[239] != 0x63) return;

    String hostname, vendorClass;
    bool isDiscoverOrRequest = false;

    int i = OPTIONS_OFFSET;
    while (i < len) {
        uint8_t code = buf[i];
        if (code == 0xFF) break;          // End
        if (code == 0x00) { i++; continue; }  // Pad
        if (i + 1 >= len) break;
        uint8_t optLen = buf[i + 1];
        int dataStart = i + 2;
        if (dataStart + optLen > len) break;

        if (code == 53 && optLen >= 1) {
            uint8_t msgType = buf[dataStart];
            isDiscoverOrRequest = (msgType == 1 || msgType == 3);   // DISCOVER ou REQUEST
        } else if (code == 12 && optLen > 0) {
            hostname = String((const char*)(buf + dataStart), optLen);
        } else if (code == 60 && optLen > 0) {
            vendorClass = String((const char*)(buf + dataStart), optLen);
        }

        i = dataStart + optLen;
    }

    if (!isDiscoverOrRequest) return;
    if (hostname.isEmpty() && vendorClass.isEmpty()) return;   // rien d'exploitable

    String mac = _macToString(buf + CHADDR_OFFSET);

    DhcpFingerprint fp;
    fp.hostname    = hostname;
    fp.vendorClass = vendorClass;
    fp.osGuess     = _guessOs(vendorClass);
    fp.lastSeen    = millis();

    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_table.size() >= MAX_DHCP_FINGERPRINTS && _table.find(mac) == _table.end()) {
        // Table pleine et MAC inconnue : evince l'entree la plus ancienne.
        auto oldest = _table.begin();
        for (auto it = _table.begin(); it != _table.end(); ++it) {
            if (it->second.lastSeen < oldest->second.lastSeen) oldest = it;
        }
        _table.erase(oldest);
    }
    _table[mac] = fp;
    xSemaphoreGive(_mutex);

    Log::d(TAG, "%s - hostname=\"%s\" vendor=\"%s\" os=\"%s\"",
           mac.c_str(), hostname.c_str(), vendorClass.c_str(), fp.osGuess.c_str());
}

void DhcpSniffer::loop() {
    if (_sock < 0) return;

    uint8_t rx[576];   // Taille max usuelle d'un paquet DHCP (RFC 2131)
    // Plafonne a quelques paquets par appel pour ne jamais bloquer loop().
    for (int n = 0; n < 4; n++) {
        int len = recv(_sock, rx, sizeof(rx), 0);
        if (len <= 0) break;   // EWOULDBLOCK / rien en attente
        _handlePacket(rx, len);
    }
}

bool DhcpSniffer::lookup(const String& mac, DhcpFingerprint& out) const {
    if (!_mutex) return false;
    String key = mac; key.toUpperCase();

    bool found = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    auto it = _table.find(key);
    if (it != _table.end()) { out = it->second; found = true; }
    xSemaphoreGive(_mutex);
    return found;
}
