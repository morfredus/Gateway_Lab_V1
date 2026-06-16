/**
 * NetBiosScanner - Implementation
 *
 * Requete Node Status NetBIOS (RFC 1002 section 4.2.17) :
 *   - Nom interroge : "*" encode selon RFC 1001 section 14.1
 *     (chaque octet du nom brut est encode en deux nibbles + 'A')
 *   - QTYPE = 0x0021 (NBSTAT)
 *
 * La reponse contient la liste des noms NetBIOS enregistres sur la machine.
 * Le premier nom UNIQUE de type Workstation (0x00) ou Server (0x20) est
 * retenu comme nom d'hote. Un nom de type GROUP est retenu comme groupe
 * de travail / domaine.
 */

#include "netbios_scanner.h"
#include "../utils/logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static const char* TAG = "NetBIOS";

NetBiosScanner netBiosScanner;

// Paquet Node Status Request (50 octets) - encodage du nom "*" + 15 octets nuls
// (RFC 1001 14.1 : chaque octet brut devient 2 caracteres 'A'-'P')
static const uint8_t NBSTAT_REQ[] = {
    0xAB, 0xCD,             // Transaction ID
    0x00, 0x00,             // Flags : requete, non recursive
    0x00, 0x01,             // QDCOUNT = 1
    0x00, 0x00,             // ANCOUNT = 0
    0x00, 0x00,             // NSCOUNT = 0
    0x00, 0x00,             // ARCOUNT = 0
    0x20,                   // Longueur du nom encode = 32
    'C', 'K',                // "*" (0x2A) encode
    'A','A', 'A','A', 'A','A', 'A','A', 'A','A',
    'A','A', 'A','A', 'A','A', 'A','A', 'A','A',
    'A','A', 'A','A', 'A','A', 'A','A', 'A','A',
    0x00,                   // Fin du nom
    0x00, 0x21,              // QTYPE = NBSTAT (33)
    0x00, 0x01               // QCLASS = IN (1)
};

NetBiosInfo NetBiosScanner::_queryOne(const String& ip, uint32_t timeout_ms) {
    NetBiosInfo info;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return info;

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(137);
    inet_aton(ip.c_str(), &dst.sin_addr);

    sendto(sock, NBSTAT_REQ, sizeof(NBSTAT_REQ), 0,
           (struct sockaddr*)&dst, sizeof(dst));

    uint8_t buf[512];
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);
    ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&src, &srclen);
    close(sock);

    if (n < 12) return info;
    if (!(buf[2] & 0x80)) return info;   // bit reponse non positionne

    int offset = 12;

    // Section question - meme encodage que la requete (34 octets) ou pointeur compresse
    if (offset >= n) return info;
    if (buf[offset] == 0x20) offset += 34;
    else if ((buf[offset] & 0xC0) == 0xC0) offset += 2;
    else return info;
    offset += 4;   // QTYPE + QCLASS

    // Section reponse (Answer RR)
    if (offset >= n) return info;
    if (buf[offset] == 0x20) offset += 34;
    else if ((buf[offset] & 0xC0) == 0xC0) offset += 2;
    else if (buf[offset] == 0x00) offset += 1;
    else return info;

    if (offset + 10 > n) return info;
    uint16_t atype = ((uint16_t)buf[offset] << 8) | buf[offset + 1];
    offset += 2;
    if (atype != 0x0021) return info;   // pas une reponse NBSTAT
    offset += 2;   // ACLASS
    offset += 4;   // TTL
    offset += 2;   // RDLENGTH (non utilise - on parcourt jusqu'a la fin du buffer)

    if (offset >= n) return info;
    uint8_t numNames = buf[offset++];

    String machineName, workgroupName;

    for (int i = 0; i < numNames && offset + 18 <= n; i++) {
        char rawName[16];
        memcpy(rawName, buf + offset, 15);
        rawName[15] = '\0';
        uint8_t  type  = buf[offset + 15];
        uint16_t flags = ((uint16_t)buf[offset + 16] << 8) | buf[offset + 17];
        bool isGroup   = (flags & 0x8000) != 0;
        offset += 18;

        String name = rawName;
        name.trim();
        if (name.isEmpty()) continue;

        if (!isGroup && (type == 0x00 || type == 0x20) && machineName.isEmpty())
            machineName = name;
        else if (isGroup && type == 0x00 && workgroupName.isEmpty())
            workgroupName = name;
    }

    if (!machineName.isEmpty()) {
        info.hostname  = machineName;
        info.workgroup = workgroupName;
        Log::d(TAG, "%s -> \"%s\" (groupe: %s)",
               ip.c_str(), machineName.c_str(), workgroupName.c_str());
    }
    return info;
}

std::map<String, NetBiosInfo> NetBiosScanner::scan(
        const std::vector<String>& ips, uint32_t timeout_ms) {
    std::map<String, NetBiosInfo> results;
    if (ips.empty()) return results;

    Log::i(TAG, "Scan NetBIOS sur %u IP(s)", (unsigned)ips.size());

    for (const auto& ip : ips) {
        NetBiosInfo info = _queryOne(ip, timeout_ms);
        if (!info.hostname.isEmpty()) results[ip] = info;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    Log::i(TAG, "NetBIOS : %u equipement(s) identifie(s)", (unsigned)results.size());
    return results;
}
