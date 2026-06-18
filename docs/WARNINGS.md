# Architecture, Limitations & Points de vigilance — Gateway Lab V1

Ce document regroupe les principales décisions d'architecture, limitations connues,
risques identifiés et points de vigilance pour la maintenance et l'évolution du projet.

---

# Décisions d'architecture

## HTML embarqué en PROGMEM

Le HTML est compilé directement dans le firmware sous forme de tableaux PROGMEM.

Objectifs :

* éviter SPIFFS / LittleFS
* supprimer l'étape "Upload Filesystem Image"
* simplifier les mises à jour OTA
* réduire la complexité du déploiement

Les pages sont servies via `server.send_P()` directement depuis la flash.

---

## API REST locale

L'interface web consomme exclusivement l'API REST locale.

Cette séparation permet :

* de découpler l'interface utilisateur de la logique métier
* de remplacer l'interface web par une application mobile ou desktop
* de simplifier les tests et le débogage

---

## Scanner réseau dans une tâche FreeRTOS dédiée

Le scanner réseau s'exécute dans sa propre tâche FreeRTOS.

Objectifs :

* ne jamais bloquer le serveur web
* ne jamais bloquer OTA
* ne jamais bloquer la gestion Wi-Fi
* permettre des scans longs sans impact sur l'interface

---

## Modèle de données centralisé

Chaque équipement réseau est représenté par une structure unique :

```cpp
struct NetworkDevice {
    String   ip;           // Adresse IPv4
    String   mac;          // Adresse MAC
    String   manufacturer; // Fabricant (OUI / SSDP / API)
    String   hostname;     // Nom d'hôte résolu
    String   category;     // Catégorie : "Router", "NAS", "TV", "SmartHub"…
    String   model;        // Modèle précis : "Freebox Ultra", "DS224+"…
    String   os;           // Système : "FreeboxOS 4.8", "DSM"…
    String   source;       // Méthode : "mDNS", "PTR", "SSDP", "HueAPI"…
    uint32_t lastSeen;     // millis() du dernier scan
    bool     online;       // true si détecté au dernier scan
};
```

Cette structure constitue la source de vérité utilisée par :

* le scanner réseau (ARP)
* le résolveur de noms (mDNS + PTR)
* le scanner SSDP/UPnP et les APIs spécifiques
* l'API REST
* l'interface web

---

## Base OUI locale

L'identification des fabricants repose sur une base OUI embarquée générée à partir de
`data/oui.json`.

Objectifs :

* fonctionnement hors ligne
* aucune dépendance à un service externe
* temps de réponse instantané

---

# Limitations connues

## Résolution des noms d'hôtes (implémentée depuis v0.0.7)

### ℹ️ Deux mécanismes complémentaires

`HostnameResolver` implémente deux stratégies parallèles :

1. **mDNS passif** — écoute les annonces multicast `224.0.0.251:5353` pendant le sweep ARP.
   Capture les enregistrements A des devices compatibles (`.local`).
