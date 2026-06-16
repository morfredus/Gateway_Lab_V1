/**
 * IcmpScanner — Sonde ICMP echo (ping) pour découverte complémentaire
 *
 * Utilisé en complément de l'ARP sweep pour trouver les équipements
 * qui ne répondent pas aux ARP Request (filtrage pare-feu, IoT limités…).
 * Uniquement appliqué aux IP non découvertes par ARP.
 */

#pragma once
#include <Arduino.h>
#include <vector>

class IcmpScanner {
public:
    // Sonde séquentiellement chaque IP. Retourne les IP qui répondent.
    // timeout_ms : délai d'attente de réponse par IP (recommandé : 150-300 ms)
    std::vector<String> ping(const std::vector<String>& ips,
                             uint32_t timeout_ms = 200);

private:
    bool _pingOne(const String& ip, uint32_t timeout_ms);
};

extern IcmpScanner icmpScanner;
