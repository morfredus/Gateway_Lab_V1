# Gateway Lab V1

Passerelle intelligente ESP32-S3 pour la découverte et l'inventaire des équipements
connectés au réseau local domestique.

---

## Matériel cible

**ESP32-S3 DevKitC-1 N16R8** — 16 Mo flash · 8 Mo PSRAM · dual-core 240 MHz

---

## Fonctionnalités actuelles

| Fonctionnalité | Détail |
|---|---|
| WiFi multi-réseaux | Connexion automatique au meilleur réseau disponible (`secrets.h`) |
| mDNS | Accessible via `gateway-lab-v1.local` ; résolution passive des `.local` pendant le scan |
| Interface web | Pages Accueil / Équipements / OTA — HTML embarqué en PROGMEM |
| Scan réseau LAN | Sweep ARP sur tout le sous-réseau, tâche FreeRTOS asynchrone Core 0 |
| Résolution hostnames | mDNS passif (annonces `.local`) + PTR DNS batch (≤ 500 ms) |
| Détection boxes FAI | Free / Orange / SFR / Bouygues — modèle identifié via hostname + OUI |
| Inventaire équipements | IP · Nom d'hôte · Fabricant · Modèle · Catégorie · MAC · Source · Vu il y a |
| Identification OUI | 152 entrées (`data/oui.json`), 16 catégories (IoT, Mobile, NAS, Camera, TV…) |
| OTA web | Upload firmware `.bin` via navigateur + redirection automatique |
| OTA réseau | Mise à jour via PlatformIO / IDE (ArduinoOTA) |
| API REST | `/api/status` · `/api/devices` · `/api/scan` |

---

## Démarrage rapide

### 1. Configurer les identifiants WiFi

Copier `include/secrets_example.h` → `include/secrets.h` et renseigner les réseaux :

```cpp
static const char* WIFI_NETWORKS[][2] = {
    {"MonSSID",    "MotDePasse"},
    {"Hotspot",    "AutreMotDePasse"},
};
```

> ⚠️ `secrets.h` est dans `.gitignore` — ne jamais le committer.

### 2. Générer les assets web

```bash
python tools/minify_web.py
```

### 3. Compiler et flasher

```bash
pio run --target upload
```

### 4. Accéder à l'interface

```
http://gateway-lab-v1.local
```

---

## Structure du projet

```
Gateway-Lab-V1/
├── src/
│   ├── main.cpp                  # Orchestrateur (~65 lignes)
│   ├── modules/
│   │   ├── wifi_manager.*        # WiFiMulti + mDNS + reconnexion
│   │   ├── ota_manager.*         # ArduinoOTA + routes web OTA
│   │   ├── web_server.*          # WebServer + routes API
│   │   ├── network_scanner.*     # Scan ARP FreeRTOS + lookup OUI
│   │   ├── hostname_resolver.*   # mDNS passif + PTR DNS batch
│   │   └── isp_detector.h        # Détection boxes FAI FR (header-only)
│   └── utils/
│       └── logger.h              # Log header-only (DEBUG/INFO/WARN/ERROR)
├── include/
│   ├── app_config.h              # Paramètres centralisés (hostname, port…)
│   ├── board_config.h            # Brochage ESP32-S3 (ne pas modifier)
│   ├── secrets_example.h         # Modèle credentials WiFi
│   ├── web_interface.h           # HTML page d'accueil PROGMEM (généré)
│   ├── web_interface_scan.h      # HTML page équipements PROGMEM (généré)
│   ├── web_interface_ota.h       # HTML page OTA PROGMEM (généré)
│   └── oui_table.h               # Table OUI C++ (générée, 151 entrées après dédup)
├── web_src/
│   ├── index.html                # Page d'accueil (source)
│   ├── scan.html                 # Page équipements (source)
│   ├── ota.html                  # Page OTA (source)
│   ├── template.html             # Gabarit commun (navigation, style)
│   ├── app.js                    # JavaScript principal
│   ├── app-lite.js               # JavaScript réduit (page OTA)
│   └── styles.css                # Feuille de style commune
├── data/
│   └── oui.json                  # Base OUI — 152 entrées, 16 catégories
├── tools/
│   ├── minify_web.py             # Génère les headers depuis web_src/ et data/
│   ├── extract_web_sources.py    # Extracteur/beautifier depuis un header existant
│   └── validate_html.py          # Validateur HTML
├── docs/
│   └── WARNINGS.md               # Limitations connues et points de vigilance
├── CHANGELOG.md
└── ROADMAP.md
```