2. **PTR DNS batch** — envoie des requêtes `d.c.b.a.in-addr.arpa` au DNS du réseau.
   Toutes les requêtes sont envoyées simultanément (fenêtre d'attente unique de 500 ms).

`gethostbyaddr()` n'est pas utilisé (indisponible sur lwIP Arduino ESP32) ;
ces deux mécanismes manuels le remplacent efficacement.

**Champ `source`** : `"mDNS"` (priorité) ou `"PTR"` (fallback).

---

## SSDP — Limitations connues

### ⚠️ Équipements hors UPnP non détectés par SSDP

Le scanner SSDP ne découvre que les devices qui répondent à M-SEARCH multicast.
Les équipements suivants n'apparaissent pas dans les résultats SSDP :

* Smartphones (Android/iOS) — UPnP désactivé par défaut
* Raspberry Pi sans serveur UPnP
* Imprimantes sans UPnP
* Équipements avec pare-feu bloquant le multicast

**Mitigation** : le scan ARP couvre ces équipements indépendamment.
SSDP enrichit les devices ARP et ajoute uniquement les devices UPnP-only.

---

### ⚠️ HTTP GET des descripteurs XML peut échouer

Certains devices annoncent une `LOCATION` qui n'est plus joignable (device éteint
entre l'annonce et le fetch, port changé, redirection non gérée).

**Comportement** : timeout de 2 s, entrée minimale conservée avec `source="SSDP"`
et champs vides — aucun crash, aucun blocage.

---

### ℹ️ LOCATION inutilisable rejetée (depuis v0.8.1)

Certains équipements UPnP mal configurés annoncent une `LOCATION` dont
l'adresse est inutilisable depuis l'ESP32 — la tentative de récupération du
descripteur XML échoue systématiquement (`Connection reset by peer`) ou ne
correspond à aucun équipement réellement joignable sur le réseau scanné :

* `127.0.0.0/8` — boucle locale
* `0.0.0.0` — adresse non initialisée
* `169.254.0.0/16` — lien-local APIPA (auto-configuration Windows)

**Comportement** : ces LOCATION sont détectées et rejetées avant tout essai
de connexion HTTP (`SsdpScanner::scan()`, `ssdp_scanner.cpp`), avec un
avertissement journalisé précisant le cas. Le device n'est tout de même
découvert que via ARP/mDNS/DNS-SD le cas échéant.

---

### ℹ️ APIs spécifiques non authentifiées

Les APIs Hue, Synology et Freebox sont appelées sans authentification.

| API | Endpoint | Données accessibles sans auth |
|---|---|---|
| Hue | `/api/config` | name, modelid, swversion |
| Synology | `/webapi/query.cgi` | confirmation DSM seulement |
| Freebox | `/api_version` | device_name, device_type, firmware_version |

Les APIs authentifiées (ex: liste des lumières Hue, infos disques Synology)
sont réservées à des versions futures.

---

## Limitation de la table ARP lwIP

### ⚠️ ARP_TABLE_SIZE = 10

**Symptôme**

Sur un réseau contenant un grand nombre d'équipements actifs, certaines entrées peuvent
être remplacées avant leur lecture.

**Cause**

La table ARP interne de lwIP est limitée à 10 entrées.

**Mitigation actuelle**

* balayage par lots de 5 adresses
* pause de 100 ms entre les lots
* lecture régulière de la table ARP

Cette approche est suffisante pour un réseau domestique standard.

---

## Débordement de millis()

### ⚠️ Après environ 49 jours

`millis()` déborde après environ 49,7 jours.

L'affichage de "Vu il y a" peut devenir incorrect après un très long fonctionnement
sans redémarrage.

Impact :

* affichage erroné possible
* aucun impact fonctionnel
* aucun risque de crash

---

## Limitation du scan ARP

### ℹ️ Sous-réseau local uniquement

Le scanner ARP ne découvre que les équipements présents sur le même sous-réseau
que l'ESP32.

Les équipements présents sur :

* un autre VLAN
* un autre sous-réseau
* un réseau routé

ne sont pas visibles.

---

## Adresses MAC randomisées

### ℹ️ Smartphones récents

iOS et Android utilisent fréquemment des adresses MAC aléatoires.

Conséquences :

* déduplication moins fiable
* fabricant parfois impossible à identifier
* plusieurs entrées possibles pour un même appareil

---

# Sécurité

## 🔴 Interface web sans authentification

Toutes les routes HTTP sont accessibles depuis le réseau local.

Routes sensibles :

* `POST /api/scan`
* `POST /update`
* `GET /api/system/backup` — télécharge les mots de passe WiFi enregistrés
  en clair (sauvegarde des paramètres de fonctionnement)
* `POST /api/system/restore` — peut enregistrer de nouveaux réseaux WiFi

Ne jamais exposer Gateway Lab directement sur Internet.

Une authentification HTTP Basic est prévue dans une version future.

---

## 🔴 Firmware OTA non signé

Les mécanismes OTA acceptent actuellement tout fichier `.bin`.

Aucune vérification cryptographique n'est effectuée avant le flash.

Impact :

* risque limité au réseau local
* utilisation recommandée uniquement sur un réseau de confiance

---

## 🔴 secrets.h

Depuis la v0.3.0, `secrets.h` n'est plus la méthode officielle de
configuration WiFi (voir `docs/WIFI_SETUP.md`) : elle reste utile uniquement
en développement, pour éviter de repasser par le portail à chaque flash.

Ne jamais committer :

```text
include/secrets.h
```

S'il existe, le fichier contient des identifiants Wi-Fi de développement
(`DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASSWORD`).

Utiliser :

```text
include/secrets_example.h
```

comme modèle versionné.

Vérification avant tout push :

```bash
git status
```

---

## 🟡 Mots de passe WiFi en mémoire NVS

Les réseaux enregistrés via le portail de configuration ou la page
`/wifi` sont stockés **en clair** dans la mémoire NVS de l'ESP32
(`Preferences`, namespace `"wifi"`). Cette mémoire n'est pas chiffrée par
défaut.

* risque limité à un accès physique à la carte (lecture flash) ou à un accès
  réseau local pouvant joindre l'API `/api/wifi`
* l'API REST `/api/wifi` ne renvoie jamais les mots de passe enregistrés
  (uniquement les SSID), mais l'ajout/suppression de réseaux n'est protégé
  par aucune authentification — limiter l'accès au réseau local de confiance
* le portail de configuration (`GatewayLab-Setup`) est lui aussi sans mot de
  passe par conception (accessibilité du premier démarrage) : il n'est actif
  que tant qu'aucun réseau n'est enregistré

---

# Concurrence & tâches FreeRTOS

## ⚠️ std::vector partagé

Tout accès concurrent à un `std::vector` doit être protégé par :

```cpp
xSemaphoreTake(...)
xSemaphoreGive(...)
```

---

## ⚠️ String non thread-safe

Le scanner modifie uniquement son conteneur privé.

Les autres composants utilisent exclusivement les copies retournées par :

```cpp
getResults()
```

---

## ⚠️ Scan et OTA simultanés

Pendant une mise à jour OTA :

```cpp
otaMgr.isUpdating()
```

suspend la tâche scanner.

Objectif :

* éviter les conflits mémoire
* réduire la charge CPU
* garantir la stabilité du flash

---

## ⚠️ Débordement de pile

Les tâches FreeRTOS doivent être surveillées en mode debug via :

```cpp
uxTaskGetStackHighWaterMark()
```

La tâche de scan (`_task`) et la tâche de passe précise (`_rescanTask`)
journalisent leur high-water-mark en fin d'exécution (niveau `DEBUG`).

---

## ⚠️ Mémoire critique — mode dégradé plutôt que redémarrage automatique

Depuis la v0.8.0, `SystemHealth` (`src/modules/system_health.*`) ne
redémarre **jamais** automatiquement l'ESP32 sur condition mémoire basse.
Sous `HEAP_CRITICAL_BYTES` (20 000 octets libres, `include/app_config.h`),
le firmware bascule en **mode dégradé** :

* refus des nouveaux scans et rescans ciblés
* refus des nouvelles notes, modification d'alias/favoris, réinitialisation,
  restauration
* refus de la journalisation de nouveaux événements d'historique
* l'inventaire déjà acquis reste consultable (lecture seule)

Sortie automatique du mode dégradé une fois `HEAP_CRITICAL_BYTES +
HEAP_RECOVERY_MARGIN` octets à nouveau libres ; sinon, redémarrage manuel
via `POST /api/system/restart` (bouton dédié, page Système). Ce choix
évite un redémarrage intempestif qui ferait perdre une session utilisateur
en cours, au prix d'une disponibilité réduite en cas de fuite mémoire réelle
— surveiller `GET /api/system/health` / `GET /api/diagnostics` en cas de
mode dégradé récurrent (signe d'une fuite à corriger plutôt que de
contourner).

**Bornes complémentaires** pour éviter toute croissance non bornée :
`MAX_TRACKED_DEVICES` (300), `MAX_HISTORY_EVENTS` (1000),
`MAX_NOTES_PER_DEVICE` (20), `MAX_NOTE_LENGTH` (256 caractères) — au-delà,
éviction des entrées les plus anciennes (devices, historique FIFO) ou refus
silencieux côté API (notes).

---

## ⚠️ Socket mDNS multicast — conflit avec ESPmDNS (résolu en v0.8.2)

### Historique du problème

Avant v0.8.1, `HostnameResolver` (écoute passive pendant tout le sweep ARP)
et `DnsSdScanner` (requêtes/réponses PTR ponctuelles) ouvraient chacun leur
propre `WiFiUDP` multicast sur `224.0.0.251:5353` : un rescan ciblé
déclenchant `DnsSdScanner::scan()` pendant qu'un scan principal gardait
`HostnameResolver` actif provoquait un échec de bind
(`could not bind socket: 112`).

La v0.8.1 a introduit `MdnsManager`, qui mutualisait un unique socket entre
ces deux modules — mais cette correction ne traitait que le conflit ENTRE
ces deux modules applicatifs. Elle ne prenait pas en compte un troisième
consommateur, toujours actif : le composant mDNS d'ESP-IDF lui-même
(`MDNS.begin()`, appelé dans `wifi_manager.cpp` au démarrage Wi-Fi, log
`[INF][WiFi] mDNS actif : http://gateway-lab-v1.local`), qui garde
`224.0.0.251:5353` exclusivement pour son responder. En conséquence,
`MdnsManager::acquire()` échouait systématiquement dès que le responder
mDNS était actif (log `[WRN][MdnsMgr] Impossible de rejoindre
224.0.0.251:5353`) — c'est-à-dire en pratique en permanence.

### Résolution (v0.8.2)

* **`DnsSdScanner`** a été réécrit pour interroger directement le composant
  mDNS d'ESP-IDF via son API C (`mdns_query_ptr()` / `mdns_query_results_free()`,
  `<mdns.h>`), qui passe par le service mDNS déjà initialisé par
  `MDNS.begin()` — aucun socket applicatif dédié, donc aucun risque de
  conflit de bind.
