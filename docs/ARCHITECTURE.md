# Architecture — Gateway Lab V1

Ce document explique comment le projet est structuré, pourquoi ces choix ont été faits,
et comment les différentes parties s'articulent entre elles.

---

## Vue d'ensemble

```
┌─────────────────────────────────────────────────────────────┐
│                         ESP32-S3                             │
│                                                              │
│  ┌──────────────┐    ┌──────────────┐   ┌────────────────┐  │
│  │  WiFiManager  │    │  OtaManager  │   │ WebServerModule│  │
│  │              │    │              │   │                │  │
│  │ WiFi multi   │    │ ArduinoOTA   │   │ GET /          │  │
│  │ reconnexion  │    │ Web upload   │   │ GET /scan      │  │
│  │ mDNS init    │    │              │   │ GET /api/*     │  │
│  └──────┬───────┘    └──────────────┘   └───────┬────────┘  │
│         │                                        │           │
│         │ callback "WiFi connecté"               │ ScanProvider
│         ▼                                        │ (lambdas)  │
│  ┌──────────────────────────────────────┐        │           │
│  │           NetworkScanner             │◄───────┘           │
│  │                                      │                    │
│  │  FreeRTOS Task (Core 0)              │                    │
│  │  ┌────────────────────────────────┐  │                    │
│  │  │ 1. ARP sweep (sous-réseau)     │  │                    │
│  │  │ 2. mDNS passif (HostnameRes.)  │  │                    │
│  │  │ 3. PTR DNS batch               │  │                    │
│  │  │ 4. ISP detection               │  │                    │
│  │  │ 5. SSDP/UPnP (SsdpScanner)    │  │                    │
│  │  │ 6. APIs Hue/Synology/Freebox   │  │                    │
│  │  │ 7. DNS-SD (DnsSdScanner)       │  │                    │
│  │  │ 8. Self entry (ESP32 lui-même) │  │                    │
│  │  └────────────────────────────────┘  │                    │
│  │                                      │                    │
│  │  _results : vector<NetworkDevice>    │                    │
│  │  Protégé par mutex FreeRTOS          │                    │
│  └──────────────────────────────────────┘                    │
│                                                              │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ HTTP (port 80)
                              ▼
                    ┌─────────────────┐
                    │    Navigateur   │
                    │                 │
                    │  /             │
                    │  /scan         │
                    │  /history      │
                    │  /topology     │
                    │  /wifi         │
                    │  /api/devices  │
                    └─────────────────┘
```

---

## Modules C++

### `src/main.cpp` — Orchestrateur

**Rôle** : initialiser les modules dans le bon ordre, rien de plus.

Chaque module expose `begin()` et optionnellement `loop()`. `main.cpp` les appelle
dans l'ordre correct. Toute la logique métier est dans les modules.

**Règle** : si vous ajoutez une fonctionnalité, créez un module, n'ajoutez pas de
logique dans `main.cpp`.

---

### `src/modules/wifi_manager.*` — Gestion WiFi

**Rôle** : connexion WiFi multi-réseaux, persistance NVS et portail de configuration.

Hiérarchie de configuration (priorité décroissante) :
1. Réseaux enregistrés en NVS (`Preferences`, namespace `"wifi"`) — configurés
   via le portail web ou la page `/wifi`
2. `DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASSWORD` dans `include/secrets.h`
   (développement uniquement, optionnel — fichier inclus via `__has_include`)
3. Portail de configuration : point d'accès `GatewayLab-Setup` + `DNSServer`
   (captif) + `WebServer` dédié sur le port 80, actif uniquement quand aucun
   réseau ne répond

