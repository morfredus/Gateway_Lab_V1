#include "mqtt_scanner.h"
#include <WiFiClient.h>
#include "../utils/logger.h"

static const char* TAG = "MQTT";

MqttScanner mqttScanner;

// ---------------------------------------------------------------------------
// CONNECT (MQTT v3.1.1) - session "clean", sans identifiants, client id
// derive de millis() pour eviter toute collision avec un client existant.
// ---------------------------------------------------------------------------
std::vector<uint8_t> MqttScanner::_buildConnect() {
    String clientId = "GatewayLab-" + String(millis());
    if (clientId.length() > 23) clientId = clientId.substring(0, 23);  // limite historique MQTT 3.1

    std::vector<uint8_t> varHeaderPayload;
    // Nom de protocole "MQTT"
    varHeaderPayload.push_back(0x00);
    varHeaderPayload.push_back(0x04);
    varHeaderPayload.insert(varHeaderPayload.end(), { 'M', 'Q', 'T', 'T' });
    varHeaderPayload.push_back(0x04);  // Niveau de protocole = 4 (v3.1.1)
    varHeaderPayload.push_back(0x02);  // Connect flags : clean session
    varHeaderPayload.push_back(0x00);  // Keep alive (MSB)
    varHeaderPayload.push_back(0x3C);  // Keep alive (LSB) = 60 s

    // Payload : client id
    varHeaderPayload.push_back((uint8_t)(clientId.length() >> 8));
    varHeaderPayload.push_back((uint8_t)(clientId.length() & 0xFF));
    for (size_t i = 0; i < clientId.length(); i++) varHeaderPayload.push_back((uint8_t)clientId[i]);

    std::vector<uint8_t> packet;
    packet.push_back(0x10);  // CONNECT
    packet.push_back((uint8_t)varHeaderPayload.size());  // Remaining length (< 128 octets ici)
    packet.insert(packet.end(), varHeaderPayload.begin(), varHeaderPayload.end());
    return packet;
}

// ---------------------------------------------------------------------------
// SUBSCRIBE aux deux topics $SYS standards (QoS 0).
// ---------------------------------------------------------------------------
std::vector<uint8_t> MqttScanner::_buildSubscribe() {
    static const char* TOPICS[] = { "$SYS/broker/version", "$SYS/broker/clients/connected" };

    std::vector<uint8_t> body;
    body.push_back(0x00);  // Packet identifier (MSB)
    body.push_back(0x01);  // Packet identifier (LSB)

    for (const char* t : TOPICS) {
        size_t len = strlen(t);
        body.push_back((uint8_t)(len >> 8));
        body.push_back((uint8_t)(len & 0xFF));
        for (size_t i = 0; i < len; i++) body.push_back((uint8_t)t[i]);
        body.push_back(0x00);  // QoS 0
    }

    std::vector<uint8_t> packet;
    packet.push_back(0x82);  // SUBSCRIBE
    packet.push_back((uint8_t)body.size());
    packet.insert(packet.end(), body.begin(), body.end());
    return packet;
}

MqttProbeResult MqttScanner::probe(const String& ip, uint16_t port, uint32_t timeout_ms) {
    MqttProbeResult result;

    WiFiClient client;
    client.setTimeout(1);
    if (!client.connect(ip.c_str(), port)) return result;

    auto connectPkt = _buildConnect();
    client.write(connectPkt.data(), connectPkt.size());

    // ── Attente du CONNACK (0x20, longueur 2, [flags, returnCode]) ───────
    uint8_t buf[4];
    uint32_t start = millis();
    int got = 0;
    while (got < 4 && millis() - start < 500) {
        if (!client.available()) { delay(5); continue; }
        int n = client.read(buf + got, 4 - got);
        if (n > 0) got += n;
    }
    if (got < 4 || buf[0] != 0x20) { client.stop(); return result; }

    result.reachable    = true;
    uint8_t returnCode  = buf[3];
    result.authRequired = (returnCode != 0x00);
    Log::d(TAG, "%s:%u CONNACK code=%u", ip.c_str(), port, returnCode);

    if (result.authRequired) { client.stop(); return result; }

    // ── Souscription $SYS/broker/version + $SYS/broker/clients/connected ─
    auto subPkt = _buildSubscribe();
    client.write(subPkt.data(), subPkt.size());

    // ── Lecture des paquets recus (SUBACK puis PUBLISH eventuels) ────────
    uint8_t rx[256];
    String pending;
    start = millis();
    while (millis() - start < timeout_ms) {
        if (!client.available()) { delay(5); continue; }
        int n = client.read(rx, sizeof(rx));
        if (n <= 0) continue;

        // Paquets PUBLISH : premier octet 0x30-0x3F, suivi de la longueur
        // restante (forme courte, < 128 ici car payload $SYS minuscule),
        // puis topic (2 octets de longueur + texte) puis payload brut.
        int offset = 0;
        while (offset + 1 < n) {
            uint8_t controlByte = rx[offset];
            if ((controlByte & 0xF0) != 0x30) break;  // pas un PUBLISH

            uint8_t remLen = rx[offset + 1];
            int topicLenOffset = offset + 2;
            if (topicLenOffset + 1 >= n) break;
            int topicLen = (rx[topicLenOffset] << 8) | rx[topicLenOffset + 1];
            int topicStart = topicLenOffset + 2;
            if (topicStart + topicLen > n) break;

            String topic((const char*)(rx + topicStart), topicLen);
            int payloadStart = topicStart + topicLen;
            int payloadLen   = remLen - 2 - topicLen;
            if (payloadLen < 0 || payloadStart + payloadLen > n) break;

            String value((const char*)(rx + payloadStart), payloadLen);

            if (topic == "$SYS/broker/version") result.brokerVersion = value;
            else if (topic == "$SYS/broker/clients/connected") result.clientsConnected = value;

            offset = payloadStart + payloadLen;
        }

        if (!result.brokerVersion.isEmpty() && !result.clientsConnected.isEmpty()) break;
    }

    client.stop();
    return result;
}
