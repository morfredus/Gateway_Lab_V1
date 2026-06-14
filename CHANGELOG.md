# Changelog — Gateway Lab V1

Toutes les modifications notables sont documentées ici.
Format : [Semantic Versioning](https://semver.org/)

---

## [0.0.2] — 2026-06-14

### Ajouté
- **WiFiMulti** : gestion multi-réseaux depuis `include/secrets.h`, reconnexion automatique dans `loop()`
- **ArduinoOTA** : mise à jour réseau via PlatformIO/IDE (hostname configurable)
- **WebServer** (port 80) avec les routes :
  - `GET /` → page d'accueil embarquée en PROGMEM
  - `GET /api/status` → JSON `{ssid, ip, rssi, uptime, version, hostname}`
  - `GET /update` → page de mise à jour OTA
  - `POST /update` → upload firmware `.bin` (librairie `Update`)
- **mDNS** : accès via `gateway-lab-v1.local` (configurable via `MDNS_HOSTNAME`)
- **Page d'accueil** (`web_src/index.html`) : titre, cartouche réseau (SSID, IP, RSSI, mDNS, Uptime, Statut), bouton OTA, rafraîchissement automatique toutes les 10 s
- **Page OTA** (`web_src/ota.html`) : formulaire upload firmware avec barre de progression
- **Minification automatique** (`tools/minify_web.py`) : génère les headers PROGMEM avant chaque compilation PlatformIO
  - `web_src/index.html` → `include/web_interface.h` (`INDEX_HTML`)
  - `web_src/ota.html`   → `include/web_interface_ota.h` (`OTA_PAGE`)
- **`include/app_config.h`** : ajout de `MDNS_HOSTNAME`, `WEB_SERVER_PORT`
- **`CHANGELOG.md`** : ce fichier

### Modifié
- `src/main.cpp` : implémentation complète (stub remplacé)
- `platformio.ini` : `PROJECT_VERSION` → `0.0.2`, ajout `lib_deps` (ArduinoJson v7), `extra_scripts` (minification pre-build), suppression des chemins Windows
- `tools/minify_web.py` : refonte complète — traite plusieurs pages, génère des headers PROGMEM paramétrables, dual-mode standalone/PlatformIO

### Architecture web
```
web_src/index.html  ──minify──►  include/web_interface.h     (INDEX_HTML[] PROGMEM)
web_src/ota.html    ──minify──►  include/web_interface_ota.h (OTA_PAGE[]  PROGMEM)
```
Aucune étape « Upload Filesystem Image » requise — le HTML voyage avec le firmware.

---

## [0.0.1] — 2026-06-13

### Ajouté
- Structure initiale du projet PlatformIO (ESP32-S3 DevKitC-1 N16R8)
- `include/board_config.h` : brochage complet de la carte
- `include/app_config.h` : paramètres de l'application
- `include/secrets_example.h` : modèle pour les identifiants WiFi
- `web_src/` : dossier sources HTML avec outils de minification
- `tools/minify_web.py` : minificateur CSS/JS pour header C++
- `tools/extract_web_sources.py` : extracteur de sources depuis le header
- `tools/validate_html.py` : validateur de structure HTML
- `README.md` et `BACKLOG.md`
- `.gitignore` incluant `include/secrets.h`

---

*Ce projet suit la roadmap définie dans `README.md`.*
