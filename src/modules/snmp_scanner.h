/**
 * SnmpScanner — Interrogation SNMP v1 (GET sysDescr)
 *
 * De nombreuses imprimantes, switches, routeurs, NAS et onduleurs exposent
 * un agent SNMP en lecture publique (communaute "public" par defaut, encore
 * tres repandue sur le materiel grand public/PME non durci). L'objet
 * sysDescr (OID 1.3.6.1.2.1.1.1.0) contient en texte clair le fabricant et
 * le modele exact de l'equipement — une source bien plus precise qu'un
 * banner HTTP generique ou qu'un OUI MAC ambigu.
 *
 * Protocole : un seul paquet UDP/161 (GetRequest SNMPv1 encode en ASN.1 BER)
 * par equipement interroge, reponse attendue sous le meme format.
 * Non bloquant : timeout court, aucune dependance a une librairie SNMP.
 */

#pragma once
#include <Arduino.h>
#include <vector>
#include <map>

class SnmpScanner {
public:
    // Interroge sysDescr sur chaque IP (UDP/161, communaute "public").
    // Retourne IP -> texte sysDescr (entree absente si pas de reponse,
    // SNMP desactive ou communaute differente).
    std::map<String, String> querySysDescr(const std::vector<String>& ips, uint32_t timeout_ms = 300);

private:
    // Construit le paquet GetRequest SNMPv1 pour l'OID sysDescr
    static std::vector<uint8_t> _buildRequest();

    // Decode une longueur ASN.1 BER (forme courte ou longue 1-2 octets)
    // Retourne -1 si le buffer est trop court / format invalide
    static int _berLength(const uint8_t* buf, int len, int offset, int& bytesUsed);

    // Extrait la valeur OCTET STRING qui suit l'OID sysDescr dans la reponse
    static String _parseSysDescr(const uint8_t* buf, int len);
};

extern SnmpScanner snmpScanner;
