/**
 * MqttScanner — Sondage cible d'un broker MQTT (port 1883)
 *
 * Beaucoup d'installations domotiques (Home Assistant, Zigbee2MQTT,
 * Tasmota, ESPHome...) s'appuient sur un broker MQTT local, souvent sans
 * authentification sur reseau prive. Au-dela de la simple ouverture du
 * port 1883 (deja detectee par PortScanner), se connecter brievement en
 * client MQTT permet de recuperer deux informations supplementaires :
 *
 *   - le code CONNACK, qui indique si une authentification est exigee
 *     (0 = accepte sans identifiants, 4/5 = identifiants requis/refuses)
 *   - le contenu des topics standards `$SYS/broker/version` et
 *     `$SYS/broker/clients/connected`, publies par la plupart des brokers
 *     (Mosquitto en tete) et qui revelent le logiciel/version du broker
 *     et le nombre de clients connectes
 *
 * Volontairement limite a ces topics `$SYS` : on ne souscrit jamais aux
 * topics applicatifs des appareils (`#`), qui releveraient de l'inventaire
 * des donnees du foyer plutot que de l'identification du broker lui-meme.
 *
 * Protocole : MQTT v3.1.1 sur TCP, encodage/decodage manuel des paquets
 * CONNECT/CONNACK/SUBSCRIBE/PUBLISH (pas de dependance a une librairie MQTT).
 */

#pragma once
#include <Arduino.h>
#include <vector>

struct MqttProbeResult {
    bool   reachable     = false;  // Un CONNACK a ete recu
    bool   authRequired  = false;  // CONNACK refuse (code != 0)
    String brokerVersion;          // $SYS/broker/version
    String clientsConnected;       // $SYS/broker/clients/connected
};

class MqttScanner {
public:
    // Se connecte au broker MQTT sur l'IP/port donnes, tente un CONNECT
    // anonyme puis souscrit aux topics $SYS/broker/version et
    // $SYS/broker/clients/connected. timeout_ms : delai global d'attente
    // des PUBLISH apres souscription.
    MqttProbeResult probe(const String& ip, uint16_t port = 1883, uint32_t timeout_ms = 600);

private:
    static std::vector<uint8_t> _buildConnect();
    static std::vector<uint8_t> _buildSubscribe();
};

extern MqttScanner mqttScanner;
