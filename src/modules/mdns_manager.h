/**
 * MdnsManager — Socket multicast mDNS partagé (224.0.0.251:5353, RFC 6762)
 *
 * Avant v0.8.1, HostnameResolver et DnsSdScanner ouvraient chacun leur propre
 * WiFiUDP et appelaient indépendamment beginMulticast(224.0.0.251, 5353).
 * HostnameResolver garde son socket ouvert pendant toute la durée du scan ARP
 * (écoute passive) ; si une repasse précise (rescan ciblé) déclenche en
 * parallèle DnsSdScanner::scan() pendant cette fenêtre, le second appel à
 * beginMulticast() échoue ("could not bind socket: 112") puisque les deux
 * tentent de rejoindre le même groupe/port simultanément.
 *
 * MdnsManager mutualise un unique WiFiUDP, partagé par tous les modules qui
 * ont besoin de 224.0.0.251:5353. Comptage de références : le socket reste
 * ouvert tant qu'au moins un module l'utilise, fermé quand le dernier le
 * libère. Un mutex protège l'ouverture/fermeture et les accès socket
 * (acquire/release/send/poll) contre les accès concurrents entre tâches
 * FreeRTOS (scan principal vs. tâche de repasse précise).
 *
 * Limite connue : les paquets reçus ne sont pas dupliqués entre modules —
 * chaque paquet est consommé par le premier appelant de poll(). En pratique
 * les deux modules ne lisent pas simultanément le même flux de réponses
 * (HostnameResolver écoute passivement des annonces spontanées, DnsSdScanner
 * lit les réponses à ses propres requêtes PTR), donc le risque de perte
 * croisée reste marginal — voir docs/WARNINGS.md.
 */

#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class MdnsManager {
public:
    // Rejoint 224.0.0.251:5353 si c'est le premier appelant (comptage de
    // références incrémenté à chaque appel). Retourne true si le socket est
    // utilisable (déjà ouvert ou ouverture réussie).
    bool acquire();

    // Décrémente le compteur de références ; ferme le socket si plus aucun
    // module ne l'utilise.
    void release();

    // Envoie un paquet multicast vers 224.0.0.251:5353. Retourne false en
    // cas d'échec d'envoi ou si le socket n'est pas ouvert.
    bool send(const uint8_t* pkt, int len);

    // Lit un paquet en attente (non bloquant). Retourne la taille lue (>0),
    // 0 si aucun paquet disponible, -1 si le socket n'est pas ouvert.
    int poll(uint8_t* buf, int maxLen);

private:
    WiFiUDP          _udp;
    int              _refCount = 0;
    bool             _open     = false;
    SemaphoreHandle_t _mutex   = nullptr;

    void _ensureMutex();
};

// Instance globale — partagée par HostnameResolver et DnsSdScanner
extern MdnsManager mdnsManager;