* **`HostnameResolver`** ne tente plus d'écoute mDNS passive : il n'existe
  pas d'API publique ESP-IDF pour observer passivement les annonces reçues
  par le responder partagé. `begin()`/`update()`/`end()` sont conservés
  comme no-op (compatibilité des appelants) ; seule la résolution PTR DNS
  (port 53, unicast, sans rapport avec ce conflit) reste active.
* `MdnsManager` (`src/modules/mdns_manager.h/.cpp`) est supprimé — devenu
  inutile, plus aucun module n'ouvre de socket multicast applicatif.

**Limite résiduelle** : la résolution de noms d'hôtes via mDNS passif
(`.local`) n'est plus disponible ; seule la résolution PTR DNS (dépendante
du DNS du routeur) fournit désormais un hostname. `DnsSdScanner` continue
de fournir le hostname cible du SRV record en complément, lorsque
disponible.

---

## ⚠️ Scan DNS-SD systématiquement vide (résolu en v0.8.3)

### Symptôme

`[INF][DNSSD] DNS-SD terminé — 0 IP(s) résolue(s)` à chaque scan, malgré la
présence réelle d'objets exposant des services DNS-SD sur le réseau
(Philips Hue, Echo, Synology, etc.).

### Cause

La correction v0.8.2 (ci-dessus) avait éliminé le conflit de bind, mais la
fenêtre d'attente par type de service interrogé via `mdns_query_ptr()`
restait calculée en divisant `timeout_ms` par le nombre de types de service
(~29), avec un plancher de seulement 100 ms. Or la RFC 6762 §6 impose aux
répondeurs un délai aléatoire de 20 à 120 ms avant de répondre à une
question portant sur un enregistrement partagé — cas des PTR de découverte
de service — afin d'éviter une rafale de réponses simultanées. Une fenêtre
de 100 ms (déjà réduite par l'appel précédent du minuteur lwIP/mDNS) ne
laissait quasiment aucune marge pour l'aller-retour réseau au-delà de ce
délai : la quasi-totalité des réponses arrivait hors fenêtre, d'où un scan
vide en pratique.

### Résolution (v0.8.3)

Plancher relevé à 300 ms (`MIN_QUERY_TIMEOUT_MS`, `dns_sd_scanner.cpp`),
valeur par défaut de `DnsSdScanner::scan()` et des deux appels dans
`network_scanner.cpp` ajustée à 9000 ms pour conserver une fenêtre réaliste
sur l'ensemble des types de service interrogés. Sans impact sur le reste du
firmware : le scan DNS-SD tourne dans la tâche FreeRTOS dédiée du scanner
réseau. Les échecs de requête (`mdns_query_ptr()` retournant une erreur)
sont désormais journalisés (`esp_err_to_name()`) pour faciliter un futur
diagnostic.

---

## ⚠️ Changement d'adresse IP

Après une reconnexion Wi-Fi avec attribution d'une nouvelle IP :

```cpp
wifi_manager
```

doit réinitialiser ou notifier :

```cpp
network_scanner
```

afin que le nouveau sous-réseau soit pris en compte.

---

# Pipeline de génération

## Headers générés versionnés

Les fichiers :

```text
include/web_interface.h
include/web_interface_scan.h
include/oui_table.h
```

sont volontairement versionnés dans Git.

Ils doivent être mis à jour après modification de :

```text
web_src/index.html
web_src/scan.html
web_src/topology.html
data/oui.json
```

---

## Génération manuelle

Les scripts Python sont exécutés manuellement avant commit.

Le mécanisme `extra_scripts` de PlatformIO a été retiré pour garantir la portabilité
et éviter certaines erreurs liées au contexte d'exécution SCons.

---

# Matériel

## ESP32-S3 N16R8

Le projet cible actuellement :

```text
ESP32-S3 DevKitC-1 N16R8
```

Configuration de référence :

* 16 Mo Flash
* 8 Mo PSRAM

Tout portage vers une autre carte nécessite une vérification de :

* `platformio.ini`
* `board_config.h`
* la table de partitions

---

## USB CDC

Le port série repose sur :

```ini
board_build.usb_cdc_on_boot = 1
```

Sur certains clones ESP32-S3, un double-appui sur BOOT peut être nécessaire pour
revenir en mode flash en cas de firmware défectueux.
