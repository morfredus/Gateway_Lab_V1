#include "telnet_log.h"

TelnetLog telnetLog;

void TelnetLog::begin(uint16_t port) {
    _mutex = xSemaphoreCreateMutex();
    _server = WiFiServer(port);
    _server.begin();
    _server.setNoDelay(true);
    _started = true;
}

void TelnetLog::loop() {
    if (!_started) return;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Un seul client a la fois — une nouvelle connexion remplace l'ancienne
    // (évite de garder un socket mort si YAT a été fermé sans déconnexion propre).
    if (_server.hasClient()) {
        if (_client && _client.connected()) _client.stop();
        _client = _server.available();
    }

    if (_client && !_client.connected()) _client.stop();

    xSemaphoreGive(_mutex);
}

void TelnetLog::write(const char* data, size_t len) {
    if (!_started || !_client) return;

    // Verrou court : protège _client contre une réassignation concurrente
    // par loop() (accept/stop) pendant qu'un autre coeur écrit dessus —
    // sans ça, les écritures depuis la tâche de scan (core 0) et la boucle
    // principale (core 1) se corrompent mutuellement sur le même socket.
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    if (_client.connected()) _client.write((const uint8_t*)data, len);
    xSemaphoreGive(_mutex);
}
