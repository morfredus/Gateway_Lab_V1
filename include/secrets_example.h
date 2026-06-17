#pragma once

// Dupliquez ce fichier en secrets.h pour le développement local.
// include/secrets.h est ignoré par Git (.gitignore) — ne le committez jamais.
//
// Depuis la v0.3.0, secrets.h n'est PLUS la méthode officielle de
// configuration WiFi : un portail de configuration web s'en charge
// automatiquement au premier démarrage (voir docs/WIFI_SETUP.md).
//
// DEFAULT_WIFI_SSID / DEFAULT_WIFI_PASSWORD restent utiles en développement
// pour éviter de ressaisir le WiFi à chaque flash : ils ne sont utilisés que
// si AUCUN réseau n'est encore enregistré en mémoire NVS de l'ESP32.
//
// Hiérarchie de configuration WiFi (priorité décroissante) :
//   1. Réseaux enregistrés en NVS (via le portail de configuration web)
//   2. DEFAULT_WIFI_SSID / DEFAULT_WIFI_PASSWORD ci-dessous (développement)
//   3. Portail de configuration (point d'accès "GatewayLab-Setup")

#define DEFAULT_WIFI_SSID     "MonWifi"
#define DEFAULT_WIFI_PASSWORD "MonMotDePasse"
