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
fichier `.js` (script de la page) et un `styles.css` commun à toutes les
pages. Un partiel partagé `web_src/menu.html` (la navigation commune) est
inliné dans chaque page via le marqueur `<!-- include:menu.html -->`.
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
[history.html + history.js] → include/web_interface_history.h
[wifi.html + wifi.js]    → include/web_interface_wifi.h
[topology.html + topology.js] → include/web_interface_topology.h
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

Une cartouche **Diagnostics système** affiche également le heap libre, la
PSRAM libre, l'usage LittleFS et les temps moyens de scan/passe précise
(`GET /api/diagnostics`) — utile pour suivre l'impact mémoire et performance
du scan dans la durée.

### Page Équipements (`/scan`)

C'est la page principale.

1. Cliquer sur le bouton **Scanner**
2. Attendre la fin du scan (10 à 30 secondes selon la taille du réseau)
3. La liste des équipements s'affiche automatiquement

Pour rafraîchir un seul équipement sans relancer un scan complet, cliquer
sur le bouton ⟲ dans sa cellule IP (sonde ARP/ICMP ciblée, puis résolution
de nom, scan de ports et NetBIOS).

Le menu **Réinitialiser** permet de vider l'inventaire avec quatre options :
tout effacer, conserver les équipements ayant un alias, conserver ceux dont
le fabricant est connu, ou les deux.

Une barre de **filtres** (type, fabricant, favoris uniquement, en ligne
uniquement) permet de réduire la liste affichée sans relancer de scan ; les
listes déroulantes Type/Fabricant se remplissent automatiquement à partir
des équipements connus.

Le menu **Données** ne propose que l'export de l'inventaire des équipements
(Patch 1 — la Sauvegarde/Restauration des paramètres de fonctionnement a été
déplacée vers la page Système, voir plus bas) :

- **Export CSV** / **Export JSON** — export de l'inventaire des
  équipements. Le CSV (`/api/devices/export.csv`, une ligne par équipement)
  a des dates lisibles, des colonnes en ligne/favori en `Yes`/`No`, et
  inclut les notes et le niveau de confiance ; le BOM UTF-8 en tête de
  fichier garantit un affichage correct des accents dans Excel. Le JSON
  (`/api/backup`) contient l'inventaire complet (alias, notes, historique,
  confiance) avec les dates en epoch, pour une restauration fidèle via
  `/api/restore`.

**Colonnes du tableau :**

| Colonne | Description |
|---|---|
| IP | Adresse IP de l'équipement |
| Nom d'hôte | Nom résolu (badge indique la méthode) |
| Fabricant | Constructeur + modèle + OS si disponible |
| Catégorie | Type d'équipement (Router, NAS, TV…) |
| MAC | Adresse MAC physique |
| Vu il y a | Temps depuis la dernière détection |

Chaque équipement peut être marqué comme **favori** (★) et annoté de notes
libres datées (bouton 📝), utile pour le suivi d'inventaire personnel
(ex : "cartouche changée le 12/05").

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

Affiche une liste chronologique des événements détectés entre deux scans
complets ou deux ticks de surveillance continue : nouvel équipement,
reconnexion, déconnexion, ou changement d'un champ (IP, fabricant,
catégorie...). Les horodatages utilisent l'heure réelle (synchronisée par
NTP au démarrage).

Quatre cases à cocher permettent de filtrer l'affichage par catégorie
d'événement, chacune regroupant plusieurs types d'événements bruts émis par
le firmware (depuis le Patch 4 / v1.0.4) :

| Case de filtre | Types d'événements regroupés |
|---|---|
| Nouveaux équipements | `new` |
| Reconnexions | `online`, `reconnected`, `mobile_returned` |
| Déconnexions | `offline`, `disappeared`, `mobile_left` |
| Changements de champs | `changed`, `identification_improved` |

Une case **favoris uniquement** filtre en plus les événements pour ne
conserver que ceux concernant des équipements actuellement marqués comme
favoris (statut récupéré en direct depuis `/api/devices`, indépendamment de
l'état de l'équipement au moment de l'événement historique).
Le bouton **Vider l'historique** télécharge automatiquement une sauvegarde
JSON du journal avant de le vider côté serveur.

### Page Topologie (`/topology`)

Vue simplifiée (texte) en préparation de la future cartographie réseau
(roadmap v0.4.x) : sépare pour l'instant la ou les passerelles/routeurs
détectés du reste des équipements connus, à partir des données déjà
collectées par le scan. La détection des répéteurs WiFi et une
visualisation graphique des relations entre équipements viendront ensuite.

### Page Système (`/wifi`)

Permet de gérer les réseaux WiFi enregistrés sans recompiler ni reflasher,
et héberge également la mise à jour du firmware (anciennement une page OTA
dédiée) :

- Affiche l'état de la connexion (SSID, adresse IP, signal)
- Liste les réseaux enregistrés en mémoire (NVS)
- Ajoute un nouveau réseau (SSID + mot de passe)
- Supprime un réseau enregistré
- **Surveillance automatique du réseau** (Patch 1) : case d'activation et
  intervalle de scan configurable de 5 minutes à 1 heure
  (`GET`/`POST /api/monitor`, champ `enabled` + `intervalMinutes`,
  persistés en NVS). La surveillance se limite à une détection de présence
  (sweep ARP) : aucun scan rapide ou approfondi n'est déclenché
  automatiquement (Patch 2) — pour une identification complète, lancer un
  scan manuel depuis la page Équipements ou une passe précise sur un
  équipement.
- **Sauvegarde / Restauration** (Patch 1, déplacée depuis la page
  Équipements) des paramètres de fonctionnement du projet
  (`/api/system/backup`, `/api/system/restore`), distincts de l'inventaire :
  réseaux WiFi enregistrés, luminosité NeoPixel, état et fréquence de la
  surveillance automatique, et nom mDNS à titre informatif (fixé à la
  compilation, non restaurable). La restauration ajoute/met à jour les
  réseaux WiFi du fichier sans jamais supprimer les réseaux déjà enregistrés.
- Mise à jour du firmware : sélectionner le fichier `.bin`
  (`.pio/build/esp32s3_n16r8/firmware.bin` après `pio run`) et cliquer
  **Mettre à jour** — envoyé via `POST /update` (inchangé)

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
