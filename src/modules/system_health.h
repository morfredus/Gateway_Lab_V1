/**
 * SystemHealth — Garde-fou mémoire et mode dégradé
 *
 * Surveille le heap libre (loop() à appeler sans interruption, non bloquant).
 * Sous HEAP_CRITICAL_BYTES, bascule en mode dégradé plutôt que de redémarrer
 * automatiquement : l'utilisateur garde l'accès en lecture à l'inventaire déjà
 * acquis et décide lui-même du redémarrage (bouton "Redémarrer" — page
 * Paramètres). Sort du mode dégradé avec hystérésis (HEAP_RECOVERY_MARGIN)
 * si le heap se libère (ex: redémarrage du serveur web, fin d'un pic).
 *
 * En mode dégradé, les opérations qui consomment de la mémoire ou écrivent
 * sur la flash sont refusées par leurs modules respectifs (chacun appelle
 * systemHealth.isDegraded() avant d'agir) :
 *   - nouveaux scans / rescans (network_scanner)
 *   - nouvelles notes (network_scanner)
 *   - journalisation des événements (device_history)
 *   - modification de configuration (alias, favoris, reset, restauration WiFi)
 */

#pragma once
#include <Arduino.h>

class SystemHealth {
public:
    void begin();

    // À appeler à chaque tour de loop() — non bloquant
    void loop();

    bool          isDegraded() const { return _degraded; }
    const String& reason()     const { return _reason; }

    // Redémarrage explicite demandé par l'utilisateur (bouton page Paramètres)
    void restartNow();

private:
    bool     _degraded     = false;
    String   _reason;
    uint32_t _lastWarnLogMs = 0;
};

extern SystemHealth systemHealth;
