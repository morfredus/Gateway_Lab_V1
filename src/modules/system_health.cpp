#include "system_health.h"
#include "../../include/app_config.h"   // HEAP_WARNING_BYTES, HEAP_CRITICAL_BYTES, HEAP_RECOVERY_MARGIN
#include "../utils/logger.h"

static const char* TAG = "SysHealth";

SystemHealth systemHealth;

void SystemHealth::begin() {
    _degraded = false;
    _reason   = "";
}

void SystemHealth::loop() {
    uint32_t freeHeap = ESP.getFreeHeap();

    if (!_degraded && freeHeap < HEAP_CRITICAL_BYTES) {
        _degraded = true;
        _reason   = "Memoire critique (" + String(freeHeap) + " octets libres)";
        Log::e(TAG, "Mode degrade active — %s. Scans, notes et modifications refuses.", _reason.c_str());
        return;
    }

    if (_degraded && freeHeap > (HEAP_CRITICAL_BYTES + HEAP_RECOVERY_MARGIN)) {
        Log::i(TAG, "Mode degrade desactive — %u octets libres", (unsigned)freeHeap);
        _degraded = false;
        _reason   = "";
        return;
    }

    if (!_degraded && freeHeap < HEAP_WARNING_BYTES && millis() - _lastWarnLogMs > 10000) {
        Log::w(TAG, "Heap bas: %u octets libres", (unsigned)freeHeap);
        _lastWarnLogMs = millis();
    }
}

void SystemHealth::restartNow() {
    Log::i(TAG, "Redemarrage demande par l'utilisateur");
    delay(200);   // laisse le temps au message série/HTTP de partir
    ESP.restart();
}
