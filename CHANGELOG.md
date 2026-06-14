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

### Technique
- **Stratégie de stockage HTML** : abandon de SPIFFS au profit de l'injection PROGMEM —
  le HTML est compilé directement dans le firmware, éliminant la partition filesystem et
  l'étape « Upload Filesystem Image »
- **Serving sans RAM** : `server.send_P()` lit `INDEX_HTML` et `OTA_PAGE` depuis la flash
  (PROGMEM) via DMA, sans copie en heap — empreinte RAM quasi nulle pour les pages web
- **`/api/status` sans template** : les données dynamiques (SSID, IP, RSSI, uptime) sont
  injectées côté client par `fetch()` toutes les 10 s, évitant la génération HTML serveur
- **OTA dual-stack** : ArduinoOTA (UDP, port 3232) pour les mises à jour PlatformIO/réseau,
  et `WebServer + Update` (HTTP POST multipart) pour les mises à jour via navigateur —
  les deux coexistent et partagent le même hostname mDNS
- **WiFiMulti** : l'ESP32 tente chaque SSID de `secrets.h` par ordre de signal, avec
  timeout `WIFI_CONNECT_TIMEOUT` (15 s) ; la reconnexion dans `loop()` est non-bloquante
  (`wifiMulti.run()` retourne immédiatement si déjà connecté)
- **Sécurité secrets** : `include/secrets.h` listé dans `.gitignore` dès v0.0.1,
  `secrets_example.h` versionné comme modèle sans donnée réelle

### Infrastructure
- **Pre-script PlatformIO** (`extra_scripts = pre:tools/minify_web.py`) : la minification
  s'exécute automatiquement avant chaque `pio run` — le header C++ est toujours synchronisé
  avec les sources HTML sans intervention manuelle
- **Table `PAGES[]`** dans `minify_web.py` : architecture extensible pour ajouter de
  nouvelles pages sans modifier la logique du script
- **Fallback sans dépendances** : `minify_web.py` fonctionne sans `rcssmin`/`rjsmin` grâce
  à des regex de substitution intégrées ; les librairies optionnelles améliorent simplement
  le taux de compression
- **Dual-mode du script** : détection automatique du contexte d'exécution
  (`Import("env")` PlatformIO vs `__main__` standalone) — un seul fichier pour les deux usages
- **`lib_deps` minimal** : seule dépendance externe ajoutée = `ArduinoJson v7` ;
  WiFi, mDNS, OTA, WebServer, Update sont tous dans le SDK Arduino ESP32
- **Suppression des chemins Windows** dans `platformio.ini` (`build_dir`, `build_cache_dir`) :
  le projet est désormais portable Linux/macOS/Windows sans configuration locale

### Architecture web
```
web_src/index.html  ──minify──►  include/web_interface.h      (INDEX_HTML[] PROGMEM)
web_src/ota.html    ──minify──►  include/web_interface_ota.h  (OTA_PAGE[]  PROGMEM)
```
Aucune étape « Upload Filesystem Image » requise — le HTML voyage avec le firmware.

---

## [0.0.1] — 2026-06-13

### Ajouté
- Structure initiale du projet PlatformIO (ESP32-S3 DevKitC-1 N16R8)
- `include/board_config.h` : brochage complet de la carte (SPI, I2C, GPIO, NeoPixel, capteurs)
- `include/app_config.h` : paramètres centralisés de l'application (timeouts, port, features)
- `include/secrets_example.h` : modèle pour les identifiants WiFi multi-réseaux
- `web_src/` : dossier sources HTML — seul endroit autorisé pour modifier le HTML
- `tools/minify_web.py` : minificateur CSS/JS pour injection dans header C++
- `tools/extract_web_sources.py` : extracteur/beautifier de sources depuis un header existant
- `tools/validate_html.py` : validateur de structure HTML (balises, IDs, i18n)
- `README.md` : roadmap versionnée (v0.0.1 → v1.0.0)
- `BACKLOG.md` : liste des fonctionnalités futures
- `.gitignore` : exclusion de `include/secrets.h`, binaires PlatformIO, caches Python

### Technique
- **Cible matérielle** : ESP32-S3 DevKitC-1 N16R8 (16 Mo flash, 8 Mo PSRAM, dual-core 240 MHz)
- **Standard C++17** activé (`-std=gnu++17`) pour les fonctionnalités modernes du langage
- **SPIFFS** activé en partition filesystem (remplacé en v0.0.2 par PROGMEM)
- **Versioning unique** : `PROJECT_VERSION` défini exclusivement dans `platformio.ini`
  via `-D PROJECT_VERSION='"x.y.z"'`, disponible dans tout le code C++ sans header dédié

### Infrastructure
- **Projet PlatformIO** : environnement `esp32s3_n16r8`, framework Arduino, upload 921 600 baud
- **USB CDC** activé au boot (`board_build.usb_cdc_on_boot = 1`) pour le port série via USB natif
- **Convention de dossiers** : `web_src/` (sources éditables), `include/` (headers générés),
  `data/` (assets SPIFFS), `tools/` (scripts Python), `src/` (code C++)

---

*Ce projet suit la roadmap définie dans `README.md`.*
