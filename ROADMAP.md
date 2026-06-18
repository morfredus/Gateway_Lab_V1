# Roadmap — Gateway Lab V1

Fonctionnalités planifiées par ordre de priorité décroissante.

---

## En cours / Prochaine version

* Équipements favoris
* Export CSV
* Authentification du portail de configuration WiFi et de l'API `/api/wifi`
* Reconnexion automatique vers le portail si tous les réseaux enregistrés
  échouent durablement (actuellement uniquement au tout premier démarrage)

---

## Roadmap produit

### v0.1.x — Inventaire enrichi (terminé en v0.2.0)

* ~~Historique persistant des équipements~~
* ~~Première apparition / dernière apparition~~
* ~~Compteur de détections~~
* Équipements favoris
* ~~Notes utilisateur sur un équipement~~ (alias)
* ~~Export JSON complet~~ (sauvegarde/restauration)
* Export CSV

### v0.2.x — Découverte avancée
* Fingerprinting TCP/IP stack avancé (TTL + TCP options + window size)
* ~~SNMP v1/v2c (community "public") pour équipements réseau (port UDP 161)~~ (terminé en v0.6.0, sondage `sysDescr` lors de la passe précise)

### v0.3.x — Configuration WiFi sans recompilation (terminé en v0.3.0)

* ~~Portail de configuration WiFi (point d'accès + page captive)~~
* ~~Persistance NVS multi-réseaux (`Preferences`)~~
* ~~Page Paramètres → Réseau WiFi (liste, ajout, suppression)~~
* Authentification du portail / de l'API `/api/wifi`
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

### v0.6.x — Détection avancée (terminé en v0.6.0)

* ~~Sous-catégorie (`type`) au sein d'une catégorie~~
* ~~Score de confiance unique et plus prudent (minimum des signaux), détail par champ~~
* ~~Passe précise asynchrone avec suivi de progression affiché sous la ligne de l'équipement~~
* ~~Sondage SNMP `sysDescr`~~
* ~~Découverte WS-Discovery / ONVIF (caméras, imprimantes, NAS)~~
* ~~API HTTP propriétaires des appareils multimédia courants (Cast, Sonos, Roku, Samsung)~~
* ~~Service DNS-SD Matter (`_matterc._udp`)~~

### v0.7.x — Matériel

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
| v0.0.9  | Scanner DNS-SD (RFC 6763) — 22 types de services, badges services HTTP/SSH/AirPlay/Cast/Sonos/HomeKit…, champ services dans NetworkDevice et API |
| v0.1.0  | Persistance LittleFS, statistiques online/offline, UI barre de statistiques et colonne statut |
| v0.1.1  | Scanner TCP 14 ports (sockets non-bloquants), banner HTTP, TTL → OS, badges ports TCP colorés dans l'UI |
| v0.1.2  | Scanner NetBIOS (UDP 137), enrichissement par patterns hostname, bannières SSH/FTP, détection API IoT (Shelly/Tasmota/FritzBox), DNS-SD étendu à 31 types de services, badge source NetBIOS |
| v0.2.0  | Historique des équipements (NTP, firstSeen/lastSeen/seenCount), alias utilisateur, classification intelligente multi-signaux, vue chronologique (/history), détection des changements, sauvegarde/restauration JSON |
| v0.2.1  | Niveau de confiance de l'identification (score 0-100%, badge couleur, libellé de source au survol) |
| v0.2.2  | Séparation HTML/CSS/JS dans `web_src/` (scripts extraits dans `*.js`, minifiés et injectés inline par `minify_web.py`), correction du lien Historique manquant sur la page OTA |
| v0.3.0  | Portail de configuration WiFi (point d'accès + page captive), persistance NVS multi-réseaux, page Paramètres → Réseau WiFi (`/wifi`), API `/api/wifi` |
| v0.4.0  | Détection des adresses MAC privées/aléatoires (catégorie `Mobile/Aléatoire`) |
| v0.5.0  | Réinitialisation de l'inventaire avec options de conservation (alias, fabricant), réinterrogation ciblée d'un équipement sans scan complet, filtrage et effacement (avec sauvegarde) de l'historique |
| v0.6.0  | Sous-catégorie (`type`), score de confiance unique et plus prudent, passe précise asynchrone avec barre de progression sous la ligne de l'équipement, sondage SNMP, découverte WS-Discovery/ONVIF, API appareils multimédia (Cast/Sonos/Roku/Samsung), service DNS-SD Matter, amélioration du contraste des textes/badges en thème sombre |
