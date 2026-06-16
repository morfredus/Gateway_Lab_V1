/**
 * PortScanner — Scan TCP des ports communs + banner grabbing HTTP
 *
 * Utilise des sockets non-bloquants avec select() pour sonder plusieurs
 * ports en parallèle (par lots de MAX_BATCH) sur chaque équipement.
 * Les ports ouverts sont retournés ; pour HTTP/8080/8123/5000 un GET /
 * est effectué pour lire l'en-tête Server: (banner grabbing).
 *
 * Ports sondés (14) :
 *   21 FTP · 22 SSH · 23 Telnet · 80 HTTP · 443 HTTPS · 445 SMB
 *   554 RTSP · 1883 MQTT · 3389 RDP · 5000 DSM/UPnP
 *   8080 HTTP-Alt · 8123 HA · 8443 HTTPS-Alt · 9100 IPP
 *
 * Temps de scan typique (réseau /24, ports majoritairement fermés) :
 *   ≈ 0.5-2 s par équipement (RST immédiat sur ports fermés)
 *   ≤ 400 ms par lot si tous les ports sont filtrés (timeout)
 */

#pragma once
#include <Arduino.h>
#include <vector>
#include <map>

struct PortScanResult {
    std::vector<uint16_t> openPorts;
    String httpBanner;   // Valeur de l'en-tête Server: HTTP (port 80/8080…)
};

class PortScanner {
public:
    // Scan les ports communs sur chaque IP de la liste.
    // timeout_ms : délai maximum par lot de sockets (recommandé : 200-300 ms)
    std::map<String, PortScanResult> scan(
        const std::vector<String>& ips,
        uint32_t timeout_ms = 250);

private:
    // Scan un lot de ports sur une IP avec sockets non-bloquants + select()
    void _scanBatch(const String& ip,
                    const uint16_t* ports, int nPorts,
                    uint32_t timeout_ms,
                    std::vector<uint16_t>& openPorts);

    // GET HTTP simple pour récupérer Server: header
    String _httpBanner(const String& ip, uint16_t port, uint32_t timeout_ms);
};

extern PortScanner portScanner;
