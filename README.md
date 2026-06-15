# Gateway Lab V1

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

## Matériel cible

**ESP32-S3 DevKitC-1 N16R8**

* 16 Mo Flash
* 8 Mo PSRAM
* Dual Core 240 MHz

---

## Fonctionnalités actuelles

| Fonctionnalité        | Détail                                                            |
| --------------------- | ----------------------------------------------------------------- |
| WiFi multi-réseaux    | Connexion automatique au meilleur réseau disponible (`secrets.h`) |
| mDNS                  | Accessible via `gateway-lab-v1.local`                             |
| Interface web         | Pages Accueil / Équipements / OTA                                 |
| Scan réseau LAN       | Sweep ARP du sous-réseau local                                    |
| Tâche FreeRTOS dédiée | Scan asynchrone sur Core 0                                        |
| Résolution hostnames  | mDNS passif + DNS inverse PTR                                     |
| Détection box FAI     | Orange, Free, SFR, Bouygues                                       |
| Inventaire réseau     | IP, nom, fabricant, modèle, catégorie, MAC                        |
| Auto-détection ESP32  | Le Gateway apparaît dans sa propre liste                          |
| Identification OUI    | Base externalisée générée automatiquement                         |
| OTA Web               | Upload firmware depuis le navigateur                              |
| ArduinoOTA            | Mise à jour réseau depuis PlatformIO                              |
| API REST              | `/api/status`, `/api/devices`, `/api/scan`                        |

---

## Captures d'écran

### Accueil

* Informations réseau
* État de connexion
* Accès rapide aux équipements
* Accès OTA

### Équipements

* Inventaire des appareils détectés
* Résolution des noms d'hôtes
* Fabricant
* Catégorie
* Modèle
* Temps depuis la dernière détection

### OTA

* Mise à jour firmware via navigateur
* Redirection automatique après flash

---

## Démarrage rapide

### 1. Configurer les identifiants WiFi

Copier :

```text
include/secrets_example.h
```

vers :

```text
include/secrets.h
```

Puis renseigner les réseaux :

```cpp
static const char* WIFI_NETWORKS[][2] = {
    {"MonSSID", "MotDePasse"},
    {"Hotspot", "AutreMotDePasse"}
};
```

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

ou via l'adresse IP affichée sur la page d'accueil.

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
│   │   └── isp_detector.h
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
│   └── web_interface_ota.h      # Généré depuis web_src/ota.html
│
├── web_src/
│   ├── index.html
│   ├── scan.html
│   ├── ota.html
│   ├── styles.css
│   ├── template.html
│   └── README.md
│
├── data/
│   ├── oui.json                 # Base OUI source
│   └── index.html
│
├── docs/
│   └── WARNINGS.md
│
├── tools/
│   ├── minify_web.py
│   ├── extract_web_sources.py
│   └── validate_html.py
│
├── test/
│
├── CHANGELOG.md
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
```

Ils sont reconstruits à partir de :

```text
data/oui.json
web_src/*.html
```

via :

```bash
python tools/minify_web.py
```

---

## Workflow de développement web

```text
web_src/*.html
        │
        ├─────► tools/minify_web.py
        │
data/oui.json
        │
        ▼
include/*.h générés
        │
        ▼
pio run
```

Les fichiers générés sont volontairement versionnés dans Git.

Aucun script PlatformIO automatique n'est nécessaire.

---

## API REST

### GET /

Page d'accueil

### GET /scan

Inventaire réseau

### GET /update

Interface OTA

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

### POST /update

Upload d'un firmware `.bin`.

---

## Sources d'identification

Les informations affichées peuvent provenir de plusieurs mécanismes :

| Source | Description                             |
| ------ | --------------------------------------- |
| OUI    | Fabricant déduit de l'adresse MAC       |
| PTR    | DNS inverse fourni par la box DHCP      |
| mDNS   | Annonces `.local` détectées passivement |
| Self   | Informations de l'ESP32 lui-même        |

---

## Documentation

| Fichier          | Description                                |
| ---------------- | ------------------------------------------ |
| CHANGELOG.md     | Historique détaillé des versions           |
| ROADMAP.md       | Fonctionnalités planifiées                 |
| docs/WARNINGS.md | Limitations connues et points de vigilance |

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

* `include/board_config.h` est considéré comme non modifiable par l'utilisateur
* `include/secrets.h` ne doit jamais être commité
* Les pages web sont modifiées dans `web_src/`
* Les headers web sont générés automatiquement
* La version du projet est définie dans `platformio.ini`

Après toute modification de :

```text
web_src/*
data/oui.json
```

relancer :

```bash
python tools/minify_web.py
```

avant compilation.

---

## Licence

Projet personnel open source publié à des fins d'apprentissage, d'expérimentation et de partage de connaissances autour de l'ESP32, du réseau et des systèmes embarqués.
