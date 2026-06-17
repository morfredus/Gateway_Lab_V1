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
                    │  /update       │
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

Phase 2 : mDNS passif
  → HostnameResolver écoute les annonces multicast pendant le sweep ARP
  → Capture les enregistrements A (.local) des devices compatibles

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
  → Envoie 22 requêtes PTR dans un seul paquet mDNS multicast
  → Écoute 4 s : PTR → instance, SRV → port/hostname, TXT → modèle, A → IP
  → Fusionne services, model, hostname, category dans chaque NetworkDevice

Phase 8 : Self entry
  → Ajoute l'ESP32 lui-même (l'ARP ne peut pas découvrir sa propre adresse)
```

**Thread-safety** : `_results` (le vecteur de résultats) est protégé par un mutex
FreeRTOS. `getResults()` retourne une copie — jamais une référence.

---

### `src/modules/hostname_resolver.*` — Résolution de noms

**Rôle** : trouver le nom d'hôte de chaque équipement découvert par ARP.

Deux mécanismes complémentaires :

**mDNS passif** (priorité haute) :
- Ouvre un socket UDP sur `224.0.0.251:5353` (multicast mDNS)
- Écoute les annonces spontanées pendant le sweep ARP
- Pas de requête active — les devices annoncent leur présence

**PTR DNS batch** (fallback) :
- Pour chaque IP sans hostname mDNS, envoie une requête DNS inverse
- Format : `d.c.b.a.in-addr.arpa` au serveur DNS de la box
- Toutes les requêtes envoyées en parallèle, réponses attendues 500 ms max

Raison du batch : 50 requêtes DNS séquentielles × 500 ms = 25 secondes.
Avec le batch : 50 requêtes simultanées × 500 ms = 0,5 seconde.

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

Fonctionnement :
1. Construit un paquet mDNS avec 22 questions PTR (un par type de service)
2. Envoie en multicast → `224.0.0.251:5353` (même canal que mDNS)
3. Écoute pendant 4 s les réponses PTR + SRV + TXT + A
4. Construit une map IP → {services, model, hostname, category}
5. Fusionne dans les NetworkDevice existants

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
web_src/styles.css   ──┐
web_src/index.html   ──┤
web_src/scan.html    ──┼── python tools/minify_web.py ──► include/*.h
web_src/ota.html     ──┤
data/oui.json        ──┘
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
| Modifier la version | `platformio.ini` (`PROJECT_VERSION`) — uniquement ici |
| Ajouter un module entier | `src/modules/nouveau.*` + include dans `network_scanner.cpp` ou `main.cpp` |
