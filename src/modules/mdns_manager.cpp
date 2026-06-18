/**
 * MdnsManager — Implémentation
 */

#include "mdns_manager.h"
#include "../utils/logger.h"

static const char* TAG = "MdnsMgr";

static const IPAddress MDNS_GROUP(224, 0, 0, 251);
static constexpr uint16_t MDNS_PORT = 5353;

MdnsManager mdnsManager;

void MdnsManager::_ensureMutex() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
}

bool MdnsManager::acquire() {
    _ensureMutex();
    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (!_open) {
        // SO_REUSEADDR activé par beginMulticast — coexistence avec le stack ESPmDNS
        if (_udp.beginMulticast(MDNS_GROUP, MDNS_PORT)) {
            _open = true;
            Log::i(TAG, "Socket mDNS partagé ouvert (224.0.0.251:5353)");
        } else {
            Log::w(TAG, "Impossible de rejoindre 224.0.0.251:5353");
            xSemaphoreGive(_mutex);
            return false;
        }
    }

    _refCount++;
    xSemaphoreGive(_mutex);
    return true;
}

void MdnsManager::release() {
    if (_mutex == nullptr) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (_refCount > 0) _refCount--;
    if (_refCount == 0 && _open) {
        _udp.stop();
        _open = false;
        Log::i(TAG, "Socket mDNS partagé fermé");
    }

    xSemaphoreGive(_mutex);
}

bool MdnsManager::send(const uint8_t* pkt, int len) {
    if (_mutex == nullptr) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);

    bool ok = false;
    if (_open) {
        _udp.beginPacket(MDNS_GROUP, MDNS_PORT);
        _udp.write(pkt, len);
        ok = _udp.endPacket();
    }

    xSemaphoreGive(_mutex);
    return ok;
}

int MdnsManager::poll(uint8_t* buf, int maxLen) {
    if (_mutex == nullptr) return -1;
    xSemaphoreTake(_mutex, portMAX_DELAY);

    int result = -1;
    if (_open) {
        int psize = _udp.parsePacket();
        result = (psize > 0) ? _udp.read(buf, maxLen) : 0;
    }

    xSemaphoreGive(_mutex);
    return result;
}
