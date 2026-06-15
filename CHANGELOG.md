# Changelog — Gateway Lab V1

Toutes les modifications notables sont documentées ici.
Format : [Semantic Versioning](https://semver.org/)

---

## [0.0.5] — 2026-06-15

### Ajouté
- **`data/oui.json`** : source unique pour 151 entrées OUI avec champs `manufacturer` et `category`
  (SBC, IoT, Mobile, Network, Router, NAS, Camera, Printer, Audio, TV, Streaming, Smart Home,
  Home Automation, Robot Vacuum, Security, Computer) — seul fichier à modifier pour enrichir la base
- **`include/oui_table.h`** : header C++ généré automatiquement depuis `data/oui.json` par
  `tools/minify_web.py`, contient `struct OuiEntry{oui, manufacturer, category}` et `OUI_TABLE[]`
- **`doc/warnings.md`** : catalogue des limitations connues, comportements non évidents et points
  de vigilance pour la maintenance et l'évolution du projet

### Modifié
- **`struct NetworkDevice`** (renommé depuis `HostInfo`) : champs refactorisés
  `vendor` → `manufacturer`, `lastSeenMs` → `lastSeen`, ajout `type` (alimenté depuis `category` OUI)
  et `online` — structure extensible pour les futures colonnes de l'interface
- **`tools/minify_web.py`** : traite désormais `data/oui.json` en plus des pages HTML ;
  génère `include/oui_table.h` avec déduplications des OUI et échappement des caractères spéciaux
- **`network_scanner.cpp`** : retire la table OUI inline (100+ lignes), inclut `oui_table.h`,
  `lookupOui()` retourne `const OuiEntry*` et alimente `manufacturer` + `type` en une seule passe
- **`resultsToJson()`** : envoie `elapsedMs = millis() - d.lastSeen` (durée écoulée en ms)
  au lieu du timestamp brut `millis()` — corrige l'affichage **"56 ans"** dans "Vu il y a"
- **`web_src/scan.html`** : `fmtSeen()` reçoit `elapsedMs` directement (plus de `Date.now()`),
  champ `d.vendor` → `d.manufacturer`, couleurs de texte éclaircies pour meilleur contraste
- **`platformio.ini`** : `PROJECT_VERSION` → `0.0.5`

### Technique
- La durée "Vu il y a" était erronée car `millis()` (ms depuis boot ESP32, ex: 5 000)
  était comparé à `Date.now()` (epoch Unix, ex: 1 750 000 000 000) côté navigateur — différence ≈ 56 ans.
  Correction : `resultsToJson()` calcule l'écart côté ESP32 avant serialisation.
- `lookupOui()` retourne `const OuiEntry*` (nullptr si inconnu) plutôt qu'une `String` copiée —
  un seul accès à la table pour remplir deux champs de `NetworkDevice`
- `OUI_TABLE[]` est en flash (`static const`) depuis `oui_table.h` inclus dans le `.cpp` uniquement —
  pas d'exposition dans l'interface publique du module

### Infrastructure
- Workflow de génération unifié : `data/oui.json` + `web_src/*.html` → `python tools/minify_web.py`
  → headers C++ versionnés — aucun autre outil requis
- `minify_web.py` déduplique les OUI au passage (protection contre les doublons dans le JSON)

---

## [0.0.4] — 2026-06-15

### Ajouté
- **Page dédiée équipements** (`web_src/scan.html`) : tableau IP / Fabricant / MAC / Vu il y a, barre de progression animée, polling 2 s pendant le scan, rafraîchissement auto 60 s
- **Navigation** : menu persistant Accueil / Équipements / OTA sur toutes les pages
- **Route `GET /scan`** : sert `SCAN_PAGE` depuis la flash (PROGMEM)
- **`include/web_interface_scan.h`** : header PROGMEM généré depuis `web_src/scan.html`
- **Champ `hostname`** dans `struct NetworkDevice` et déclaration `_resolveHostnames()` (stub) : infrastructure préparée pour la résolution PTR DNS, différée en v0.0.6 — `gethostbyaddr()` non disponible sur lwIP ESP32 (voir `docs/WARNINGS.md`)
- **`ROADMAP.md`** : renommé depuis `BACKLOG.md`

### Modifié
- `src/modules/network_scanner.h` : renommage `HostInfo` → `struct NetworkDevice`, ajout du champ `hostname`
- `src/modules/network_scanner.cpp` : `_resolveHostnames()` déclarée mais non-operative (no-op) — `gethostbyaddr()` introuvable sur cette plateforme ; `resultsToJson()` inclut le champ `hostname` (vide)
- `web_src/index.html` : simplifié (tableau équipements retiré), accès `/scan` via menu et raccourcis
- `tools/minify_web.py` : ajout de `scan.html` dans `PAGES[]`
- `platformio.ini` : `PROJECT_VERSION` → `0.0.4`

### Technique
- `send_P()` pour `/scan` : lecture directe depuis la flash sans copie en RAM
- Le champ `hostname` est transmis dans l'API JSON mais reste vide tant que `_resolveHostnames()` est un no-op

---

## [0.0.3] — 2026-06-15

### Ajouté
- **Architecture modulaire** : `src/` réorganisé en sous-dossiers thématiques
  - `src/modules/` — modules fonctionnels indépendants et transposables
  - `src/utils/` — utilitaires partagés header-only
- **`WiFiManager`** (`modules/wifi_manager.h/.cpp`) : encapsule WiFiMulti, reconnexion
  avec debounce 30 s, callback de connexion, mDNS intégré
- **`OtaManager`** (`modules/ota_manager.h/.cpp`) : encapsule ArduinoOTA + routes web OTA,
  `registerRoutes(WebServer&)` découplé
- **`WebServerModule`** (`modules/web_server.h/.cpp`) : WebServer avec interface
  `ScanProvider` pour découpler le scanner du serveur
- **`NetworkScanner`** (`modules/network_scanner.h/.cpp`) : scan LAN async (FreeRTOS
  task Core 0), sweep UDP du sous-réseau, lecture table ARP lwIP, déduplication par MAC,
  lookup OUI embarqué (~40 fabricants courants)
- **`Log`** (`utils/logger.h`) : wrapper Serial header-only avec niveaux DEBUG/INFO/WARN/ERROR,
  désactivable via `-D LOG_LEVEL=0`
- **Cartouche "Équipements réseau"** dans `web_src/index.html` : table IP / MAC / Fabricant /
  Vu il y a, bouton "Scanner", rafraîchissement auto toutes les 30 s, polling 2 s pendant scan
- **API REST** nouvelles routes :
  - `GET  /api/devices` → `{scanning: bool, devices: [...]}`
  - `POST /api/scan`    → déclenche un scan async

### Modifié
- `src/main.cpp` : réduit à 30 lignes — orchestre uniquement l'initialisation des modules
- `web_src/index.html` : cartouche réseau aligné à la largeur du nouveau tableau équipements

### Technique
- **`ScanProvider`** (struct de lambdas) : interface de découplage entre `WebServerModule`
  et `NetworkScanner` — pas d'include croisé entre modules
- **Thread-safety** : `NetworkScanner` protège `_results` par `SemaphoreHandle_t` ;
  `getResults()` retourne une copie value, jamais une référence mutable
- **Sweep UDP** : envoie un paquet vide sur le port 9 (discard) pour chaque IP du sous-réseau
  → déclenche la résolution ARP sans nécessiter `CONFIG_LWIP_RAW` ni socket ICMP
- **Lecture ARP incrémentale** : `_readArpTable()` appelée tous les 16 hôtes pendant le sweep
  pour capturer les réponses rapides avant que la table lwIP (10 entrées) ne soit réécrasée
- **Déduplication MAC** : un équipement qui change d'IP entre deux scans est mis à jour
  sans doublon dans `_results`
- **`etharp_get_entry()`** : API lwIP 2.x disponible dans le SDK Arduino ESP32 ;
  appelée depuis Core 0 (même core que le stack TCP/IP)

### Infrastructure
- **`src/modules/*.h`** : interfaces conçues pour être copiées dans d'autres projets ESP32
  sans modification (seules `app_config.h` et `secrets.h` varient par projet)
- **PlatformIO auto-discovery** : les `.cpp` dans `src/modules/` et `src/utils/` sont
  compilés automatiquement sans modifier `platformio.ini`
- **`main.cpp` comme orchestrateur** : aucune logique métier, uniquement
  `module.begin()` + `module.loop()` — extension future = ajouter un module, pas modifier main
- **`extra_scripts` corrigé** : déplacé de la section `[platformio]` vers `[env:esp32s3_n16r8]`
  (seul emplacement valide selon la doc PlatformIO)
- **`PROJECT_VERSION`** mis à jour → `0.0.3`
- **Commentaires pédagogiques** ajoutés dans tous les fichiers `.h` et `.cpp` :
  description du rôle de chaque fichier, explication des bibliothèques,
  justification des choix techniques (byte-order, ARP, FreeRTOS, PROGMEM...)

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
