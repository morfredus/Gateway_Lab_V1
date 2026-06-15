# Roadmap — Gateway Lab V1

Fonctionnalités planifiées par ordre de priorité décroissante.

---

## En cours / Prochaine version (v0.0.6)

- **Résolution des noms d'hôtes** : implémentation d'une requête PTR DNS manuelle
  (construire `x.x.x.x.in-addr.arpa` et interroger le DNS du routeur via `lwip/dns.h`)
  en remplacement du `gethostbyaddr()` non disponible sur lwIP ESP32 — voir `docs/WARNINGS.md`

---

## Roadmap produit

### v0.1.x — Inventaire enrichi
- **Scan de ports** : détection des services actifs sur chaque équipement (HTTP, SSH, MQTT, SMB…)
- **Détection de nouveaux équipements** : notification à l'apparition d'un MAC inconnu (log + API)
- **Historique des équipements** : persistance des appareils vus entre les redémarrages (NVS ESP32)
- **Endpoint `/api/export`** : téléchargement de l'inventaire complet en JSON

### v0.2.x — Découverte passive
- **mDNS/Bonjour passif** : écoute des annonces `_http._tcp`, `_mqtt._tcp`, etc.
- **SSDP/UPnP** : découverte des équipements annoncés (TV, imprimantes, NAS…)
- **DNS-SD** : agrégation des services annoncés par type

### v0.3.x — Intégrations domotiques
- **Philips Hue** : liste des ampoules, état on/off, couleur via API locale
- **Tado** : lecture des thermostats et zones de chauffe
- **X-Sense** : capteurs de fumée et CO connectés
- **Détection caméras Xiaomi** : identification par signature HTTP/mDNS

### v0.4.x — Connectivité
- **MQTT** : publication des équipements détectés sur un broker externe
- **Webhook** : notification HTTP sur événement (nouveau MAC, équipement hors ligne…)

### v0.5.x — Matériel
- **Écran OLED** : affichage local du nombre d'équipements et de l'IP
- **Analyse Bluetooth Low Energy** : découverte des équipements BLE à proximité

---

## ✅ Réalisé

| Version | Fonctionnalité |
|---------|----------------|
| v0.0.1 | Structure PlatformIO, `board_config.h`, `app_config.h`, `secrets_example.h`, outils Python |
| v0.0.2 | WiFiMulti, ArduinoOTA, WebServer, mDNS, HTML PROGMEM (abandon SPIFFS), pipeline minification |
| v0.0.3 | Architecture modulaire (`src/modules/`, `src/utils/`), NetworkScanner FreeRTOS, sweep ARP lwIP, OUI ~40 entrées |
| v0.0.4 | Page `/scan` dédiée, navigation multi-pages, `struct NetworkDevice`, fix "Vu il y a 56 ans", OTA redirect, champ `hostname` préparé (stub — `gethostbyaddr()` indisponible sur lwIP ESP32) |
| v0.0.5 | OUI externalisé dans `data/oui.json` (152 entrées, 16 catégories), `include/oui_table.h` généré (151 entrées après déduplication), colonne Type avec badges colorés, `minify_web.py` unifié, `docs/WARNINGS.md` |
