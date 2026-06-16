/**
 * IcmpScanner — Implémentation
 *
 * Utilise les raw sockets BSD (AF_INET / SOCK_RAW / IPPROTO_ICMP)
 * disponibles via lwIP sur l'ESP32 Arduino.
 *
 * Paquet ICMP echo request :
 *   Octet 0 : type = 8 (echo request)
 *   Octet 1 : code = 0
 *   Octets 2-3 : checksum (complément à 1)
 *   Octets 4-5 : identifier
 *   Octets 6-7 : sequence number
 *
 * La réponse (echo reply, type=0) est reçue via recvfrom() avec timeout.
 * L'IP header (20 octets) précède l'en-tête ICMP dans le buffer reçu.
 */

#include "icmp_scanner.h"
#include "../utils/logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static const char* TAG = "ICMP";

IcmpScanner icmpScanner;

// Calcul du checksum Internet (complément à 1 sur 16 bits)
static uint16_t _checksum(const uint8_t* buf, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2)
        sum += ((uint32_t)buf[i] << 8) | buf[i + 1];
    if (len & 1) sum += (uint32_t)buf[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

bool IcmpScanner::_pingOne(const String& ip, uint32_t timeout_ms) {
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        Log::w(TAG, "socket() échoué — droits insuffisants ?");
        return false;
    }

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    inet_aton(ip.c_str(), &dst.sin_addr);

    // Construction du paquet ICMP echo request (8 octets)
    uint8_t pkt[8];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 8;     // Type : echo request
    pkt[1] = 0;     // Code : 0
    pkt[4] = 0xAB;  // Identifier hi
    pkt[5] = 0xCD;  // Identifier lo
    pkt[6] = 0;     // Sequence hi
    pkt[7] = 1;     // Sequence lo
    uint16_t ck = _checksum(pkt, sizeof(pkt));
    pkt[2] = ck >> 8;
    pkt[3] = ck & 0xFF;

    ssize_t sent = sendto(sock, pkt, sizeof(pkt), 0,
                          (struct sockaddr*)&dst, sizeof(dst));
    if (sent < 0) {
        close(sock);
        return false;
    }

    // Attente de la réponse
    uint8_t buf[64];
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);
    ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr*)&src, &srclen);
    close(sock);

    // buf[0..19] = IP header (20 octets), buf[20] = type ICMP
    // Type 0 = echo reply — confirme que l'hôte est joignable
    return (n >= 28 && buf[20] == 0);
}

std::vector<String> IcmpScanner::ping(const std::vector<String>& ips,
                                       uint32_t timeout_ms) {
    std::vector<String> alive;
    alive.reserve(ips.size() / 4);

    for (const auto& ip : ips) {
        if (_pingOne(ip, timeout_ms)) {
            alive.push_back(ip);
            Log::d(TAG, "Alive: %s", ip.c_str());
        }
        // Petit délai pour ne pas saturer le stack lwIP
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    Log::i(TAG, "ICMP: %u/%u IP(s) répondent au ping",
           (unsigned)alive.size(), (unsigned)ips.size());
    return alive;
}
