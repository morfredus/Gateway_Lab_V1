# Guide de démarrage — Gateway Lab V1

Ce guide explique comment installer, configurer et utiliser Gateway Lab V1
en partant de zéro. Aucune connaissance avancée n'est requise.

---

## Ce que fait ce projet

Gateway Lab V1 est un programme qui tourne sur une petite carte électronique (ESP32-S3)
connectée à votre réseau WiFi domestique.

Une fois démarré, il :

1. Se connecte à votre WiFi
2. Détecte automatiquement tous les appareils présents sur le réseau
3. Identifie chaque appareil (nom, fabricant, modèle, catégorie)
4. Affiche un inventaire complet dans une interface web accessible depuis n'importe quel navigateur

---

## Matériel nécessaire

| Composant | Détail |
|---|---|
| Carte cible | ESP32-S3 DevKitC-1 N16R8 |
| Câble USB | USB-C vers votre ordinateur |
| Réseau WiFi | 2.4 GHz (WPA2) |
| Ordinateur | Windows, macOS ou Linux |

---

## Logiciels à installer

### 1. Python 3

Télécharger depuis [python.org](https://python.org).

Vérification :

```bash
python --version
```

### 2. PlatformIO

PlatformIO est l'outil de compilation utilisé par ce projet.

```bash
pip install platformio
```

Vérification :

```bash
pio --version
```

### 3. (Optionnel) VS Code + extension PlatformIO

L'extension PlatformIO pour VS Code offre une interface graphique pour compiler et flasher.

---

## Installation pas à pas

### Étape 1 — Récupérer le code source

```bash
git clone https://github.com/morfredus/Gateway_Lab_V1.git
cd Gateway_Lab_V1
```

### Étape 2 — Configurer le WiFi (optionnel pour le développement)

Depuis la v0.3.0, **il n'est plus nécessaire de modifier le code pour configurer
le WiFi** : un portail de configuration web s'en charge automatiquement au
premier démarrage. Voir `docs/WIFI_SETUP.md` pour le détail complet.

Pour le développement uniquement, si vous préférez ne pas ressaisir vos
identifiants WiFi à chaque flash, vous pouvez créer `include/secrets.h` :

```bash
cp include/secrets_example.h include/secrets.h
```

Puis renseigner un réseau de développement :

```cpp
#define DEFAULT_WIFI_SSID     "NomDuReseau"
#define DEFAULT_WIFI_PASSWORD "MotDePasse"
```

Ce fichier est ignoré par Git (`.gitignore`) et n'est utilisé que si aucun
réseau n'est encore enregistré dans la mémoire de l'ESP32 (NVS).

> **Important** : Ne jamais commiter `secrets.h`. Ne jamais partager ce fichier.

### Étape 3 — Générer les fichiers web

Les pages HTML sont embarquées directement dans le firmware.
Leurs sources se trouvent dans `web_src/` : un fichier `.html` (markup), un
fichier `.js` (script de la page) et un `styles.css` commun aux 4 pages.
Avant de compiler, il faut les transformer en headers C++ (`include/*.h`) :

```bash
python tools/minify_web.py
```

Le script minifie le CSS et le JavaScript, les injecte directement dans
chaque page HTML, puis génère un header `.h` par page. Vous devriez voir une
sortie comme :

```
[styles.css]  → injecté inline dans chaque page
[index.html + index.js]  → include/web_interface.h
[scan.html + scan.js]    → include/web_interface_scan.h
[ota.html + ota.js]      → include/web_interface_ota.h
[history.html + history.js] → include/web_interface_history.h
[wifi.html + wifi.js]    → include/web_interface_wifi.h
[oui.json]    → include/oui_table.h
OK
```

## Automatisation de la génération des assets

Par défaut, les assets web sont générés manuellement :

```bash
python tools/minify_web.py
```

Cette approche facilite le développement et permet de contrôler
précisément les fichiers générés avant un commit.

Une automatisation est disponible via :

```ini
extra_scripts = pre:tools/custom_hooks.py
```

Le hook exécute automatiquement `tools/minify_web.py` avant chaque build
et interrompt la compilation si la génération échoue.

### Étape 4 — Compiler et flasher

Brancher la carte ESP32-S3 via USB, puis :

```bash
pio run --target upload
```

> Si le port USB n'est pas détecté automatiquement, le préciser manuellement :
> `pio run --target upload --upload-port /dev/ttyUSB0` (Linux/macOS)
> ou `COM3` (Windows).

### Étape 5 — Surveiller le démarrage

```bash
pio device monitor
```

Vous devriez voir dans le terminal :

```
=== GatewayLabV1 v0.3.0 ===
[WiFi] Connexion en cours...
[WiFi] Connecté : 192.168.1.42
[Scanner] Module initialisé
[WebSrv] Serveur démarré sur le port 80
```

Si aucun réseau n'est encore configuré (premier démarrage, sans `secrets.h`),
vous verrez à la place :

```
=== GatewayLabV1 v0.3.0 ===
[WiFi] Aucun réseau disponible — démarrage du portail de configuration
[WiFi] Portail de configuration actif — SSID "GatewayLab-Setup" — http://192.168.4.1
```

Dans ce cas, suivez `docs/WIFI_SETUP.md` pour connecter l'ESP32 à votre WiFi
depuis un téléphone ou un ordinateur, sans rien recompiler.

### Étape 6 — Accéder à l'interface web

Ouvrir un navigateur et aller sur :

```
http://gateway-lab-v1.local
```

ou en utilisant l'adresse IP affichée dans le terminal (ex: `http://192.168.1.42`).

---

## Utiliser l'interface web

### Page Accueil (`/`)

Affiche les informations de connexion :
- Réseau WiFi connecté
- Adresse IP attribuée
- Niveau du signal (RSSI)
- Durée de fonctionnement (uptime)
- Version du firmware

### Page Équipements (`/scan`)

C'est la page principale.

1. Cliquer sur le bouton **Scanner**
2. Attendre la fin du scan (10 à 30 secondes selon la taille du réseau)
3. La liste des équipements s'affiche automatiquement

**Colonnes du tableau :**

| Colonne | Description |
|---|---|
| IP | Adresse IP de l'équipement |
| Nom d'hôte | Nom résolu (badge indique la méthode) |
| Fabricant | Constructeur + modèle + OS si disponible |
| Catégorie | Type d'équipement (Router, NAS, TV…) |
| MAC | Adresse MAC physique |
| Vu il y a | Temps depuis la dernière détection |

**Badges source :**

| Badge | Signification |
|---|---|
| `mDNS` | Nom trouvé via annonce `.local` |
| `DNS↩` | Nom fourni par la box DHCP (DNS inverse) |
| `UPnP` | Descripteur XML via SSDP |
| `Hue` | API Philips Hue Bridge |
| `DSM` | API Synology |
| `Freebox` | API Freebox |
| `ESP32` | L'ESP32 lui-même |

### Page Historique (`/history`)

Affiche une liste chronologique des événements détectés entre deux scans :
nouvel équipement, reconnexion, déconnexion, ou changement d'un champ
(IP, fabricant, catégorie...). Les horodatages utilisent l'heure réelle
(synchronisée par NTP au démarrage).

### Page OTA (`/update`)

Permet de mettre à jour le firmware sans rebrancher la carte :

1. Compiler le firmware : `pio run`
2. Trouver le fichier `.bin` dans `.pio/build/esp32s3_n16r8/firmware.bin`
3. Le sélectionner sur la page OTA et cliquer **Mettre à jour**

### Page Paramètres (`/wifi`)

Permet de gérer les réseaux WiFi enregistrés sans recompiler ni reflasher :

- Affiche l'état de la connexion (SSID, adresse IP, signal)
- Liste les réseaux enregistrés en mémoire (NVS)
- Ajoute un nouveau réseau (SSID + mot de passe)
- Supprime un réseau enregistré

Voir `docs/WIFI_SETUP.md` pour le détail complet de la configuration WiFi.

---

## Résolution des problèmes fréquents

### La carte n'est pas détectée par USB

Sur certains ESP32-S3, il faut appuyer deux fois rapidement sur le bouton **BOOT**
pour entrer en mode flash, puis relancer `pio run --target upload`.

### `gateway-lab-v1.local` ne répond pas

1. Vérifier que la carte est bien connectée (LED allumée, pas de reboot en boucle)
2. Utiliser l'adresse IP directement (visible dans `pio device monitor`)
3. Sur Windows, mDNS peut nécessiter Bonjour (installé avec iTunes ou disponible séparément)

### `secrets.h : No such file or directory`

Le fichier `secrets.h` n'a pas encore été créé. Voir **Étape 2** ci-dessus.

### Le scan ne trouve aucun équipement

- Vérifier que l'ESP32 est bien connecté au même réseau WiFi que les équipements à scanner
- Attendre 5–10 secondes après le démarrage avant de lancer un scan
- Le scan ARP ne fonctionne que sur le sous-réseau local (même réseau WiFi)

### La résolution des noms ne fonctionne pas

- Les noms via mDNS nécessitent que les appareils supportent Bonjour/mDNS
- Les noms via PTR DNS nécessitent que la box enregistre les noms DHCP
- Certains appareils (smartphones récents) n'exposent pas leur nom

---

## Workflow de développement

Si vous souhaitez modifier le projet :

```
1. Modifier web_src/*.html, web_src/*.js ou web_src/styles.css
           ↓
2. python tools/minify_web.py    (régénère les headers C++)
           ↓
3. pio run --target upload       (compile et flashe)
           ↓
4. pio device monitor            (surveille les logs)
```

**Règles importantes :**
- Chaque type de contenu va dans son propre fichier source : HTML dans
  `web_src/*.html`, CSS dans `web_src/styles.css`, JavaScript dans
  `web_src/*.js` — jamais de `<style>` ou `<script>` inline dans le HTML
- Ne jamais modifier les fichiers `include/web_interface*.h` à la main
  (ils sont régénérés à chaque exécution de `minify_web.py`)
- La version du firmware n'est définie qu'une seule fois, dans `platformio.ini`
- En cas de perte des fichiers `web_src/`, `python tools/extract_web_sources.py
  --force` permet de les reconstruire (dans `web_src/extracted/`) à partir
  des headers `include/*.h` déjà générés

---

## Pour aller plus loin

- `docs/ARCHITECTURE.md` — Comment le projet est construit et pourquoi
- `docs/PROTOCOLS.md` — Les protocoles réseau utilisés expliqués simplement
- `docs/WARNINGS.md` — Limitations connues et points de vigilance
- `ROADMAP.md` — Fonctionnalités planifiées pour les prochaines versions