Fonctionnement :
- Essaie chaque réseau connu (NVS puis fallback dev) par ordre de signal via `WiFiMulti`
- Si aucun ne répond avant `WIFI_CONNECT_TIMEOUT`, démarre le portail de
  configuration et **ne revient pas** tant qu'aucun réseau n'a été enregistré
  (l'ESP32 redémarre après la saisie utilisateur, via `ESP.restart()`)
- Une fois connecté, appelle un callback applicatif (défini dans `main.cpp`)
- Détecte la déconnexion dans `loop()` et rappelle automatiquement `_multi.run()`
- Relance le mDNS après chaque reconnexion
- Expose `savedNetworks()` / `addNetwork()` / `removeNetwork()` pour la gestion
  multi-réseaux depuis l'API web (`/api/wifi`)

**Particularité** : tous les services réseau (OTA, web server, scanner) sont initialisés
dans le callback, pas dans `setup()`. Cela garantit qu'ils ne démarrent que si le WiFi est actif.
En mode portail (point d'accès), aucun de ces services ne démarre : seul le
portail de configuration tourne, jusqu'au redémarrage suivant la connexion.

---

### `src/modules/network_scanner.*` — Scanner réseau

**Rôle** : découvrir tous les équipements du réseau et construire l'inventaire.

Le scan se déroule en 7 phases dans une **tâche FreeRTOS dédiée sur le Core 0** :

```
Phase 1 : ARP sweep
  → Envoie des ARP Request sur chaque IP du sous-réseau (par lots de 5)
  → Lit la table ARP lwIP après chaque lot (capacité max : 10 entrées)

Phase 2 : (retirée en v0.8.2 — voir HostnameResolver ci-dessous)

Phase 3 : PTR DNS batch
  → Envoie des requêtes DNS inverse pour chaque IP découverte
  → Toutes les requêtes en parallèle, une seule attente de 500 ms

Phase 4 : Détection ISP
  → Analyse le hostname + OUI pour identifier les box françaises
  → Renseigne manufacturer, model, category="Router"

Phase 5 : SSDP/UPnP
  → Envoie M-SEARCH multicast, collecte les réponses
  → Fetch le descripteur XML de chaque device UPnP
  → Appelle les APIs Hue/Synology/Freebox si le device correspond

Phase 6 : Fusion SSDP
  → Enrichit les devices ARP existants avec les données UPnP
  → Ajoute les devices UPnP-only (non détectés par ARP)

Phase 7 : DNS-SD (DnsSdScanner)
  → Interroge mdns_query_ptr() (ESP-IDF) pour chaque type de service connu
  → Chaque résultat fournit hostname, TXT (modèle), adresse(s) IPv4
  → Fusionne services, model, hostname, category dans chaque NetworkDevice

Phase 8 : Self entry
  → Ajoute l'ESP32 lui-même (l'ARP ne peut pas découvrir sa propre adresse)
```

**Enrichissement DHCP passif** : l'étape finale `_enrichDevices()` consulte
aussi, par MAC, la table tenue en continu par `DhcpSniffer` (cf.
`src/modules/dhcp_sniffer.*` ci-dessous) — simple lecture mémoire, aucune
requête supplémentaire. Complète hostname/os si encore vides, sans jamais
écraser une source plus fiable (SSDP, API, mDNS/PTR…).

**Thread-safety** : `_results` (le vecteur de résultats) est protégé par un mutex
FreeRTOS. `getResults()` retourne une copie — jamais une référence.

---

### `src/modules/hostname_resolver.*` — Résolution de noms

**Rôle** : trouver le nom d'hôte de chaque équipement découvert par ARP.

Depuis v0.8.2, un seul mécanisme :

**PTR DNS batch** (seul mécanisme actif) :
- Pour chaque IP sans hostname mDNS, envoie une requête DNS inverse
- Format : `d.c.b.a.in-addr.arpa` au serveur DNS de la box
- Toutes les requêtes envoyées en parallèle, réponses attendues 500 ms max

Raison du batch : 50 requêtes DNS séquentielles × 500 ms = 25 secondes.
Avec le batch : 50 requêtes simultanées × 500 ms = 0,5 seconde.

**mDNS passif (retiré en v0.8.2)** : `224.0.0.251:5353` reste détenu
exclusivement par le composant mDNS d'ESP-IDF (responder `MDNS.begin()`,
voir `wifi_manager.cpp`) dès que le Wi-Fi est connecté — en pratique en
permanence. Aucune API ESP-IDF publique ne permet d'observer passivement
les annonces reçues par ce service partagé. `begin()`/`update()`/`end()`
sont conservés comme no-op pour compatibilité. Voir `docs/WARNINGS.md`.

---

### `src/modules/dhcp_sniffer.*` — Fingerprinting passif DHCP

**Rôle** : capter, sans jamais émettre de requête, les paquets DHCP
broadcast des autres équipements pour en extraire hostname et OS déclarés.

Module continu et indépendant de `NetworkScanner` :
- `begin()` (appelé une fois le WiFi connecté, comme `timeSync`/`otaMgr`) :
  ouvre un socket UDP non bloquant lié sur `0.0.0.0:67`. Si le bind échoue
  (port déjà occupé), le module se désactive silencieusement — log
  d'avertissement uniquement, aucun impact sur le reste du firmware.
- `loop()` (appelé depuis la boucle principale) : draine jusqu'à 4 paquets
  par appel, parse l'en-tête BOOTP fixe (`chaddr` = MAC client) puis les
  options 12 (Host Name), 53 (Message Type — ne retient que
  DISCOVER/REQUEST) et 60 (Vendor Class Identifier).
- Stocke le résultat dans une table interne MAC → `DhcpFingerprint`
  (mutex FreeRTOS dédié), bornée à 64 entrées (éviction de la plus
  ancienne au-delà).
- `lookup(mac)` : consultée uniquement par `NetworkScanner::_enrichDevices()`
  — lecture mémoire, aucune dépendance temporelle avec le cycle de scan.

`osGuess` provient d'une table de signatures Vendor Class limitée et
documentée (`MSFT*` → Windows, `android-dhcp*` → Android, `dhcpcd*`/
`udhcp*` → Linux) — volontairement sans heuristique sur l'option 55
(liste de paramètres demandés), jugée trop instable sans base de
signatures externe. Voir `docs/PROTOCOLS.md`.

---

### `src/modules/isp_detector.h` — Détection FAI (header-only)

**Rôle** : identifier les box Internet françaises par leurs signatures.

Analyse le hostname et l'OUI (fabricant MAC) pour détecter :
- Free : Freebox Ultra, Pop, Révolution, Delta, Mini 4K
- Orange : Livebox 4, 5, 6
- SFR : Box Plus, Box 8
- Bouygues : Bbox Miami, Ultym

100 % local — aucune requête réseau. Appliqué après la résolution hostname.

---

### `src/modules/ssdp_scanner.*` — Scanner UPnP/SSDP

**Rôle** : découvrir les équipements annoncés via le protocole UPnP.

Voir `docs/PROTOCOLS.md` pour le détail du protocole SSDP.

Fonctionnement :
1. Envoie M-SEARCH multicast → `239.255.255.250:1900`
2. Collecte les réponses (champ `LOCATION` = URL du descripteur XML)
3. HTTP GET du descripteur XML pour chaque device
4. Parsing XML robuste (namespaces, attributs, malformations)
5. Catégorisation automatique (Sonos, Hue, Freebox, Synology, Samsung TV…)
6. APIs spécifiques pour les devices reconnus (Hue, Synology, Freebox)

---

### `src/modules/dns_sd_scanner.*` — Scanner DNS-SD

**Rôle** : identifier les services exposés par chaque équipement du réseau.

Voir `docs/PROTOCOLS.md` pour le détail du protocole DNS-SD.

Fonctionnement (depuis v0.8.2) :
1. Pour chaque type de service connu, appelle `mdns_query_ptr()` (API C du
   composant mDNS d'ESP-IDF, `<mdns.h>`) — passe par le service mDNS déjà
   initialisé par `MDNS.begin()`, aucun socket applicatif dédié
2. Chaque résultat fournit directement hostname, TXT records et adresse(s)
   IPv4 — pas de parsing DNS manuel
3. Construit une map IP → {services, model, hostname, category}
4. Fusionne dans les NetworkDevice existants

Avant v0.8.2, ce scanner ouvrait son propre socket multicast (mutualisé via
`MdnsManager` depuis v0.8.1), qui entrait en conflit avec le socket
exclusif du responder mDNS d'ESP-IDF — voir `docs/WARNINGS.md`. `MdnsManager`
est supprimé depuis v0.8.2, devenu inutile.

---

### `src/modules/snmp_scanner.*`, `media_api_scanner.*` — Passe précise

**Rôle** : enrichir un seul équipement (réinterrogation ciblée) avec des
protocoles plus coûteux, non utilisés lors du scan complet, et qui peuvent
être adressés directement à l'IP visée (requête unicast).

- `SnmpScanner` : GetRequest SNMP v1 (ASN.1 BER manuel) sur `sysDescr` (UDP 161)
- `MediaApiScanner` : sondes HTTP séquentielles Cast (`:8008`), Sonos (`:1400`),
  Roku (`:8060`), Samsung Smart TV (`:8001`)
- `MqttScanner` : connexion TCP unicast à un broker MQTT (`:1883`), CONNECT
  anonyme + souscription `$SYS/broker/version` et `$SYS/broker/clients/connected`
  — déclenché uniquement si le profil déduit (`SmartHome`/`Unknown`) a le
  port MQTT ouvert

Ces deux scanners sont appelés uniquement depuis `_runRescan(ip, deep)` dans
`network_scanner.cpp`, exécuté dans une tâche FreeRTOS dédiée
(`_rescanTask`) avec progression exposée via `RescanStatus`
(`GET /api/devices/rescan/status`, polling 500 ms côté UI). Voir
`docs/PROTOCOLS.md` pour le détail de chaque protocole.

**Architecture depuis v0.9.1** : `_runRescan()` interroge uniquement l'IP
visée, jamais le reste du réseau. Le scan rapide se limite à ARP/ICMP + PTR
DNS. Le scan approfondi sonde d'abord les ports de la cible
(`kRescanTargetPorts`, `port_scanner.cpp`) ; s'il ne trouve aucun port/
service exploitable, la passe s'arrête immédiatement. Sinon, le profil
(`_profileFor()`) est réévalué à partir des ports découverts, et seuls les
modules pertinents pour ce profil sont lancés. `WsDiscoveryScanner`
(`ws_discovery_scanner.*`) n'est plus invoqué : c'est un protocole de
diffusion multicast, structurellement incompatible avec une interrogation
ciblée sur une seule IP — le module reste dans le code pour une éventuelle
réintégration au scan complet, mais n'est appelé par aucune route
actuellement (cf. `docs/PROTOCOLS.md`).

---

### `src/modules/web_server.*` — Serveur HTTP

**Rôle** : servir l'interface web et l'API REST.

Les pages HTML sont embarquées en PROGMEM (flash) et servies directement
sans copie en RAM (`server.send_P()`).

Le serveur est découplé du scanner via `ScanProvider` (struct de lambdas) :

```cpp
webSrv.registerScanProvider({
    .isScanning  = [] { return netScanner.isScanRunning(); },
    .getJson     = [] { return netScanner.resultsToJson(); },
    .triggerScan = [] { netScanner.startScan(); },
});
```

Ce découplage permet de tester ou de remplacer le scanner sans modifier le serveur web.

---

### `src/modules/system_health.*` — Mode dégradé mémoire

**Rôle** : surveiller le heap libre et basculer en mode dégradé plutôt que
de redémarrer automatiquement.

Appelé depuis `loop()` (`systemHealth.loop()`, non bloquant) :
- Sous `HEAP_CRITICAL_BYTES` (20 000 octets libres) → `isDegraded() == true`
- Hystérésis : la sortie du mode dégradé exige `HEAP_CRITICAL_BYTES +
  HEAP_RECOVERY_MARGIN` octets libres, pour éviter une oscillation rapide
  autour du seuil
- `restartNow()` : redémarrage **manuel uniquement**, déclenché par
  `POST /api/system/restart` (bouton « Redémarrer l'appareil » de la page
  Système) — le firmware ne redémarre jamais de lui-même sur condition
  mémoire

En mode dégradé, les points d'entrée suivants vérifient `systemHealth.isDegraded()`
et refusent l'opération (retour d'erreur explicite côté API) :
- `NetworkScanner::startScan()`, `rescanDevice()`
- `NetworkScanner::addNote()`, `setAlias()`, `setFavorite()`, `resetDevices()`, `restoreFromJson()`
- `DeviceHistory::addEvent()` (journalisation suspendue)

L'inventaire déjà acquis reste consultable (`GET /api/devices`, `/api/history`,
exports CSV/JSON) — seules les écritures et les opérations coûteuses en
mémoire/CPU sont bloquées.

