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
| mDNS | Accessible via `gateway-lab-v1.local` |
| Interface web | Pages Accueil / Équipements / OTA — HTML embarqué en PROGMEM |
| Scan réseau LAN | Sweep ARP sur tout le sous-réseau, tâche FreeRTOS asynchrone |
| Inventaire équipements | IP · Fabricant · Type · MAC · Vu il y a |
| Identification OUI | 151 entrées, 16 catégories (IoT, Mobile, NAS, Camera, TV…) |
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
⚠️ secrets.h est dans .gitignore — ne jamais le committer.

2. Générer les assets web
python tools/minify_web.py
3. Compiler et flasher
pio run --target upload
4. Accéder à l'interface
http://gateway-lab-v1.local
Structure du projet
Gateway-Lab-V1/
├── src/
│   ├── main.cpp                  # Orchestrateur (30 lignes)
│   ├── modules/
│   │   ├── wifi_manager.*        # WiFiMulti + mDNS + reconnexion
│   │   ├── ota_manager.*         # ArduinoOTA + routes web OTA
│   │   ├── web_server.*          # WebServer + routes API
│   │   └── network_scanner.*     # Scan ARP FreeRTOS + lookup OUI
│   └── utils/
│       └── logger.h              # Log header-only (DEBUG/INFO/WARN/ERROR)
├── include/
│   ├── app_config.h              # Paramètres centralisés (hostname, port…)
│   ├── board_config.h            # Brochage ESP32-S3 (ne pas modifier)
│   ├── secrets_example.h         # Modèle credentials WiFi
│   ├── web_interface*.h          # HTML minifié PROGMEM (généré)
│   └── oui_table.h               # Table OUI C++ (générée)
├── web_src/
│   ├── index.html                # Page d'accueil (source)
│   ├── scan.html                 # Page équipements (source)
│   └── ota.html                  # Page OTA (source)
├── data/
│   └── oui.json                  # Base OUI — 151 entrées, 16 catégories
├── tools/
│   ├── minify_web.py             # Génère les headers depuis web_src/ et data/
│   └── validate_html.py          # Validateur HTML
├── docs/
│   └── WARNINGS.md               # Limitations connues et points de vigilance
├── CHANGELOG.md
└── ROADMAP.md
Workflow de développement web
web_src/*.html  ──┐
data/oui.json   ──┴─ python tools/minify_web.py ──► include/*.h ──► pio run
Les headers générés sont versionnés dans Git — aucun pre-script PlatformIO requis.

API REST
Méthode	Route	Description
GET	/	Page d'accueil
GET	/scan	Page équipements
GET	/update	Page OTA
GET	/api/status	{ssid, ip, rssi, uptime, version, hostname, scanning}
GET	/api/devices	{scanning, devices:[{ip, mac, manufacturer, type, hostname, elapsedMs, online}]}
POST	/api/scan	Déclenche un scan asynchrone
POST	/update	Upload firmware .bin
Versioning
Version	État	Contenu
v0.0.5	✅ Actuelle	OUI externalisé (data/oui.json, 151 entrées), badges Type, pipeline unifié
v0.0.4	✅	Page /scan dédiée, struct NetworkDevice, fix "Vu il y a 56 ans", OTA redirect
v0.0.3	✅	Architecture modulaire src/modules/, scanner ARP FreeRTOS, lookup OUI ~40 entrées
v0.0.2	✅	WebServer, mDNS, OTA web, HTML PROGMEM, pipeline minification
v0.0.1	✅	Structure PlatformIO, board_config, app_config, secrets_example, outils Python
Roadmap
Version	Objectif
v0.0.6	Résolution des noms d'hôtes (requête PTR DNS manuelle via lwip/dns.h)
v0.1.x	Scan de ports, historique NVS, détection nouveaux équipements, /api/export
v0.2.x	mDNS/Bonjour passif, SSDP/UPnP, DNS-SD
v0.3.x	Intégrations domotiques (Philips Hue, Tado, X-Sense)
v0.4.x	MQTT, webhooks événements réseau
v0.5.x	Écran OLED, BLE scan
v1.0.0	Cartographie complète du réseau domotique
Constraints de développement
include/board_config.h — ne pas modifier
include/secrets.h — ne jamais committer
HTML modifiable uniquement dans web_src/
Versioning uniquement dans platformio.ini via PROJECT_VERSION
Après toute modification HTML ou data/oui.json → relancer python tools/minify_web.py
