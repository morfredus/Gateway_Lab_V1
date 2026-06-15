# Roadmap — Gateway Lab V1

Fonctionnalités planifiées par ordre de priorité décroissante.

---

## En cours / Prochaine version (v0.0.9)

* Scan de ports TCP (HTTP, HTTPS, SSH, MQTT, SMB…)
* Détection des nouveaux équipements depuis le dernier scan
* Endpoint `/api/export` (JSON complet)
* Historique local des équipements (NVS)

---

## Roadmap produit

### v0.1.x — Inventaire enrichi

* Historique persistant des équipements (NVS)
* Première apparition / dernière apparition
* Compteur de détections
* Équipements favoris
* Notes utilisateur sur un équipement
* Export JSON complet
* Export CSV

### v0.2.x — Découverte avancée

* DNS-SD (découverte de services via mDNS)
* Identification automatique des services exposés (port scan + bannières)
* Détection du système d'exploitation (heuristiques TTL, TCP/IP stack)
* Corrélation multi-sources pour les champs encore vides

### v0.3.x — Intégrations domotiques

* Tado (thermostats)
* X-Sense (capteurs)
* Détection des caméras Xiaomi
* Intégrations d'objets connectés locaux supplémentaires

### v0.4.x — Topologie réseau

* Détection automatique de la passerelle
* Détection des points d'accès WiFi
* Identification des switches connus
* Cartographie logique du réseau
* Relations entre équipements
* Visualisation graphique du réseau

### v0.5.x — Connectivité

* MQTT
* Webhooks
* Événements réseau temps réel
* API d'intégration externe

### v0.6.x — Matériel

* Écran OLED
* Analyse Bluetooth Low Energy
* Découverte des équipements BLE
* Interface locale embarquée

### v1.0.0 — Gateway réseau complète

* Inventaire réseau avancé
* Découverte multi-protocoles
* Cartographie réseau
* Historique persistant
* Intégrations domotiques
* Export et API
* Interface web unifiée

---

## ✅ Réalisé

| Version | Fonctionnalité                                                                                                                                                     |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| v0.0.1  | Structure PlatformIO, board_config.h, app_config.h, secrets_example.h, outils Python                                                                               |
| v0.0.2  | WiFiMulti, ArduinoOTA, WebServer, mDNS, HTML PROGMEM, pipeline de génération                                                                                       |
| v0.0.3  | Architecture modulaire, NetworkScanner FreeRTOS, sweep ARP, lookup OUI                                                                                             |
| v0.0.4  | Page Équipements dédiée, navigation multi-pages, struct NetworkDevice, infrastructure hostname                                                                     |
| v0.0.5  | Base OUI externalisée, catégories d'équipements, pipeline unifié, documentation des limitations                                                                    |
| v0.0.6  | Corrections de robustesse, reconnexion WiFi, mDNS, OTA, JSON sécurisé, modules idempotents                                                                         |
| v0.0.7  | Résolution des noms d'hôtes (mDNS + PTR), détection des box FAI, modèle/source/catégorie, ESP32 visible dans sa propre liste, amélioration complète de l'interface |
| v0.0.8  | Scanner SSDP/UPnP complet, parsing XML robuste, catégorisation automatique, APIs Hue Bridge / Synology DSM / Freebox, nouveaux badges source UI |
