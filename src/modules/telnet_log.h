/**
 * TelnetLog — Miroir du moniteur série sur TCP brut (port TELNET_LOG_PORT)
 *
 * Permet de connecter un terminal comme YAT en TCP plutôt qu'en USB série :
 * un seul client à la fois, écho passif (aucune commande interprétée, c'est
 * un miroir en lecture seule de ce que Serial.print* aurait affiché).
 * Le port série reste actif en parallèle, sans modification de comportement.
 */
#pragma once
#include <Arduino.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class TelnetLog {
public:
    void begin(uint16_t port);

    // Diffuse une ligne au client connecté (no-op si personne n'est connecté).
    // Non bloquant : un client lent ne doit jamais ralentir le scan ni le log série.
    // Thread-safe : Log::* est appelé depuis la boucle principale (core 1) ET
    // depuis les tâches de scan (core 0) — sans le mutex, des écritures
    // concurrentes sur le même socket corrompent le flux vu par YAT.
    void write(const char* data, size_t len);

    // Accepte une nouvelle connexion entrante si aucun client n'est déjà connecté
    // — à appeler depuis loop().
    void loop();

private:
    WiFiServer _server{0};
    WiFiClient _client;
    bool       _started = false;
    SemaphoreHandle_t _mutex = nullptr;
};

extern TelnetLog telnetLog;