---

## Workflow de développement web

```
web_src/*.html  ──┐
data/oui.json   ──┴─ python tools/minify_web.py ──► include/*.h ──► pio run
```

Les headers générés sont versionnés dans Git — aucun pre-script PlatformIO requis.

---

## API REST

| Méthode | Route | Description |
|---------|-------|-------------|
| GET | `/` | Page d'accueil |
| GET | `/scan` | Page équipements |
| GET | `/update` | Page OTA |
| GET | `/api/status` | `{ssid, ip, rssi, uptime, version, hostname, scanning}` |
| GET | `/api/devices` | `{scanning, devices:[{ip, mac, manufacturer, hostname, category, model, os, source, elapsedMs, online}]}` |
| POST | `/api/scan` | Déclenche un scan asynchrone |
| POST | `/update` | Upload firmware `.bin` |

> Le champ `source` indique la méthode de résolution du nom : `"mDNS"` (annonce .local), `"PTR"` (reverse DNS), `"MAC"` (OUI uniquement), ou `""` si inconnu.

---

## Versioning

| Version | État | Contenu |
|---------|------|---------|
| v0.0.7 | ✅ Actuelle | Résolution hostnames (mDNS passif + PTR DNS batch), détection boxes FAI FR, nouvelle `struct NetworkDevice`, badges source + modèle dans l'UI |
| v0.0.6 | ✅ | Corrections de bugs : reconnexion WiFi relance les services, mDNS republié, mutex guard, callbacks OTA idempotents, `resultsToJson()` sécurisé (ArduinoJson) |
| v0.0.5 | ✅ | OUI externalisé (`data/oui.json`, 152 entrées), badges Type, pipeline unifié |
| v0.0.4 | ✅ | Page `/scan` dédiée, `struct NetworkDevice`, fix "Vu il y a 56 ans", OTA redirect, champ `hostname` (stub) |
| v0.0.3 | ✅ | Architecture modulaire `src/modules/`, scanner ARP FreeRTOS, lookup OUI ~40 entrées |
| v0.0.2 | ✅ | WebServer, mDNS, OTA web, HTML PROGMEM, pipeline minification |
| v0.0.1 | ✅ | Structure PlatformIO, board_config, app_config, secrets_example, outils Python |

---

## Roadmap

| Version | Objectif |
|---------|----------|
| v0.0.8 | Scan de ports, détection nouveaux équipements, `/api/export` |
| v0.1.x | Historique NVS, mDNS/Bonjour passif étendu, SSDP/UPnP |
| v0.2.x | mDNS/Bonjour passif, SSDP/UPnP, DNS-SD |
| v0.3.x | Intégrations domotiques (Philips Hue, Tado, X-Sense) |
| v0.4.x | MQTT, webhooks événements réseau |
| v0.5.x | Écran OLED, BLE scan |
| v1.0.0 | Cartographie complète du réseau domotique |

---

## Contraintes de développement

- `include/board_config.h` — ne pas modifier
- `include/secrets.h` — ne jamais committer
- HTML modifiable uniquement dans `web_src/`
- Versioning uniquement dans `platformio.ini` via `PROJECT_VERSION`
- Après toute modification HTML ou `data/oui.json` → relancer `python tools/minify_web.py`
