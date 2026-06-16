/**
 * NetBiosScanner - Decouverte des noms d'hotes via NetBIOS Name Service (UDP 137)
 *
 * Tres efficace pour les PC Windows et serveurs Samba qui ne repondent pas
 * a mDNS/PTR DNS - la requete "Node Status" (RFC 1001/1002) retourne le nom
 * de la machine et son groupe de travail sans configuration prealable.
 *
 * Principe :
 *   1. Envoi d'une requete Node Status (nom NBSTAT "*") sur le port UDP 137
 *   2. La machine repond avec la liste de ses noms NetBIOS enregistres
 *   3. On extrait le premier nom UNIQUE de type Workstation/Server
 *
 * Limite : uniquement les hotes IPv4 ayant NetBIOS over TCP/IP actif
 * (desactive par defaut sur certaines configurations Windows modernes,
 * mais reste tres repandu sur les reseaux domestiques).
 */

#pragma once
#include <Arduino.h>
#include <vector>
#include <map>

struct NetBiosInfo {
    String hostname;    // Nom de la machine (ex: "DESKTOP-ABC123", "PC-FRED")
    String workgroup;   // Groupe de travail / domaine NetBIOS
};

class NetBiosScanner {
public:
    // Sonde chaque IP avec une requete Node Status NetBIOS.
    // timeout_ms : delai d'attente de reponse par IP (recommande : 200-300 ms)
    std::map<String, NetBiosInfo> scan(const std::vector<String>& ips,
                                        uint32_t timeout_ms = 250);

private:
    NetBiosInfo _queryOne(const String& ip, uint32_t timeout_ms);
};

extern NetBiosScanner netBiosScanner;
