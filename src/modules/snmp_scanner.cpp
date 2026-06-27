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

std::vector<uint8_t> SnmpScanner::_encodeLength(size_t len) {
    std::vector<uint8_t> out;
    if (len < 0x80) { out.push_back((uint8_t)len); }
    else { out.push_back(0x81); out.push_back((uint8_t)len); }   // Suffisant : paquets SNMP construits ici toujours < 256 octets
    return out;
}

std::vector<uint8_t> SnmpScanner::_encodeOid(const std::vector<uint32_t>& arcs) {
    std::vector<uint8_t> out;
    if (arcs.size() < 2) return out;
    out.push_back((uint8_t)(arcs[0] * 40 + arcs[1]));
    for (size_t i = 2; i < arcs.size(); i++) {
        uint32_t v = arcs[i];
        uint8_t groups[5];
        int n = 0;
        groups[n++] = v & 0x7F;
        v >>= 7;
        while (v > 0) { groups[n++] = v & 0x7F; v >>= 7; }
        for (int k = n - 1; k >= 0; k--) {
            out.push_back(k == 0 ? groups[k] : (uint8_t)(groups[k] | 0x80));
        }
    }
    return out;
}

std::vector<uint32_t> SnmpScanner::_decodeOid(const uint8_t* buf, int len) {
    std::vector<uint32_t> arcs;
    if (len < 1) return arcs;
    uint8_t first = buf[0];
    uint32_t arc0 = (first < 80) ? (first / 40) : 2;
    uint32_t arc1 = first - arc0 * 40;
    arcs.push_back(arc0);
    arcs.push_back(arc1);
    uint32_t value = 0;
    for (int i = 1; i < len; i++) {
        value = (value << 7) | (buf[i] & 0x7F);
        if (!(buf[i] & 0x80)) { arcs.push_back(value); value = 0; }
    }
    return arcs;
}

// GetNextRequest SNMPv1 — structure identique au GetRequest de _buildRequest()
// mais avec un PDU-tag A1 et un OID dynamique (calcul des longueurs BER fait
// au fur et a mesure, du varbind vers l'enveloppe).
std::vector<uint8_t> SnmpScanner::_buildGetNextRequest(const std::vector<uint32_t>& oid) {
    std::vector<uint8_t> oidBytes = _encodeOid(oid);

    std::vector<uint8_t> varbind = { 0x06 };
    auto oidLenBytes = _encodeLength(oidBytes.size());
    varbind.insert(varbind.end(), oidLenBytes.begin(), oidLenBytes.end());
    varbind.insert(varbind.end(), oidBytes.begin(), oidBytes.end());
    varbind.push_back(0x05);
    varbind.push_back(0x00);

    std::vector<uint8_t> varbindList = { 0x30 };
    auto vbLenBytes = _encodeLength(varbind.size());
    varbindList.insert(varbindList.end(), vbLenBytes.begin(), vbLenBytes.end());
    varbindList.insert(varbindList.end(), varbind.begin(), varbind.end());

    std::vector<uint8_t> pduBody = { 0x02, 0x01, 0x01, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00 };
    pduBody.insert(pduBody.end(), varbindList.begin(), varbindList.end());

    std::vector<uint8_t> pdu = { 0xA1 };
    auto pduLenBytes = _encodeLength(pduBody.size());
    pdu.insert(pdu.end(), pduLenBytes.begin(), pduLenBytes.end());
    pdu.insert(pdu.end(), pduBody.begin(), pduBody.end());

    std::vector<uint8_t> body = { 0x02, 0x01, 0x00, 0x04, 0x06, 'p', 'u', 'b', 'l', 'i', 'c' };
    body.insert(body.end(), pdu.begin(), pdu.end());

    std::vector<uint8_t> packet = { 0x30 };
    auto bodyLenBytes = _encodeLength(body.size());
    packet.insert(packet.end(), bodyLenBytes.begin(), bodyLenBytes.end());
    packet.insert(packet.end(), body.begin(), body.end());

    return packet;
}

// Le premier octet de tag OID (0x06) rencontre dans la reponse appartient
// forcement au varbind retourne — aucun autre champ du paquet (version,
// communaute, request-id/error-status/error-index) n'utilise ce tag.
bool SnmpScanner::_parseGetNextResponse(const uint8_t* buf, int len,
                                         std::vector<uint32_t>& outOid,
                                         std::vector<uint8_t>& outValue) {
    for (int i = 0; i < len; i++) {
        if (buf[i] != 0x06) continue;

        int bytesUsed = 0;
        int oidLen = _berLength(buf, len, i + 1, bytesUsed);
        if (oidLen < 0) return false;
        int oidStart = i + 1 + bytesUsed;
        if (oidStart + oidLen > len) return false;
        outOid = _decodeOid(buf + oidStart, oidLen);

        int valOffset = oidStart + oidLen;
        if (valOffset >= len) return false;
        int vBytesUsed = 0;
        int valLen = _berLength(buf, len, valOffset + 1, vBytesUsed);
        if (valLen < 0) return false;
        int dataOffset = valOffset + 1 + vBytesUsed;
        if (dataOffset + valLen > len) valLen = len - dataOffset;
        if (valLen < 0) return false;
        outValue.assign(buf + dataOffset, buf + dataOffset + valLen);
        return true;
    }
    return false;
}

std::vector<String> SnmpScanner::walkBridgeMacTable(const String& ip, uint32_t timeout_ms, int maxEntries) {
    std::vector<String> macs;

    IPAddress target;
    if (!target.fromString(ip)) return macs;

    static const std::vector<uint32_t> BASE_OID = { 1, 3, 6, 1, 2, 1, 17, 4, 3, 1, 1 };   // dot1dTpFdbAddress
    std::vector<uint32_t> current = BASE_OID;
    uint8_t rxBuf[512];

    for (int iter = 0; iter < maxEntries; iter++) {
        WiFiUDP udp;
        if (!udp.begin(0)) break;

        std::vector<uint8_t> packet = _buildGetNextRequest(current);
        udp.beginPacket(target, 161);
        udp.write(packet.data(), packet.size());
        udp.endPacket();

        int n = 0;
        uint32_t start = millis();
        while (millis() - start < timeout_ms) {
            int packetSize = udp.parsePacket();
            if (packetSize > 0) { n = udp.read(rxBuf, sizeof(rxBuf)); break; }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        udp.stop();
        if (n <= 0) break;   // Pas de reponse : agent SNMP absent/desactive, ou fin de MIB

        std::vector<uint32_t> respOid;
        std::vector<uint8_t>  respVal;
        if (!_parseGetNextResponse(rxBuf, n, respOid, respVal)) break;

        // Toujours dans la sous-arborescence dot1dTpFdbTable ? Sinon on a
        // depasse la table (MIB suivante) - fin de la marche.
        if (respOid.size() < BASE_OID.size()) break;
        bool samePrefix = true;
        for (size_t i = 0; i < BASE_OID.size(); i++) {
            if (respOid[i] != BASE_OID[i]) { samePrefix = false; break; }
        }
        if (!samePrefix) break;

        if (respVal.size() == 6) {
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                      respVal[0], respVal[1], respVal[2], respVal[3], respVal[4], respVal[5]);
            macs.push_back(String(macStr));
        }

        if (respOid == current) break;   // Pas de progression - evite une boucle infinie
        current = respOid;
    }

    return macs;
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