---

### `src/utils/logger.h` — Journalisation (header-only)

**Rôle** : afficher des logs sur le port série avec niveaux de sévérité.

```cpp
Log::i("Module", "Message info");
Log::w("Module", "Avertissement");
Log::e("Module", "Erreur");
Log::d("Module", "Debug (désactivé par défaut)");
```

Désactivable via `-D LOG_LEVEL=0` dans `platformio.ini`.

---

## Pipeline HTML → Firmware

Les pages web ne sont pas stockées sur une carte SD ou en SPIFFS.
Elles sont **compilées directement dans le firmware** :

```
web_src/styles.css    ──┐
web_src/index.html    ──┤
web_src/scan.html     ──┤
web_src/history.html  ──┼── python tools/minify_web.py ──► include/*.h
web_src/wifi.html     ──┤
web_src/topology.html ──┤
data/oui.json         ──┘
                              │
                              ▼
                    pio run (compilation C++)
                              │
                              ▼
                    firmware.bin (inclut HTML + CSS + OUI)
```

Avantages :
- Pas d'étape "Upload Filesystem Image" séparée
- HTML mis à jour avec le firmware en une seule opération OTA
- Aucune dépendance au système de fichiers

---

## Données — `struct NetworkDevice`

Tous les modules partagent cette structure :

