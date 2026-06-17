# Gateway Lab V1

![Version](https://img.shields.io/badge/version-0.3.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-orange)
![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-00979D)
![License](https://img.shields.io/badge/license-MIT-green)
![Status](https://img.shields.io/badge/status-en%20développement-yellow)

Passerelle intelligente ESP32-S3 pour la découverte, l'inventaire et l'exploration des équipements connectés au réseau local domestique.

---

## Présentation

Gateway Lab V1 est un projet personnel développé autour d'un ESP32-S3.

L'objectif est de construire progressivement une passerelle réseau autonome capable de :

* Découvrir automatiquement les équipements présents sur le réseau local
* Identifier les appareils détectés
* Enrichir les informations collectées (nom, fabricant, catégorie, modèle...)
* Fournir une interface web locale simple et légère
* Permettre les mises à jour OTA sans accès physique au matériel
* Servir de laboratoire d'expérimentation autour des protocoles réseau domestiques

Le projet privilégie :

* La simplicité de déploiement
* La maintenabilité du code
* La documentation
* La compréhension des mécanismes réseau

---

## Installation rapide

1. Télécharger le firmware depuis la dernière release GitHub
2. Flasher l'ESP32-S3
3. Se connecter au réseau WiFi `GatewayLab-Setup`
4. Configurer le WiFi depuis le navigateur
5. Accéder à Gateway Lab via `http://gateway-lab-v1.local`

Guide détaillé : voir INSTALLATION.md
Guide développeur : voir docs/DEVELOPMENT.md

---

## Matériel cible

**ESP32-S3 DevKitC-1 N16R8**

* 16 Mo Flash
* 8 Mo PSRAM
* Dual Core 240 MHz

---

## Fonctionnalités actuelles

| Fonctionnalité           | Détail                                                            |
| ------------------------ | ----------------------------------------------------------------- |
| WiFi multi-réseaux       | Connexion automatique au réseau enregistré au meilleur signal     |
| Portail de configuration | Point d'accès `GatewayLab-Setup` + page web si aucun réseau n'est connu |
| Persistance NVS          | Réseaux WiFi enregistrés survivant aux redémarrages/coupures      |
| mDNS                     | Accessible via `gateway-lab-v1.local`                             |
| Interface web            | Pages Accueil / Équipements / Historique / OTA / Paramètres       |
| Scan réseau LAN          | Sweep ARP du sous-réseau local                                    |
| Tâche FreeRTOS dédiée    | Scan asynchrone sur Core 0                                        |
| Résolution hostnames     | mDNS passif + DNS inverse PTR                                     |
| Détection box FAI        | Orange, Free, SFR, Bouygues                                       |
| Découverte SSDP/UPnP     | M-SEARCH multicast, parsing XML, catégorisation automatique       |
| API Philips Hue Bridge   | Modèle, firmware, sans authentification (`/api/config`)           |
| API Synology DSM         | Confirmation NAS, modèle depuis XML UPnP                          |
| API Freebox              | Modèle exact (Ultra/Pop/Révolution), version FreeboxOS            |
| Découverte DNS-SD        | 31 types de services, badges HTTP/SSH/AirPlay/Cast/Sonos/HomeKit  |
| Découverte NetBIOS       | Node Status (UDP 137) - hostnames PC Windows / Samba              |
| Scan de ports TCP        | 14 ports, bannières HTTP/SSH/FTP, détection API IoT (Shelly/Tasmota/FritzBox) |
| Inventaire réseau        | IP, nom, fabricant, modèle, catégorie, OS, services, ports, MAC, source |
| Auto-détection ESP32     | Le Gateway apparaît dans sa propre liste                          |
| Identification OUI       | Base externalisée générée automatiquement                         |
| Persistance LittleFS     | Statistiques online/offline, état conservé entre redémarrages     |
| Historique des équipements | Synchronisation NTP, firstSeen/lastSeen/seenCount, journal chronologique (`/history`) |
| Alias utilisateur        | Renommage manuel d'un équipement, prioritaire sur le hostname     |
| Classification intelligente | Affine la catégorie d'un équipement à partir de plusieurs signaux |
| Niveau de confiance      | Score 0-100% expliquant la fiabilité de l'identification, par source |
| Détection des changements | Comparaison automatique entre deux scans (IP, fabricant, catégorie, ports...) |
| Sauvegarde / restauration | Export et import JSON complet de l'inventaire et de l'historique |
| OTA Web                  | Upload firmware depuis le navigateur                              |
| ArduinoOTA               | Mise à jour réseau depuis PlatformIO                              |
| API REST                 | `/api/status`, `/api/devices`, `/api/scan`, `/api/alias`, `/api/history`, `/api/backup`, `/api/restore`, `/api/wifi` |

---

### Accueil

* Informations réseau
* État de connexion
* Accès rapide aux équipements
* Accès OTA

![Accueil](<docs/pictures/Gateway_Lab_V1_Accueil.png>)

### Équipements

* Inventaire des appareils détectés
* Résolution des noms d'hôtes
* Alias utilisateur personnalisable
* Fabricant
* Catégorie
* Modèle
* Temps depuis la dernière détection / nombre de détections

![Equipement](<docs/pictures/Gateway_Lab_V1_Equipement.png>)

### Historique

* Vue chronologique des événements (nouvel équipement, reconnexion, déconnexion, changement)
* Horodatage réel via synchronisation NTP

![Historique](<docs/pictures/Gateway_Lab_V1_Historique.png>)

### OTA

* Mise à jour firmware via navigateur
* Redirection automatique après flash

![OTA](<docs/pictures/Gateway_Lab_V1_OTA.png>)

### Paramètres — Réseau WiFi

* État de la connexion (SSID, IP, signal)
* Liste des réseaux enregistrés
* Ajout / suppression de réseaux WiFi
* Portail de configuration automatique au premier démarrage (point d'accès `GatewayLab-Setup`)

---

## Démarrage rapide

### 1. Configurer le WiFi

**Utilisateur final** (firmware déjà flashé, `gateway-lab-v1.bin`) : aucune
étape requise — connectez-vous au point d'accès `GatewayLab-Setup` au premier
démarrage et suivez le portail de configuration. Détail complet dans INSTALLATION.md
Fonctionnement avancé : docs/WIFI_SETUP.md

**Développeur** (build local, pour éviter de ressaisir le WiFi à chaque
flash) : copier

```text
include/secrets_example.h
```

vers :

```text
include/secrets.h
```

Puis renseigner un réseau de développement :

```cpp
#define DEFAULT_WIFI_SSID     "MonSSID"
#define DEFAULT_WIFI_PASSWORD "MotDePasse"
```

Ce réseau n'est utilisé que si aucun réseau n'est encore enregistré en
mémoire NVS de l'ESP32 (priorité à la configuration utilisateur).

⚠️ `include/secrets.h` est ignoré par Git et ne doit jamais être commité.

---

### 2. Générer les assets web

```bash
python tools/minify_web.py
```

---

### 3. Compiler et flasher

```bash
pio run --target upload
```

---

### 4. Accéder à l'interface

```text
http://gateway-lab-v1.local
```

ou via l'adresse IP affichée sur la page d'accueil ou la page Paramètres.

---

## Structure du projet

```text
Gateway-Lab-V1/
├── src/
│   ├── main.cpp
│   │
│   ├── modules/
│   │   ├── wifi_manager.*
│   │   ├── ota_manager.*
│   │   ├── web_server.*
│   │   ├── network_scanner.*
│   │   ├── hostname_resolver.*
│   │   ├── isp_detector.h
│   │   ├── ssdp_scanner.*
│   │   ├── dns_sd_scanner.*
│   │   ├── netbios_scanner.*
│   │   ├── port_scanner.*
│   │   ├── device_enricher.h
│   │   ├── device_store.*
│   │   ├── device_history.*
│   │   └── time_sync.*
│   │
│   └── utils/
│       └── logger.h
│
├── include/
│   ├── app_config.h
│   ├── board_config.h
│   ├── secrets_example.h
│   ├── secrets.h                # Non versionné
│   ├── oui_table.h              # Généré depuis data/oui.json
│   ├── web_interface.h          # Généré depuis web_src/index.html
│   ├── web_interface_scan.h     # Généré depuis web_src/scan.html
│   ├── web_interface_ota.h      # Généré depuis web_src/ota.html
│   ├── web_interface_history.h  # Généré depuis web_src/history.html
│   └── web_interface_wifi.h     # Généré depuis web_src/wifi.html
│
├── web_src/
│   ├── index.html                # Page d'accueil — HTML uniquement (source)
│   ├── index.js                  # Script de la page d'accueil (source)
│   ├── scan.html                 # Page équipements — HTML uniquement (source)
│   ├── scan.js                   # Script de la page équipements (source)
│   ├── ota.html                  # Page OTA — HTML uniquement (source)
│   ├── ota.js                    # Script de la page OTA (source)
│   ├── history.html              # Page historique — HTML uniquement (source)
│   ├── history.js                # Script de la page historique (source)
│   ├── wifi.html                 # Page Paramètres WiFi — HTML uniquement (source)
│   ├── wifi.js                   # Script de la page Paramètres WiFi (source)
│   ├── styles.css                # Feuille de style unique (injectée inline par minify_web.py)
│   ├── template.html             # Gabarit de référence (documentation)
│   ├── extracted/                # Sortie de extract_web_sources.py (non versionné)
│   └── README.md                 # Guide développeur web_src/
│
├── data/
│   ├── oui.json                 # Base OUI source
│   └── index.html
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DEVELOPMENT.md
│   ├── PROTOCOLS.md
│   ├── WARNINGS.md
│   ├── WIFI_SETUP.md
│   └── pictures/
│
├── tools/
│   ├── minify_web.py
│   ├── extract_web_sources.py
│   └── validate_html.py
│
├── test/
│
├── CHANGELOG.md
├── CONTRIBUTING.md
├── INSTALLATION.md
├── LICENSE
├── ROADMAP.md
├── README.md
└── platformio.ini
```
### Fichiers générés

Les fichiers suivants sont générés automatiquement et versionnés dans Git :

```text
include/oui_table.h
include/web_interface.h
include/web_interface_scan.h
include/web_interface_ota.h
include/web_interface_history.h
include/web_interface_wifi.h
```

Ils sont reconstruits à partir de :

```text
data/oui.json
web_src/*.html
web_src/*.js
```

via :

```bash
python tools/minify_web.py
```

---

## Workflow de développement web

Chaque page web est découpée en trois sources, qui ont chacune un rôle unique :

* `web_src/styles.css` → **tout** le CSS commun (une seule feuille pour les 5 pages)
* `web_src/*.html` → uniquement du HTML/markup (aucun style, aucun script inline)
* `web_src/*.js` → uniquement le JavaScript de la page correspondante

```text
web_src/styles.css     ──┐
web_src/index.html     ──┤
web_src/index.js       ──┤
web_src/scan.html      ──┤
web_src/scan.js        ──┤
web_src/ota.html       ──┼── python tools/minify_web.py ──► include/*.h ──► pio run
web_src/ota.js         ──┤
web_src/history.html   ──┤
web_src/history.js     ──┤
web_src/wifi.html      ──┤
web_src/wifi.js        ──┘
```

`minify_web.py` minifie le CSS et le JS, puis les injecte **inline** dans chaque
page (à la place du `<link rel="stylesheet">` et du `<script src="...">`) avant
de générer le header C++ correspondant. Résultat : l'ESP32 sert chaque page comme
un seul fichier HTML auto-contenu depuis la mémoire flash (PROGMEM), sans serveur
de fichiers statiques.

Note : la page du **portail de configuration WiFi** (`GatewayLab-Setup`,
servie uniquement en mode point d'accès) est une exception volontaire : son
HTML est défini directement dans `src/modules/wifi_manager.cpp`, car elle
doit pouvoir s'afficher avant toute connexion réseau, indépendamment du
pipeline `web_src/`.

Les headers générés sont versionnés dans Git — aucun pre-script PlatformIO requis.

### Outils disponibles

| Outil | Usage |
|---|---|
| `python tools/minify_web.py` | Génère les headers PROGMEM depuis `web_src/` et `data/oui.json` |
| `python tools/validate_html.py` | Valide la structure HTML des 5 pages + gabarit |
| `python tools/extract_web_sources.py` | Prévisualise l'extraction des headers → `web_src/extracted/` (dry-run) |
| `python tools/extract_web_sources.py --force` | Récupération d'urgence : écrit le HTML/JS extrait des headers dans `web_src/extracted/` (sans jamais toucher aux sources originales de `web_src/`) |

---

## API REST

### GET /

Page d'accueil

### GET /scan

Inventaire réseau

### GET /update

Interface OTA

### GET /history

Vue chronologique des événements détectés

### GET /wifi

Page Paramètres → Réseau WiFi (état de connexion, réseaux enregistrés)

### GET /api/status

Retourne :

```json
{
  "ssid": "...",
  "ip": "...",
  "rssi": -42,
  "uptime": "...",
  "version": "...",
  "hostname": "...",
  "scanning": false
}
```

### GET /api/devices

Retourne :

```json
{
  "scanning": false,
  "devices": [...]
}
```

### POST /api/scan

Déclenche un scan réseau asynchrone.

### POST /api/alias

Définit ou efface l'alias d'un équipement (paramètres `mac` et `alias`).

### GET /api/history

Retourne le journal chronologique des événements (les plus récents en premier).

### GET /api/backup

Télécharge un export JSON complet de l'inventaire, des alias et de l'historique.

### POST /api/restore

Restaure l'inventaire depuis un export JSON précédemment généré par `/api/backup`.

### POST /update

Upload d'un firmware `.bin`.

### GET /api/wifi

Retourne :

```json
{
  "connected": true,
  "ssid": "...",
  "ip": "...",
  "rssi": -42,
  "networks": [{"ssid": "Maison"}, {"ssid": "Atelier"}]
}
```

Les mots de passe enregistrés ne sont jamais renvoyés au navigateur.

### POST /api/wifi

Ajoute ou met à jour un réseau enregistré (paramètres `ssid` et `password`).

### DELETE /api/wifi

Supprime un réseau enregistré (paramètre `ssid`).

---

## Sources d'identification

Les informations affichées peuvent provenir de plusieurs mécanismes :

| Source       | Badge UI  | Description                                                     |
| ------------ | --------- | --------------------------------------------------------------- |
| OUI          | —         | Fabricant déduit de l'adresse MAC (base locale `data/oui.json`) |
| PTR          | `DNS↩`   | DNS inverse fourni par la box DHCP                              |
| mDNS         | `mDNS`    | Annonce `.local` captée passivement                             |
| SSDP         | `UPnP`    | Descripteur XML UPnP (M-SEARCH multicast)                       |
| HueAPI       | `Hue`     | API Philips Hue Bridge `/api/config`                            |
| SynologyAPI  | `DSM`     | API Synology DSM `/webapi/query.cgi`                            |
| FreeboxAPI   | `Freebox` | API Freebox `/api_version`                                      |
| NetBIOS      | `NetBIOS` | Node Status NetBIOS (UDP 137) - PC Windows / Samba              |
| Self         | `ESP32`   | Informations de l'ESP32 lui-même                                |

---

## Documentation

| Fichier | Description |
|----------|-------------|
| INSTALLATION.md | Installation et première configuration |
| docs/DEVELOPMENT.md | Compilation, flash et développement |
| docs/WIFI_SETUP.md | Fonctionnement détaillé du portail WiFi |
| docs/ARCHITECTURE.md | Architecture interne du projet |
| docs/PROTOCOLS.md | Protocoles réseau utilisés |
| docs/WARNINGS.md | Limitations connues |
| CHANGELOG.md | Historique des versions |
| ROADMAP.md | Évolutions prévues |

---

## Évolution du projet

Le développement suit une progression volontaire :

1. Découvrir les équipements du réseau
2. Identifier les équipements détectés
3. Comprendre les services qu'ils exposent
4. Construire une cartographie logique du réseau
5. Interagir avec les équipements compatibles

Pour les fonctionnalités prévues, consulter `ROADMAP.md`.

---

## Contraintes de développement

- `include/board_config.h` — ne pas modifier
- `include/secrets.h` — ne jamais committer
- CSS modifiable uniquement dans `web_src/styles.css`
- HTML modifiable uniquement dans `web_src/*.html` (jamais de `<style>` ou `<script>` inline)
- JavaScript modifiable uniquement dans `web_src/*.js`
- Versioning uniquement dans `platformio.ini` via `PROJECT_VERSION`
- Après toute modification de `web_src/` ou `data/oui.json` → relancer `python tools/minify_web.py`

## Licence

Projet personnel open source publié à des fins d'apprentissage, d'expérimentation et de partage de connaissances autour de l'ESP32, du réseau et des systèmes embarqués.