/**
 * TimeSync - Implementation
 *
 * configTime() programme le client SNTP integre a l'ESP-IDF. La synchronisation
 * se fait en arriere-plan ; isSynced() teste simplement si l'annee resultante
 * est plausible (> 2020) pour distinguer un epoch valide d'un epoch par defaut.
 */

#include "time_sync.h"
#include <time.h>
#include "../utils/logger.h"

static const char* TAG = "TimeSync";

TimeSync timeSync;

void TimeSync::begin() {
    // GMT+0, pas de DST - l'heure relative suffit pour l'historique
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    Log::i(TAG, "Synchronisation NTP demarree");
}

bool TimeSync::isSynced() {
    time_t now = time(nullptr);
    return now > 1600000000;   // ~2020-09 - epoch plausible
}

uint32_t TimeSync::nowEpoch() {
    if (!isSynced()) return 0;
    return (uint32_t)time(nullptr);
}