```cpp
struct NetworkDevice {
    String   ip;           // Source : ARP
    String   mac;          // Source : ARP
    String   manufacturer; // Source : OUI table → enrichi par SSDP/API
    String   hostname;     // Source : mDNS → PTR DNS → SSDP friendlyName
    String   category;     // Source : OUI → ISP → SSDP deviceType
    String   model;        // Source : ISP → SSDP modelName → API
    String   os;           // Source : API Hue/Synology/Freebox
    String   source;       // Méthode ayant fourni le hostname/enrichissement
    String   services;     // Services DNS-SD : "HTTP|SSH|SMB" (pipe-séparé)
    uint32_t lastSeen;     // millis() du dernier scan
    bool     online;       // true si vu au dernier scan
};
```

**Priorité des sources** (la plus précise écrase la moins précise) :
```
HueAPI / SynologyAPI / FreeboxAPI  (plus précis)
  ↓
SSDP (XML UPnP)
  ↓
ISP detection (Free/Orange/SFR/Bouygues)
  ↓
mDNS passif
  ↓
PTR DNS
  ↓
OUI table (MAC prefix)             (moins précis)
```

**MAC aléatoire (privacy mode)** : avant toute recherche dans la table OUI,
`isRandomizedMac()` (`src/modules/network_scanner.cpp`) vérifie le bit
"locally administered" de l'adresse (2ème caractère hexadécimal du 1er
octet = `2`, `6`, `A` ou `E`). Si l'adresse est aléatoire (cas fréquent des
smartphones iOS/Android récents), la table OUI n'est pas consultée et
l'équipement est classé `manufacturer="Unknown (Privacy Mode)"`,
`category="Mobile/Aléatoire"` — ces champs restent ensuite enrichissables
par les sources plus précises (mDNS, SSDP, etc.) ci-dessus.

