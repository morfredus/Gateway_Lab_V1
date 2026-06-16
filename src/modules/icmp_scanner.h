/**
 * IcmpScanner — Sonde ICMP echo (ping) pour découverte complémentaire
 *
 * Utilisé en complément de l'ARP sweep pour trouver les équipements
 * qui ne répondent pas aux ARP Request (filtrage pare-feu, IoT limités…).
 * Uniquement appliqué aux IP non découvertes par ARP.
 *
 * pingWithTtl() retourne aussi la valeur TTL de la réponse IP, utilisée
 * pour déduire le système d'exploitation :
 *   TTL > 200 → Cisco / équipement réseau
 *   TTL > 100 → Windows  (initial 128, décrément par saut)
 *   TTL >  50 → Linux / Android / macOS / iOS  (initial 64)
 */

#pragma once
#include <Arduino.h>
#include <vector>

// Résultat ping enrichi avec valeur TTL
struct PingResult {
    String  ip;
    uint8_t ttl;   // 0 si aucune réponse
};

class IcmpScanner {
public:
    // Sonde séquentiellement chaque IP. Retourne les IP qui répondent.
    // timeout_ms : délai d'attente de réponse par IP (recommandé : 150-300 ms)
    std::vector<String> ping(const std::vector<String>& ips,
                             uint32_t timeout_ms = 200);

    // Identique à ping() mais retourne aussi la valeur TTL pour chaque réponse.
    std::vector<PingResult> pingWithTtl(const std::vector<String>& ips,
                                        uint32_t timeout_ms = 200);

private:
    // Retourne le TTL reçu (> 0) ou 0 si pas de réponse
    uint8_t _pingOne(const String& ip, uint32_t timeout_ms);
};

extern IcmpScanner icmpScanner;
