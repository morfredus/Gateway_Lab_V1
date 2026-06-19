/**
 * PortScanner - Scan TCP des ports communs + banner grabbing HTTP/SSH/FTP
 * + sondage des API HTTP propres aux equipements IoT courants.
 *
 * Utilise des sockets non-bloquants avec select() pour sonder plusieurs
 * ports en parallele (par lots de MAX_BATCH) sur chaque equipement.
 * Les ports ouverts sont retournes ; pour HTTP/8080/8123/5000 un GET /
 * est effectue pour lire l'en-tete Server: (banner grabbing). Pour
 * SSH/FTP/Telnet, la banniere brute envoyee a la connexion est lue.
 *
 * Sondage IoT : une fois le port HTTP ouvert identifie, quelques requetes
 * cibles (Shelly, Tasmota, FritzBox...) permettent de recuperer le modele
 * exact et la version de firmware sans configuration prealable.
 *
 * Ports sondes (14) :
 *   21 FTP - 22 SSH - 23 Telnet - 80 HTTP - 443 HTTPS - 445 SMB
 *   554 RTSP - 1883 MQTT - 3389 RDP - 5000 DSM/UPnP
 *   8080 HTTP-Alt - 8123 HA - 8443 HTTPS-Alt - 9100 IPP
 *
 * Temps de scan typique (reseau /24, ports majoritairement fermes) :
 *   ~ 0.5-2 s par equipement (RST immediat sur ports fermes)
 *   <= 400 ms par lot si tous les ports sont filtres (timeout)
 */

#pragma once
#include <Arduino.h>
#include <vector>
#include <map>

struct PortScanResult {
    std::vector<uint16_t> openPorts;
    String httpBanner;    // Valeur de l'en-tete Server: HTTP (port 80/8080...)
    String httpTitle;     // Contenu de <title> de la page HTTP (port 80/8080...)
    String sshBanner;     // Banniere brute SSH (port 22, ex: "SSH-2.0-OpenSSH_8.4")
    String ftpBanner;     // Banniere brute FTP (port 21)
    String iotType;       // Type d'API IoT detectee (ex: "Shelly", "Tasmota", "FritzBox", "Synology", "Hue")
    String iotModel;      // Modele rapporte par l'API IoT
    String iotFirmware;   // Version de firmware rapportee par l'API IoT
};

// Ports sondes lors d'une passe precise ciblee (scan rapide/approfondi d'un
// seul equipement, cf. NetworkScanner::rescanDevice). Liste differente du
// scan complet : se limite aux services exploitables a la cible et inclut
// des ports specifiques (53, 135, 139, 515, 631) absents du balayage /24.
extern const std::vector<uint16_t> kRescanTargetPorts;

class PortScanner {
public:
    // Scan les ports communs sur chaque IP de la liste.
    // timeout_ms : delai maximum par lot de sockets (recommande : 200-300 ms)
    // customPorts : liste de ports a sonder a la place de la liste par
    // defaut (utilise par la passe precise avec kRescanTargetPorts).
    std::map<String, PortScanResult> scan(
        const std::vector<String>& ips,
        uint32_t timeout_ms = 250,
        const std::vector<uint16_t>* customPorts = nullptr);

private:
    // Scan un lot de ports sur une IP avec sockets non-bloquants + select()
    void _scanBatch(const String& ip,
                    const uint16_t* ports, int nPorts,
                    uint32_t timeout_ms,
                    std::vector<uint16_t>& openPorts);

    // GET HTTP simple pour recuperer l'en-tete Server: et le <title> de la page
    void _httpBanner(const String& ip, uint16_t port, uint32_t timeout_ms,
                      String& serverOut, String& titleOut);

    // Lecture de la banniere brute envoyee a la connexion (SSH/FTP/Telnet)
    String _tcpBanner(const String& ip, uint16_t port, uint32_t timeout_ms);

    // Sondage des API HTTP propres aux equipements IoT connus
    void _probeIoTApis(const String& ip, uint16_t httpPort, uint32_t timeout_ms,
                        PortScanResult& res);
};

extern PortScanner portScanner;
