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

    // Parcourt la table de pontage (Bridge MIB, dot1dTpFdbTable, OID
    // 1.3.6.1.2.1.17.4.3.1.1) d'un equipement par une suite de requetes
    // GetNextRequest SNMPv1 — l'index de cette table est l'adresse MAC
    // elle-meme, donc chaque reponse donne directement une MAC pontee par
    // l'equipement interroge (donc rattachee a lui sur le reseau). Retourne
    // un ensemble vide si l'equipement ne repond pas (pas d'agent SNMP, ou
    // MIB non supportee — le cas frequent des repeteurs mesh grand public).
    std::vector<String> walkBridgeMacTable(const String& ip, uint32_t timeout_ms = 300, int maxEntries = 64);

private:
    // Construit le paquet GetRequest SNMPv1 pour l'OID sysDescr
    static std::vector<uint8_t> _buildRequest();

    // Decode une longueur ASN.1 BER (forme courte ou longue 1-2 octets)
    // Retourne -1 si le buffer est trop court / format invalide
    static int _berLength(const uint8_t* buf, int len, int offset, int& bytesUsed);

    // Encode une longueur ASN.1 BER (forme courte si <128, sinon 1 octet de
    // prefixe + 1 octet de valeur - suffisant pour tous les paquets SNMP
    // construits ici, toujours tres courts)
    static std::vector<uint8_t> _encodeLength(size_t len);

    // Extrait la valeur OCTET STRING qui suit l'OID sysDescr dans la reponse
    static String _parseSysDescr(const uint8_t* buf, int len);

    // Encode une suite d'arcs OID (ex: {1,3,6,1,2,1,17,4,3,1,1}) au format
    // ASN.1 BER (les deux premiers arcs sont combines en un seul octet,
    // chaque arc suivant en base-128 avec bit de continuation)
    static std::vector<uint8_t> _encodeOid(const std::vector<uint32_t>& arcs);

    // Decode un OID BER (sans le tag/longueur, deja consommes par l'appelant)
    // en suite d'arcs
    static std::vector<uint32_t> _decodeOid(const uint8_t* buf, int len);

    // Construit un paquet GetNextRequest SNMPv1 pour l'OID donne
    static std::vector<uint8_t> _buildGetNextRequest(const std::vector<uint32_t>& oid);

    // Decode la reponse a un GetNextRequest : OID retourne (arcs) + valeur
    // brute (octets). Retourne false si le paquet est mal forme.
    static bool _parseGetNextResponse(const uint8_t* buf, int len,
                                       std::vector<uint32_t>& outOid,
                                       std::vector<uint8_t>& outValue);
};

extern SnmpScanner snmpScanner;