---

## Concurrence FreeRTOS

Le projet utilise deux cores de l'ESP32-S3 :

| Core | Tâches |
|---|---|
| Core 0 | TCP/IP stack (lwIP), scan réseau, mDNS, SSDP |
| Core 1 | `setup()` + `loop()` Arduino (WiFi, OTA, WebServer) |

Le scan réseau est sur le Core 0 pour co-localiser les appels lwIP (ARP, DNS, sockets).

**Mutex** : un seul mutex protège `_results`. Durée de verrouillage : < 1 ms
(juste le temps de copier/modifier le vecteur).

---

## Où modifier quoi

| Objectif | Fichier(s) à modifier |
|---|---|
| Modifier l'interface web | `web_src/*.html` puis `python tools/minify_web.py` |
| Modifier les styles | `web_src/styles.css` puis `python tools/minify_web.py` |
| Ajouter un fabricant OUI | `data/oui.json` puis `python tools/minify_web.py` |
| Modifier les timeouts | `include/app_config.h` |
| Ajouter une box FAI | `src/modules/isp_detector.h` |
| Ajouter un device UPnP | `src/modules/ssdp_scanner.cpp` (`_categorize()` ou `_enrich*()`) |
| Ajouter un type de service DNS-SD | `src/modules/dns_sd_scanner.cpp` (table `SERVICE_TYPES[]`) |
| Ajouter une sonde à la passe précise | `src/modules/network_scanner.cpp` (`_runRescan()`) + nouveau scanner dans `src/modules/` |
| Modifier la version | `platformio.ini` (`PROJECT_VERSION`) — uniquement ici |
| Ajouter un module entier | `src/modules/nouveau.*` + include dans `network_scanner.cpp` ou `main.cpp` |
