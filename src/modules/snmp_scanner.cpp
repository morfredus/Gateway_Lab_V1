#include "snmp_scanner.h"
#include <WiFiUdp.h>
#include "../utils/logger.h"

static const char* TAG = "SNMP";

SnmpScanner snmpScanner;

// ---------------------------------------------------------------------------
// Paquet GetRequest SNMPv1 fixe pour l'OID sysDescr (1.3.6.1.2.1.1.1.0),
// communaute "public". Encodage ASN.1 BER construit a la main (memes
// principes que le DNS/SSDP deja presents dans le projet) :
//
//   SEQUENCE {                        30 27
//     INTEGER version=0               02 01 00
//     OCTET STRING "public"           04 06 'p''u''b''l''i''c'
//     [0] GetRequest-PDU              A0 1A
//       INTEGER request-id=1          02 01 01
//       INTEGER error-status=0        02 01 00
//       INTEGER error-index=0         02 01 00
//       SEQUENCE varbind-list         30 0E
//         SEQUENCE varbind            30 0C
//           OID 1.3.6.1.2.1.1.1.0     06 08 2B 06 01 02 01 01 01 00
//           NULL                      05 00
//   }
// ---------------------------------------------------------------------------
std::vector<uint8_t> SnmpScanner::_buildRequest() {
    return {
        0x30, 0x27,
            0x02, 0x01, 0x00,
            0x04, 0x06, 'p', 'u', 'b', 'l', 'i', 'c',
            0xA0, 0x1A,
                0x02, 0x01, 0x01,
                0x02, 0x01, 0x00,
                0x02, 0x01, 0x00,
                0x30, 0x0E,
                    0x30, 0x0C,
                        0x06, 0x08, 0x2B, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00,
                        0x05, 0x00
    };
}

int SnmpScanner::_berLength(const uint8_t* buf, int len, int offset, int& bytesUsed) {
    if (offset >= len) return -1;
    uint8_t first = buf[offset];
    if (first < 0x80) { bytesUsed = 1; return first; }
    int nBytes = first & 0x7F;
    if (nBytes == 0 || nBytes > 2 || offset + 1 + nBytes > len) return -1;
    int value = 0;
    for (int i = 0; i < nBytes; i++) value = (value << 8) | buf[offset + 1 + i];
    bytesUsed = 1 + nBytes;
    return value;
}

String SnmpScanner::_parseSysDescr(const uint8_t* buf, int len) {
    // Recherche du motif OID sysDescr tel qu'envoye dans la requete -
    // la reponse SNMP reprend le meme varbind, suivi de la valeur reelle.
    static const uint8_t OID[] = { 0x06, 0x08, 0x2B, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00 };
    const int oidLen = sizeof(OID);

    for (int i = 0; i + oidLen < len; i++) {
        if (memcmp(buf + i, OID, oidLen) != 0) continue;

        int valueOffset = i + oidLen;
        if (valueOffset >= len) return "";
        uint8_t tag = buf[valueOffset];
        if (tag != 0x04) return "";   // Pas une OCTET STRING (NULL = pas de droits / OID inconnu)

        int bytesUsed = 0;
        int valLen = _berLength(buf, len, valueOffset + 1, bytesUsed);
        if (valLen < 0) return "";
        int dataOffset = valueOffset + 1 + bytesUsed;
        if (dataOffset + valLen > len) valLen = len - dataOffset;   // Tronque si paquet UDP coupe
        if (valLen <= 0) return "";

        String result;
        result.reserve(valLen);
        for (int k = 0; k < valLen; k++) {
            char c = (char)buf[dataOffset + k];
            // sysDescr est cense etre du texte ASCII - on filtre les octets
            // de controle qui casseraient l'affichage/JSON
            result += (c >= 32 && c < 127) ? c : ' ';
        }
        result.trim();
        return result;
    }
    return "";
}

std::map<String, String> SnmpScanner::querySysDescr(const std::vector<String>& ips, uint32_t timeout_ms) {
    std::map<String, String> results;
    if (ips.empty()) return results;

    static const std::vector<uint8_t> request = _buildRequest();
    uint8_t rxBuf[512];

    for (const auto& ip : ips) {
        WiFiUDP udp;
        if (!udp.begin(0)) continue;

        IPAddress target;
        if (!target.fromString(ip)) { udp.stop(); continue; }

        udp.beginPacket(target, 161);
        udp.write(request.data(), request.size());
        udp.endPacket();

        uint32_t start = millis();
        while (millis() - start < timeout_ms) {
            int packetSize = udp.parsePacket();
            if (packetSize > 0) {
                int n = udp.read(rxBuf, sizeof(rxBuf));
                String descr = _parseSysDescr(rxBuf, n);
                if (!descr.isEmpty()) {
                    results[ip] = descr;
                    Log::d(TAG, "sysDescr %s : %s", ip.c_str(), descr.c_str());
                }
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        udp.stop();
    }

    return results;
}
