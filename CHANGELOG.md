# Changelog — Gateway Lab V1

Toutes les modifications notables sont documentées ici.
Format : [Semantic Versioning](https://semver.org/)

---

## [0.6.0] - 2026-06-18

### Ajoute

- **Champ `type` (sous-catégorie)** (`src/modules/network_scanner.cpp`,
  `device_store.cpp`, `oui_table.h`) : les équipements peuvent désormais
  porter un sous-type au sein de leur catégorie (ex. "Smart Speaker", "Smart
  Display") en plus de la catégorie générale, persisté avec le reste de la
  fiche et affiché dans les pages Équipements et Historique.
- **Score de confiance unique et plus prudent** (`NetworkScanner::_confidenceFor()`)
  : remplace l'ancienne logique par un score global qui retient le signal le
  *plus faible* entre marque et catégorie plutôt que le meilleur. Les OUI
  ambigus (Apple, Samsung, Xiaomi, Huawei, Intel, HP, Dell, Lenovo, Microsoft
  — un même préfixe MAC pouvant désigner un PC, un mobile, une tablette ou un
  écran connecté) sont désormais plafonnés à 35 % de confiance. Une infobulle
  détaille la confiance par champ (marque / catégorie / modèle / type).
- **Passe précise asynchrone avec suivi de progression** (`POST /api/devices/rescan`,
  `GET /api/devices/rescan/status`) : la réinterrogation ciblée d'un
  équipement (bouton ⟲) tourne désormais dans sa propre tâche FreeRTOS au
  lieu de bloquer la requête HTTP. L'avancement (étape + pourcentage) est
  interrogeable via polling et s'affiche directement **sous la ligne de
  l'équipement concerné** dans le tableau, pour rester visible même si la
  ligne est en bas de liste.
- **Sondage SNMP `sysDescr`** (`src/modules/snmp_scanner.cpp`, nouveau) :
  implémentation maison (encodage/décodage ASN.1 BER) d'une requête SNMPv1
  `GetRequest` en lecture publique — beaucoup de routeurs, switches,
  imprimantes et NAS y exposent fabricant et modèle en texte clair.
  Utilisé uniquement lors de la passe précise.
- **Découverte WS-Discovery / ONVIF** (`src/modules/ws_discovery_scanner.cpp`,
  nouveau) : sonde les caméras IP et imprimantes compatibles ONVIF
  (protocole indépendant de SSDP), utilisée lors de la passe précise pour
  catégoriser automatiquement (Caméra, Imprimante, NAS).
- **API HTTP propriétaires des appareils multimédia courants**
  (`src/modules/media_api_scanner.cpp`, nouveau) : sondage direct, lors de
  la passe précise, des ports fixes non couverts par le scan de ports
  standard — Google Cast/Chromecast (`:8008/setup/eureka_info`), Sonos
  (`:1400/xml/device_description.xml`), Roku (`:8060/query/device-info`),
  Samsung Smart TV (`:8001/api/v2/`) — pour récupérer marque/modèle/nom
  exacts sans configuration préalable.
- **Service DNS-SD Matter** (`_matterc._udp`, en plus de `_matter._tcp` déjà
  présent) : détection des appareils Matter commissionables (pas encore
  appairés), en préparation de la généralisation de ce standard IoT.
- **Catégorisation Amazon Fire TV / Fire Stick** (`SsdpScanner::_categorize()`)
  : les équipements répondant au protocole DIAL (SSDP, `manufacturer=Amazon`
  ou `deviceType` contenant `dial`) sont désormais classés en catégorie
  *Streaming* (`os="Fire OS"`) au lieu de retomber dans le générique *IoT*.

### Modifié

- Contraste de l'interface Équipements/Historique éclairci sur le thème
  sombre : textes secondaires (`#94a3b8` → `#c3d0e0`, `#64748b` → `#9aacc2`)
  et quelques badges peu lisibles (`.port-other`, `.status-offline`) rendus
  plus visibles sur le fond bleu nuit.

## [0.5.0] - 2026-06-17

### Ajoute

- **Réinitialisation de l'inventaire** (`POST /api/devices/reset`,
  `src/modules/network_scanner.cpp`) : nouveau menu « Réinitialiser » sur la
  page Équipements, avec quatre options — tout effacer, conserver les
  équipements ayant un alias, conserver ceux dont le fabricant est connu, ou
  les deux. Permet de repartir sur une base propre sans perdre les
  équipements identifiés manuellement ou automatiquement.
- **Réinterrogation ciblée d'un équipement** (`POST /api/devices/rescan`,
  `NetworkScanner::rescanDevice()`) : bouton ⟲ sur chaque ligne de la page
  Équipements pour rafraîchir un seul équipement (sonde ARP/ICMP ciblée puis
  résolution de nom, scan de ports et NetBIOS) sans relancer un scan complet
  du sous-réseau.
- **Filtrage de l'historique** (`web_src/history.html`, `web_src/history.js`) :
  cases à cocher pour n'afficher que certains types d'événements (nouveaux
  équipements, reconnexions, déconnexions, changements de champs).
- **Effacement de l'historique** (`DELETE /api/history`) : bouton « Vider
  l'historique » sur la page Historique. Télécharge automatiquement une
  sauvegarde JSON du journal avant de le vider côté serveur.

## [0.4.0] - 2026-06-17

### Ajoute

- **Détection des adresses MAC privées/aléatoires** (`src/modules/network_scanner.cpp`) :
  les smartphones récents (iOS 14+, Android 10+) se connectent au Wi-Fi avec
  une adresse MAC générée aléatoirement pour préserver la confidentialité de
  l'utilisateur, plutôt que leur adresse MAC matérielle réelle. Le scanner
  détecte désormais ce cas via le bit "locally administered" de l'adresse
  (2ème caractère hexadécimal du 1er octet valant `2`, `6`, `A` ou `E`) et
  classe automatiquement l'équipement en catégorie `Mobile/Aléatoire` avec
  le fabricant `Unknown (Privacy Mode)`, sans consulter inutilement la table
  OUI (qui ne contiendrait de toute façon aucune correspondance pertinente).

## [0.3.0] - 2026-06-17

### Ajoute

- **Portail de configuration WiFi** : si aucun réseau n'est enregistré,
  l'ESP32 démarre désormais un point d'accès `GatewayLab-Setup` avec une
  page de configuration captive (`src/modules/wifi_manager.cpp`,
  `web_src/wifi.html`). L'utilisateur choisit son réseau (liste détectée ou
  saisie manuelle) et son mot de passe depuis un navigateur, sans
  installation ni recompilation.
- **Persistance NVS multi-réseaux** : les identifiants WiFi sont désormais
  enregistrés dans la mémoire `Preferences` de l'ESP32 (namespace `"wifi"`)
  et survivent aux redémarrages et coupures de courant. Plusieurs réseaux
  peuvent être enregistrés simultanément (ex : domicile + atelier) ; l'ESP32
  se connecte automatiquement au premier disponible (signal le plus fort).
- **Nouvelle page `Paramètres → Réseau WiFi`** (`/wifi`) : affiche l'état de
  connexion (SSID, IP, RSSI), liste les réseaux enregistrés, permet d'en
  ajouter ou d'en supprimer — sans jamais exposer les mots de passe au
  navigateur.
- **Nouvelles routes API** : `GET /api/wifi` (état + liste des SSID
  enregistrés), `POST /api/wifi` (ajoute/met à jour un réseau), `DELETE
  /api/wifi` (supprime un réseau).
- **Nouveau guide** `docs/WIFI_SETUP.md` : explique pas à pas, pour un
  débutant, comment connecter la carte à son WiFi via le portail, gérer
  plusieurs réseaux et réinitialiser la configuration.

### Modifie

- `include/secrets_example.h` / `include/secrets.h` ne sont plus la méthode
  officielle de configuration WiFi : ils ne définissent plus qu'un réseau de
  **développement** (`DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASSWORD`), utilisé
  uniquement si aucun réseau n'est encore enregistré en NVS. `secrets.h` est
  désormais optionnel (inclusion conditionnelle via `__has_include`).
- `WiFiManager` (`src/modules/wifi_manager.*`) réécrit pour exposer la
  hiérarchie de configuration : NVS (priorité 1) → `secrets.h` (priorité 2,
  dev) → portail de configuration (priorité 3).
- Menu de navigation : ajout du lien **Paramètres** sur les 5 pages
  (Accueil, Équipements, Historique, OTA, Paramètres).
- `tools/minify_web.py`, `tools/extract_web_sources.py` et
  `tools/validate_html.py` mis à jour pour inclure `wifi.html` / `wifi.js`.

### Notes

- Aucune authentification n'est requise pour le portail de configuration ni
  pour l'API `/api/wifi` — voir `docs/WARNINGS.md` (section mots de passe
  WiFi en mémoire NVS) pour les implications de sécurité sur un réseau non
  fiable.
- Compatible avec une distribution future du firmware sous forme de fichier
  `.bin` unique : aucune modification de code source n'est requise pour
  configurer le WiFi.

---

## [0.2.2] - 2026-06-17

### Modifie

- **Séparation HTML / CSS / JS dans `web_src/`** : tout le JavaScript inline
  des pages (`index.html`, `scan.html`, `ota.html`, `history.html`) a été
  extrait dans des fichiers dédiés (`index.js`, `scan.js`, `ota.js`,
  `history.js`). Les fichiers HTML ne contiennent désormais plus que du
  markup, dans la même logique que `styles.css` pour le CSS.
- `tools/minify_web.py` minifie et injecte désormais aussi le `.js` de
  chaque page inline (à la place de `<script src="page.js"></script>`),
  en plus du CSS commun.
- `tools/extract_web_sources.py` réécrit en conséquence : il extrait HTML
  **et** JS séparément depuis les headers PROGMEM, et écrit toujours dans
  `web_src/extracted/` — les sources originales de `web_src/` ne sont plus
  jamais écrasées.

### Corrige

- Page OTA (`/update`) : le lien **Historique** manquait dans le menu de
  navigation. Les 4 pages affichent maintenant le même menu complet
  (Accueil / Équipements / Historique / OTA).

### Notes

- Aucun changement de comportement côté utilisateur (hors lien de menu
  corrigé) — uniquement une réorganisation des sources et de la chaîne de
  build des assets web.

---

## [0.2.1] - 2026-06-17

### Ajoute

- **Niveau de confiance** (`network_scanner.cpp` - `_confidenceFor()`) :
  chaque equipement affiche desormais un score 0-100% expliquant la fiabilite
  de son identification (manufacturer/category), avec un libelle de source
  au survol. Ponderation par source :
  - Box FAI (DHCP local) : 100%
  - API specifique (Hue/DSM/Freebox) : 95%
  - mDNS : 90%
  - SSDP/UPnP : 80%
  - PTR DNS : 70%
  - NetBIOS : 65%
  - OUI (adresse MAC) : 60%
  - Heuristiques (pattern matching hostname/ports/services) : 40%
  - Aucun signal fiable : 20%
  - Nouveaux champs JSON `confidence` et `confidenceLabel` sur `GET /api/devices`
  - Badge couleur (vert/jaune/rouge) dans la colonne Categorie de la page Equipements

### Notes

- Aucune fonctionnalite existante n'a ete retiree - le score est calcule a la
  volee a partir des signaux deja collectes (source, manufacturer, category),
  sans modifier le pipeline de scan ni la persistance.

---

## [0.2.0] - 2026-06-16

### Ajoute

- **Historique des equipements** (`src/modules/device_history.h/.cpp`, `time_sync.h/.cpp`) :
  - Synchronisation NTP au demarrage (epoch reel pour les horodatages)
  - Champs `firstSeen`, `lastSeenAt`, `seenCount` sur chaque equipement (`NetworkDevice`)
  - Journal chronologique des evenements (`/history.json`, 300 entrees max, FIFO) :
    nouvel equipement, reconnexion, deconnexion, changement de champ
  - Nouvelle page **Historique** (`/history`) affichant la vue chronologique
  - Nouvel endpoint `GET /api/history`

- **Alias utilisateur** : chaque equipement peut etre renomme manuellement
  (bouton ✎ dans la colonne "Nom d'hote" de la page Equipements). Persiste
  dans `/devices.json` et prend le pas sur le hostname a l'affichage.
  Nouvel endpoint `POST /api/alias`.

- **Classification intelligente** (`network_scanner.cpp` - `_classifyDevices()`) :
  combine manufacturer/services DNS-SD/ports ouverts/hostname pour affiner la
  categorie d'un equipement quand elle est encore vide ou generique ("IoT") -
  ex: SMB+SSH/HTTP -> NAS, RTSP -> Camera, port HA -> Smart Hub, AirPlay -> Speaker.

- **Detection des changements** : chaque scan compare l'etat courant a l'etat
  precedent et journalise automatiquement les changements de IP, fabricant,
  categorie, nom d'hote ou ports ouverts pour un meme equipement (MAC).

- **Sauvegarde / restauration** :
  - `GET /api/backup` - telechargement d'un export JSON complet (equipements,
    alias, historique de detection)
  - `POST /api/restore` - restauration depuis un fichier exporte precedemment
  - Nouvelle section "Sauvegarde des equipements" sur la page d'accueil

### Notes

- Toutes les fonctionnalites s'ajoutent a l'existant sans rien retirer - le
  scan continue de suivre ARP -> ICMP -> hostnames -> SSDP -> DNS-SD -> ports
  -> NetBIOS -> enrichissement -> classification -> historique -> sauvegarde.

---

## [0.1.2] - 2026-06-16

### Ajoute

- **`src/modules/netbios_scanner.h/.cpp`** - Decouverte de hostnames via NetBIOS Name Service (UDP 137)

  Requete Node Status (RFC 1001/1002) sur les equipements encore sans hostname apres
  mDNS/PTR DNS - tres efficace pour les PC Windows et serveurs Samba. Retourne le nom
  de machine et le groupe de travail/domaine sans configuration prealable.

- **`src/modules/device_enricher.h`** - Enrichissement par reconnaissance de patterns hostname

  Complete `manufacturer`/`category`/`os` a partir de mots-cles trouves dans le hostname
  resolu (~75 patterns : robots aspirateurs, domotique, routeurs, IoT, NAS, SBC, streaming,
  enceintes, imprimantes, cameras, mobile, ordinateurs, consoles). Detection 100% locale,
  appliquee uniquement sur les champs encore vides en derniere etape du scan.

- **Scanner de ports etendu** (`port_scanner.h/.cpp`) :
  - Banniere brute SSH/FTP (lue immediatement a la connexion, sans requete)
  - Sondage des API HTTP propres aux equipements IoT connus : Shelly (`/shelly`),
    Tasmota (`/cm?cmnd=Status%200`), FritzBox (`/jason_boxinfo.xml`) - identifie le
    modele exact et la version de firmware sans configuration prealable
  - Nouveaux champs `PortScanResult` : `sshBanner`, `ftpBanner`, `iotType`, `iotModel`, `iotFirmware`

- **Scanner DNS-SD etendu** (`dns_sd_scanner.cpp`) : 9 nouveaux types de services
  (`_pdl-datastream._tcp`, `_scanner._tcp`, `_workstation._tcp`, `_companion-link._tcp`,
  `_privet._tcp`, `_matter._tcp`, `_sleep-proxy._tcp`), soit 31 types de services au total.

- **Interface** : nouveau badge de source "NetBIOS" sur la page Equipements.

### Notes

- Toutes les nouvelles fonctionnalites s'ajoutent aux sources existantes (ARP, ICMP, mDNS,
  PTR, SSDP, DNS-SD, scan de ports) sans en retirer aucune - la regle de non-ecrasement des
  champs deja renseignes par une source plus fiable est preservee.

---

## [0.1.1] — 2026-06-16

### Ajouté

- **`src/modules/port_scanner.h/.cpp`** — Scanner TCP des ports communs

  **Principe :** Sockets non-bloquants + `select()` pour sonder plusieurs ports simultanément
  (lots de `MAX_BATCH = 8` pour rester dans les limites lwIP `CONFIG_LWIP_MAX_SOCKETS = 16`).

  **14 ports sondés :**

  | Port | Service |
  |---|---|
  | 21 | FTP |
  | 22 | SSH |
  | 23 | Telnet |
  | 80 | HTTP |
  | 443 | HTTPS |
  | 445 | SMB |
  | 554 | RTSP (caméras IP) |
  | 1883 | MQTT |
  | 3389 | RDP |
  | 5000 | HTTP (Synology DSM / UPnP) |
  | 8080 | HTTP alternatif |
  | 8123 | Home Assistant |
  | 8443 | HTTPS alternatif |
  | 9100 | IPP (impression) |

  **Banner grabbing HTTP :** `GET /` sur les ports HTTP ouverts (80, 8080, 8123, 5000) →
  lecture de l'en-tête `Server:` pour enrichir `manufacturer`, `category`, `model`.

  Heuristiques banner → fabricant : Synology, QNAP, Freebox, Philips, Ubiquiti, OpenWrt,
  DD-WRT, Hikvision, Dahua, Axis, Cisco, MikroTik, GoAhead (caméra IP), Home Assistant.

  **Temps de scan typique :** ≤ 500 ms/équipement (RST immédiat sur ports fermés).
  Lot de 8 sockets × 2 iterations × ~5 ms = ≈ 80 ms si tous les ports sont fermés.
  Timeout de 250 ms par lot si ports filtrés.

- **`IcmpScanner::pingWithTtl()`** — Nouveau variant ICMP retournant la valeur TTL

  L'en-tête IP (octet 8 = TTL, RFC 791) est extrait de chaque réponse echo reply.
  `struct PingResult { String ip; uint8_t ttl; }` retourné pour chaque hôte répondant.

- **Détection OS depuis TTL** (`NetworkScanner::_osFromTtl()`)

  | TTL reçu | OS déduit |
  |---|---|
  | > 200 | Cisco / équipement réseau |
  | > 100 | Windows (TTL initial 128) |
  | > 50  | Linux / Android / iOS (TTL initial 64) |

  Injecté dans `d.os` uniquement si vide (ne pas écraser les données SSDP/API).
  Heuristiques complémentaires basées sur les ports : RDP ouvert → Windows certain,
  RTSP ouvert → Camera/NVR.

- **`NetworkDevice.openPorts`** — Nouveau champ `String` pipe-séparé

  Stocke les noms courts des ports TCP ouverts (ex: `"HTTP|SSH|SMB"`).
  Sérialisé en tableau JSON `["HTTP","SSH","SMB"]` dans `/api/devices`.
  Persisté dans `LittleFS` (`/devices.json`).

- **`NetworkScanner::_scanPorts()`** — Nouvelle phase de scan (phase 10)

  Appelée après DNS-SD, avant la sauvegarde LittleFS.
  Collecte les IPs online (excl. ESP32 lui-même), appelle `portScanner.scan()`,
  fusionne les résultats dans `_results` sous mutex.

- **UI — Badges ports TCP** (`web_src/scan.html`)

  Badges colorés affichés dans la cellule Fabricant, sous les badges de services DNS-SD.
  Tooltip `title="Port TCP ouvert : NOM"` sur chaque badge.
  Fonction `portClass(p)` → 10 classes CSS selon le protocole.

- **UI — `web_src/styles.css`**

  Bloc `.port-list` + `.port-badge` + 10 classes de couleur :

  | Classe | Couleur | Ports |
  |---|---|---|
  | `.port-http` | Bleu | HTTP, HTTPS, HTTP/8080… |
  | `.port-ssh` | Jaune | SSH |
  | `.port-smb` | Cyan | SMB |
  | `.port-rdp` | Violet | RDP |
  | `.port-rtsp` | Vert émeraude | RTSP |
  | `.port-mqtt` | Turquoise | MQTT |
  | `.port-print` | Vert pâle | IPP |
  | `.port-ha` | Orange | Home Assistant |
  | `.port-legacy` | Rouge pâle | FTP, Telnet |
  | `.port-other` | Gris | autres |

### Modifié

- **`src/modules/icmp_scanner.h`** : ajout `struct PingResult`, déclaration `pingWithTtl()`
- **`src/modules/icmp_scanner.cpp`** : `_pingOne()` retourne le TTL (uint8_t) ; `ping()` adapté ; `pingWithTtl()` implémenté
- **`src/modules/network_scanner.h`** : champ `openPorts` ajouté à `NetworkDevice` ; déclarations `_scanPorts()`, `_osFromTtl()`
- **`src/modules/network_scanner.cpp`** :
  - Import de `port_scanner.h`
  - ICMP sweep utilise `pingWithTtl()` → alimente `d.os` depuis TTL
  - `_scanPorts()` implémentée, appelée dans `_run()` après `_mergeDnsSd()`
  - `_osFromTtl()` implémentée
  - `resultsToJson()` : champ `openPorts` sérialisé en tableau JSON
- **`src/modules/device_store.cpp`** : lecture/écriture du champ `openPorts`
- **`platformio.ini`** : `PROJECT_VERSION` → `0.1.1`

### Technique

- **Sockets non-bloquants** : `fcntl(F_SETFL, O_NONBLOCK)` + `connect()` → `EINPROGRESS` →
  `select()` avec timeout — permet de sonder N ports en parallèle sans bloquer la tâche FreeRTOS
- **Contrainte lwIP** : `CONFIG_LWIP_MAX_SOCKETS = 16` → `MAX_BATCH = 8` pour laisser 8 sockets
  disponibles pour le Web Server et les autres modules réseau actifs
- **TTL extraction** : `buf[8]` dans la réponse ICMP raw (octet TTL de l'en-tête IP, RFC 791)
- **Heuristiques conservatrices** : OS depuis TTL n'écrase jamais les données issues de SSDP/API ;
  les seuils TTL absorbent 1-2 sauts de routage (réseau domestique typique = 1 saut)

---

## [0.1.0] — 2026-06-16

### Ajouté

- **ARP 3 passes** (`network_scanner.cpp`) :
  - Passe 1 : sweep complet du sous-réseau, lots de 5 IPs, 100 ms/lot, lecture ARP après chaque lot
  - Passe 2 : re-sonde uniquement les IPs non répondues, lots de 5, 150 ms/lot
  - Passe 3 : attente 500 ms + lecture finale de la table ARP pour capturer les retardataires

- **ICMP sweep** (`src/modules/icmp_scanner.h/.cpp`) :
  - Après les 3 passes ARP, ping ICMP raw sur les IPs restantes (≤ 100)
  - Timeout 150 ms par hôte, traitement séquentiel
  - Les IPs répondantes sont injectées dans `_results` avec `source="ICMP"`

- **Persistance LittleFS** (`src/modules/device_store.h/.cpp`) :
  - `DeviceStore::begin()` monte LittleFS (format automatique si besoin)
  - `load()` : désérialise `/devices.json` → vecteur de `NetworkDevice` (online=false)
  - `save()` : sérialise tous les équipements non-vides dans `/devices.json`
  - Au démarrage du scan, les équipements connus sont injectés comme offline
  - En fin de scan, les résultats enrichis sont sauvegardés

- **Statistiques online/offline** (`ScanStats`) :
  - Nouveau struct `ScanStats { known, online, offline }` dans `network_scanner.h`
  - `NetworkScanner::getStats()` : comptage thread-safe sous mutex
  - `/api/devices` retourne `"stats":{"known":N,"online":N,"offline":N}`
  - `ScanProvider::getStats` lambda dans `web_server.h`

- **UI — barre de statistiques** (`web_src/scan.html`) :
  - Bandeau `#stats-bar` au-dessus du tableau : connus · en ligne · hors ligne
  - Colonne "Statut" dans le tableau : ● vert (online) / ○ gris (offline)
  - `window._lastStats` extrait de la réponse `/api/devices` pour mise à jour du bandeau

- **UI — styles** (`web_src/styles.css`) :
  - `.stats-bar`, `.stat-sep`, `.stat-known`, `.stat-online`, `.stat-offline`
  - `.status-cell`, `.status-online`, `.status-offline`

### Modifié

- **`src/modules/network_scanner.h`** : ajout `ScanStats`, `getStats()`, `_mergePersistedDevices()`, `_saveToStore()`
- **`src/modules/network_scanner.cpp`** : imports `device_store.h` et `icmp_scanner.h`, sweep 3 passes, intégration ICMP, persistence
- **`src/modules/web_server.h`** : `ScanProvider::getStats` lambda ajoutée
- **`src/modules/web_server.cpp`** : `_handleApiDevices()` inclut `stats` dans la réponse JSON
- **`src/main.cpp`** : `deviceStore.begin()` avant WiFi, lambda `getStats` dans `registerScanProvider`
- **`platformio.ini`** : `PROJECT_VERSION` → `0.1.0`

## [0.0.9] — 2026-06-16

### Ajouté

- **`src/modules/dns_sd_scanner.h/.cpp`** — scanner DNS-SD complet (RFC 6763 / RFC 6762)

  **Principe :** DNS-SD utilise le même canal multicast que mDNS (`224.0.0.251:5353`)
  mais interroge des *types de services* (`_http._tcp.local`, `_ssh._tcp.local`…).
  Chaque device compatible répond avec ses instances de service + métadonnées TXT.

  **22 types de services interrogés en un seul paquet mDNS multi-question :**

  | Catégorie | Services |
  |---|---|
  | Web | `_http._tcp`, `_https._tcp` |
  | Accès | `_ssh._tcp`, `_sftp-ssh._tcp`, `_smb._tcp`, `_afpovertcp._tcp`, `_nfs._tcp`, `_ftp._tcp` |
  | Apple | `_airplay._tcp`, `_raop._tcp`, `_homekit._tcp`, `_daap._tcp`, `_device-info._tcp` |
  | Google | `_googlecast._tcp` |
  | Musique | `_sonos._tcp`, `_spotify-connect._tcp` |
  | IoT | `_hue._tcp`, `_esphome._tcp`, `_home-assistant._tcp`, `_mqtt._tcp` |
  | Impression | `_ipp._tcp`, `_printer._tcp` |

  **Records DNS parsés :**
  - `PTR` (12) : nom de l'instance → associé au type de service
  - `SRV` (33) : port + hostname canonique de l'instance
  - `TXT` (16) : métadonnées `md=`, `fn=`, `am=` → modèle device
  - `A`   (1)  : hostname.local → IP (résolution inverse)

  **Résultat :** map `IP → DnsSdInfo { services, model, hostname, category }`

  **Déduction de catégorie** depuis les services (priorité décroissante) :
  HomeKit→SmartHome, AirPlay→TV, Cast→Streaming, Sonos/Spotify→Speaker,
  Hue→SmartHub, ESPHome/HA/MQTT→IoT, IPP/Print→Printer, SMB/AFP/NFS→NAS, SSH→Computer

- **`NetworkDevice.services`** (nouveau champ) :
  Labels des services DNS-SD séparés par `|` (ex: `"HTTP|SSH|SMB"`)
  Sérialisé en tableau JSON `["HTTP","SSH","SMB"]` dans `/api/devices`

- **`NetworkScanner::_mergeDnsSd()`** :
  - Appelle `dnsSdScanner.scan(4000)` hors mutex (4 s d'écoute)
  - Fusionne services, model, hostname, category sous mutex
  - Accumule les services sans doublon (merge avec services déjà présents)
  - Appelée après `_mergeSsdp()`, avant `_addSelfEntry()`

- **UI — `web_src/scan.html`** :
  - Badges de service dans la cellule Fabricant, sous l'OS
  - 9 couleurs selon la famille : `web` (bleu), `ssh` (jaune), `share` (cyan),
    `apple` (rose), `google` (vert), `music` (violet), `iot` (turquoise),
    `print` (vert pâle), `other` (gris)
  - Tooltip `title="Service DNS-SD : NOM"` sur chaque badge

- **UI — `web_src/styles.css`** :
  - `.svc-list` : conteneur flex-wrap pour les badges de service
  - `.svc-badge` : style de base (très compact, 0.58 rem)
  - 9 classes de couleur : `.svc-web`, `.svc-ssh`, `.svc-share`, `.svc-apple`,
    `.svc-google`, `.svc-music`, `.svc-iot`, `.svc-print`, `.svc-other`

### Modifié

- **`src/modules/network_scanner.h`** : champ `services` ajouté à `NetworkDevice`
- **`src/modules/network_scanner.cpp`** :
  - Import `dns_sd_scanner.h`
  - `_mergeDnsSd()` implémentée et appelée dans `_run()`
  - `resultsToJson()` : services sérialisé en tableau JSON
  - Stack FreeRTOS augmentée **20 → 24 Ko** (DNS-SD multicast + 3 std::map)
- **`platformio.ini`** : `PROJECT_VERSION` → `0.0.9`

### Technique

- **Paquet mDNS multi-question** : toutes les requêtes DNS-SD dans un seul UDP
  (≈ 650 octets pour 22 services) — évite 22 allers-retours réseau séparés
- **QU bit** (class 0x8001) : signale la préférence pour des réponses unicast ;
  réduit la charge multicast sur le réseau domestique
- **Buffers heap** : les buffers de parsing (1 Ko) sont alloués sur le tas
  (`malloc/free`) plutôt que sur la stack FreeRTOS — économise ~1 Ko de stack
- **Déduplication instances** : clé composée `instanceName|serviceType` évite
  les doublons quand un device répond plusieurs fois à la même requête
- **Résolution IP à 3 niveaux** : `inst.ip` direct → `_hostToIp[hostname]` →
  `_hostToIp[instanceName.toLowerCase()-sanitized.local]`

---

## [0.0.8] — 2026-06-15

### Ajouté

- **`src/modules/ssdp_scanner.h/.cpp`** — scanner SSDP/UPnP complet et modulaire

  **Niveau 4 — Découverte UPnP :**
  - Envoi M-SEARCH multicast UDP → `239.255.255.250:1900`, collecte des réponses SSDP
  - Déduplication des réponses par URL `LOCATION` (un device peut annoncer plusieurs services)
  - HTTP GET non bloquant du descripteur XML pour chaque device découvert (timeout 2 s)
  - Parsing XML robuste : supporte les namespaces (`<ns:friendlyName>`), les attributs,
    et le XML partiellement malformé — retourne `""` plutôt que de planter
  - Extraction : `friendlyName` → `hostname`, `manufacturer`, `modelName` → `model`,
    `deviceType`, `presentationURL`
  - Catégorisation automatique des équipements courants :

    | Équipement détecté | Catégorie assignée |
    |---|---|
    | Sonos | Speaker |
    | Philips Hue Bridge / Signify | SmartHub |
    | Freebox (Free SAS) | Router |
    | Synology DiskStation / RackStation | NAS |
    | Samsung TV | TV |
    | Google Chromecast | Streaming |
    | Livebox / Orange | Router |
    | Bbox / Bouygues | Router |
    | SFR Box | Router |
    | Inférence via `deviceType` UPnP | MediaRenderer→Streaming, MediaServer→NAS… |
    | Fallback | IoT |

  **Niveau 5 — APIs spécifiques (non authentifiées) :**
  - **Philips Hue Bridge** : `GET http://<ip>/api/config`
    - Extrait : `name`, `modelid` (BSB002 → "Hue Bridge v2"), `swversion` → `os`
    - Renseigne : `manufacturer="Philips Hue"`, `category="SmartHub"`, `source="HueAPI"`
  - **Synology DSM** : `GET http://<ip>:5000/webapi/query.cgi?api=SYNO.API.Info&…`
    - Confirme la présence d'un DSM ; modèle extrait depuis le XML UPnP (DS224+, RS…)
    - Renseigne : `manufacturer="Synology"`, `category="NAS"`, `os="DSM"`, `source="SynologyAPI"`
  - **Freebox** : `GET http://<ip>/api_version`
    - Extrait : `device_name` (ex: "Freebox Ultra"), `device_type` (V9→Ultra, V8→Pop,
      V7→Révolution, V6→Mini 4K), `firmware_version`
    - Renseigne : `manufacturer="Free"`, `category="Router"`, `os="FreeboxOS x.y"`,
      `source="FreeboxAPI"`

- **`NetworkScanner::_mergeSsdp()`** — fusion des résultats SSDP dans la liste ARP :
  - Enrichit les champs vides d'un device déjà découvert par ARP (sans écraser mDNS/PTR)
  - Ajoute les devices UPnP non détectés par ARP (cas rare mais possible)
  - Appelée après `_resolveHostnames()` dans `_run()`, sous protection du mutex

- **UI — `web_src/scan.html`** :
  - Nouveaux badges source : `UPnP`, `Hue`, `DSM`, `Freebox` (avec tooltips explicatifs)
  - Champ `os` affiché sous le modèle (`.mfr-os`) — ex: "FreeboxOS 4.8.3", "Hue FW 1.60"

- **UI — `web_src/styles.css`** :
  - Badges source : `.source-ssdp` (vert pâle), `.source-hue` (orange), `.source-synology` (bleu),
    `.source-freebox` (violet)
  - Nouvelles catégories : `.type-smarthub` (rose/fuchsia), `.type-speaker` (vert)
  - Nouvelle classe `.mfr-os` pour le texte OS en italique sous le modèle

### Modifié

- **`src/modules/network_scanner.h`** : déclaration de `_mergeSsdp()`
- **`src/modules/network_scanner.cpp`** :
  - Import de `ssdp_scanner.h`
  - Appel de `_mergeSsdp()` en fin de `_run()` (après hostname resolution, avant `_addSelfEntry`)
  - Stack FreeRTOS augmentée **16 → 20 Ko** (HTTP client + XML parsing SSDP)
- **`platformio.ini`** : `PROJECT_VERSION` → `0.0.8`

### Technique

- **HTTP GET sans HTTPClient** : implémentation directe sur `WiFiClient` pour limiter la
  consommation de stack FreeRTOS ; timeout court (2 s), corps limité à 16 Ko par réponse
- **Déduplication LOCATION** : `std::vector<String>` parcouru linéairement — suffisant pour
  les ≤ 30 devices UPnP typiques d'un réseau domestique
- **Robustesse XML** : deux stratégies de recherche (`<tag>` exact puis `:<tag>`) ;
  fermeture `</` cherchée après la valeur sans nécessiter le nom exact du tag fermant —
  tolérant aux namespaces et aux malformations courantes
- **APIs non bloquantes** : chaque appel API spécifique a son propre timeout de 2 s ;
  si le device ne répond pas, le résultat SSDP de base est conservé sans erreur fatale
- **Source prioritaire** : `source` ARP (mDNS/PTR) remplacée par la source API enrichie
  (HueAPI > SynologyAPI > FreeboxAPI > SSDP) si disponible ; source MAC conservée en fallback

### Documentation

- `ROADMAP.md` mis à jour : SSDP/UPnP et APIs Hue/Freebox déplacés en ✅ Réalisé
- `README.md` mis à jour : nouvelles fonctionnalités, structure du projet, badges source
- `docs/WARNINGS.md` mis à jour : limitations SSDP, struct NetworkDevice corrigée
- Nouvelles docs débutant : `docs/GETTING_STARTED.md`, `docs/ARCHITECTURE.md`,
  `docs/PROTOCOLS.md`

---

## [0.0.7] — 2026-06-15 (suite — refactoring CSS & outils)

### Amélioré

- **CSS entièrement externalisé dans `styles.css`** : les blocs `<style>` inline ont été
  supprimés des trois pages HTML. `styles.css` est la source unique pour tous les styles,
  organisée en 12 sections (reset, conteneurs, en-tête, nav, carte, footer, puis sections
  spécifiques à chaque page). Le script `minify_web.py` l'injecte inline dans chaque page
  lors de la minification — l'ESP32 continue de servir du HTML auto-contenu.

- **Polices légèrement agrandies** pour une meilleure lisibilité :

  | Élément | Avant | Après |
  |---|---|---|
  | Navigation | 0.80 rem | 0.875 rem |
  | Données réseau (label/valeur) | 0.875 rem | 0.90 rem |
  | Tableau (cellules) | 0.80 rem | 0.85 rem |
  | En-têtes de colonne | 0.65 rem | 0.72 rem |
  | Pied de page | 0.78 rem | 0.85 rem |
  | Sous-titre en-tête | 0.72 rem | 0.80 rem |
  | Pastille version | 0.63 rem | 0.72 rem |
  | Badges source/catégorie | 0.58–0.65 rem | 0.65–0.72 rem |

- **Largeur de page gérée par classe CSS** (`body.page-scan` → max-width 960px,
  défaut → 520px) : élimine les règles dupliquées entre les pages et simplifie
  la maintenance.

- **Outils Python mis à jour** :
  - `extract_web_sources.py` : réécrit pour extraire les constantes PROGMEM
    `R"HTML(...)HTML"` réelles (index, scan, ota) ; mode dry-run par défaut,
    `--force` requis pour écraser les sources existantes
  - `validate_html.py` : valide les 3 pages réelles + template ; vérifie la
    présence du `<link styles.css>`, la structure `.site-hdr`/nav/footer,
    l'unicité des IDs ; supprime les vérifications `data-i18n` hors-sujet
  - `minify_web.py` : affiche le gain de compression réel (source HTML + styles.css
    vs sortie minifiée)

- **`web_src/` restructuré** : `styles.css` comme source unique, `template.html`
  et `README.md` réécrits pour documenter la vraie architecture Gateway Lab V1 ;
  suppression des fichiers hérités d'un projet précédent (`app.js` 119 Ko,
  `app-lite.js`, `README_FR.md`).

---

## [0.0.7] — 2026-06-15 (suite — passe cosmétique UI)

### Amélioré

- **En-tête cohérent sur toutes les pages** : chaque page (`/`, `/scan`, `/update`) affiche
  désormais un en-tête identique avec le nom du projet « Gateway Lab V1 », une pastille
  de version (`v0.0.7` récupérée dynamiquement via `/api/status`) et un sous-titre
  contextuel (ex. « Passerelle réseau locale », « Équipements réseau », « Mise à jour OTA »).

- **Navigation présente sur toutes les pages** : la barre de navigation (Accueil /
  Équipements / OTA) est maintenant visible sur les trois pages, y compris la page OTA
  qui en était dépourvue.

- **Pied de page unifié** : chaque page affiche un pied de page commun avec la date/heure
  de la dernière actualisation à gauche et un lien « ← Accueil » à droite.

- **Contraste du texte amélioré** : les textes de couleur `#475569` (trop sombre sur fond
  `#1e293b`) remplacés par `#94a3b8` sur l'ensemble des pages pour une lisibilité correcte.

---

## [0.0.7] — 2026-06-15

### Corrigé

- **Catégorie (badges Type) absente sur la page Équipements** : le header PROGMEM
  `include/web_interface_scan.h` n'avait pas été régénéré après le renommage
  `d.type` → `d.category` dans `web_src/scan.html`. L'ESP32 servait encore
  l'ancienne version minifiée qui référençait `d.type` (toujours `undefined`
  dans la réponse JSON v0.0.7). Correction : regénération via
  `python tools/minify_web.py` — le header intègre maintenant `d.category`,
  `d.model` et `d.source`.

- **ESP32 absent de sa propre liste d'équipements** (`network_scanner.cpp`) :
  Le protocole ARP ne peut pas découvrir sa propre adresse IP (pas de réponse ARP
  pour soi-même). L'ESP32 n'apparaissait donc jamais dans le tableau.
  Correction : nouvelle méthode `_addSelfEntry()` injectée en fin de `_run()` ;
  crée une entrée avec `WiFi.localIP()`, `WiFi.macAddress()`, `hostname = MDNS_HOSTNAME`,
  `category = "Gateway"`, `source = "Self"`. Idempotente entre deux scans.

- **Badge "PTR" opaque pour l'utilisateur** (`web_src/scan.html`) :
  Le badge affiché à côté du nom d'hôte ne donnait aucune indication sur sa
  signification. Correction : renommé `"DNS↩"` (flèche inverse = résolution inverse)
  avec tooltip `title="DNS inverse (PTR) — nom fourni par le routeur / box"`.
  Le badge `"Self"` pour l'ESP32 affiche `"ESP32"` avec tooltip explicatif.

- **Modèle sous le fabricant peu lisible** (`web_src/scan.html`) :
  La ligne modèle (ex: "Livebox") était trop sombre (#64748b) par rapport à
  la couleur de fond. Correction : passage à #94a3b8 et margin-top légèrement
  augmenté pour une meilleure séparation visuelle avec le badge fabricant.

### Ajouté

- **`src/modules/hostname_resolver.h/.cpp`** : nouveau module de résolution des noms d'hôtes
  - **mDNS passif** : écoute multicast sur 224.0.0.251:5353 pendant le sweep ARP ;
    les enregistrements A (nom.local → IPv4) peuplent un cache interne sans interrogation active
  - **PTR DNS batch** : pour chaque IP sans hostname mDNS, envoie des requêtes
    `d.c.b.a.in-addr.arpa` au DNS du réseau ; toutes les requêtes sont envoyées simultanément,
    réponses collectées dans une fenêtre unique de 500 ms (impact temps total : ≤ 500 ms)
  - Priorité de résolution : mDNS > PTR DNS
  - Non bloquant : timeout court, graceful fallback si le réseau ne répond pas
  - Champ `source` renseigné : `"mDNS"`, `"PTR"`, `"MAC"` ou `""`

- **`src/modules/isp_detector.h`** : détection des boxes des FAI français (header-only)
  - **Free / Freebox** : Ultra, Pop, Révolution, Delta, Mini 4K
    (signatures hostname : `freebox-*` ; OUI : `freebox sas`, `free sas`)
  - **Orange / Livebox** : 4, 5, 6
    (signatures hostname : `livebox`, `livebox4`… ; OUI : `orange`)
  - **SFR / SFR Box** : Box Plus, Box 8
    (signatures hostname : `sfrbox`, `sfr-box`, `sfr-*` ; OUI : `sfr`)
  - **Bouygues / Bbox** : Miami, Ultym
    (signatures hostname : `bbox*` ; OUI : `bouygues`)
  - Remplit `manufacturer` (marque FAI), `model` (ex: "Livebox 6"), `category` = "Router"
  - Détection non bloquante, 100 % locale (pas de requête réseau)

- **Champ `hostname` effectivement renseigné** : `_resolveHostnames()` n'est plus un no-op —
  implémentation complète avec `HostnameResolver`

### Modifié

- **`struct NetworkDevice`** : refactoring des champs
  - `type` → `category` (alignement avec `OuiEntry.category`)
  - Ajout `model` (ex: "Freebox Ultra", "Livebox 6", `""` si inconnu)
  - Ajout `os`     (usage futur — vide en v0.0.7)
  - Ajout `source` (`"mDNS"` | `"PTR"` | `"MAC"` | `""`)

- **`network_scanner.cpp`** :
  - `_readArpTable()` : `h.type` → `h.category`
  - `_sweepSubnet()` : appel de `hostnameResolver.update()` après chaque lot ARP
    pour collecter les annonces mDNS reçues pendant le sweep
  - `_run()` : `hostnameResolver.begin()` / `.clearCaches()` avant le sweep,
    `hostnameResolver.end()` après la résolution
  - `_resolveHostnames()` : implémentée (remplace le no-op)
  - `resultsToJson()` : champ `"type"` → `"category"`, ajout `"model"`, `"os"`, `"source"`
  - Stack FreeRTOS : 8 192 → 16 384 octets (lwIP + résolution DNS + `std::map`)

- **`web_src/scan.html`** :
  - `d.type` → `d.category` dans `renderDevices()`
  - Badge **source** sur le hostname (`mDNS` vert, `PTR` violet)
  - Modèle affiché sous le fabricant (ex: "Freebox Ultra" sous "Free")
  - Fonction `esc()` : échappement HTML des valeurs serveur (XSS defense)
  - `max-width` porté à 900 px pour accommoder la colonne enrichie
  - Titre colonne : "Type" → "Catégorie", "Nom" → "Nom d'hôte"

- **`platformio.ini`** : `PROJECT_VERSION` → `0.0.7`

### Technique

- **Écoute mDNS et ESPmDNS coexistent** grâce à `SO_REUSEADDR` (activé par `beginMulticast`) ;
  si la jointure du groupe échoue, le système bascule silencieusement sur PTR DNS uniquement
- **Batch PTR** : toutes les requêtes envoyées en rafale → une seule fenêtre d'attente de 500 ms
  pour N équipements (vs N × timeout en mode séquentiel)
- **ISP detection** : appliquée après résolution hostname pour bénéficier du nom complet ;
  n'écrase pas les champs déjà renseignés par l'OUI si la détection FAI ne matche pas
- **`HostnameResolver`** : instance globale partagée, appelée uniquement depuis la tâche FreeRTOS
  du scanner → pas de mutex nécessaire pour le resolver lui-même

### Résultat utilisateur

- Les équipements apparaissent désormais avec un nom exploitable lorsque le routeur fournit un enregistrement PTR DNS.
- Les équipements compatibles mDNS sont identifiés automatiquement.
- Les box Internet françaises sont reconnues et catégorisées sans configuration manuelle.
- L'ESP32 apparaît dans sa propre liste d'équipements.
- L'inventaire réseau devient lisible sans avoir à interpréter les adresses IP ou MAC.
- Les badges et tooltips expliquent désormais la provenance des informations affichées.
---

## [0.0.6] — 2026-06-15

### Corrigé

- **Services réseau non relancés après reconnexion WiFi** (`wifi_manager.cpp`) :
  `WiFiManager::loop()` détectait la perte du WiFi et déclenchait `_multi.run()`, mais ne
  rappelait jamais le callback une fois le WiFi rétabli. OTA, scanner et serveur web
  restaient donc inactifs après une reconnexion automatique.
  Correction : `_storedCb` conserve le callback de `begin()` ; la transition
  déconnecté → connecté est détectée via `_wasConnected` et rappelle les services.

- **mDNS non republié après reconnexion** (`wifi_manager.cpp`) :
  `MDNS.begin()` n'était appelé qu'une seule fois dans `begin()`. Après perte et
  reprise du WiFi, `gateway-lab-v1.local` devenait injoignable.
  Correction : `_startMdns()` est extrait en fonction interne et rappelé lors de
  chaque reconnexion détectée dans `loop()`.

- **Fuite de mutex sur double initialisation** (`network_scanner.cpp`) :
  Rappeler `NetworkScanner::begin()` (via le callback de reconnexion) créait un
  second mutex FreeRTOS sans libérer le premier.
  Correction : guard `if (_mutex) return;` en tête de `begin()`.

- **Callbacks ArduinoOTA empilés à chaque reconnexion** (`ota_manager.h/.cpp`) :
  `ArduinoOTA.onStart/onEnd/onError` s'enregistrent en liste — les rappeler à
  chaque reconnexion faisait croître la chaîne indéfiniment.
  Correction : flag `_callbacksRegistered` ; les lambdas ne s'enregistrent qu'une
  fois. `ArduinoOTA.begin()` est toujours rappelé (nécessaire pour ré-ouvrir le
  port UDP et republier le service mDNS après reconnexion).

- **Routes HTTP re-enregistrées inutilement** (`web_server.h/.cpp`) :
  `WebServerModule::begin()` pouvait être rappelé par le callback de reconnexion,
  dupliquant les handlers et rappelant `_server.begin()` sur un socket déjà actif.
  Le socket TCP du WebServer persiste à travers les reconnexions WiFi côté lwIP.
  Correction : flag `_started` ; `begin()` est idempotent.

- **Injection JSON dans `resultsToJson()`** (`network_scanner.cpp`) :
  Les champs `hostname` et `type` étaient concaténés directement dans la chaîne JSON
  sans échappement. Toute valeur contenant `"` ou `\` (ex. nom d'hôte mDNS avec
  guillemet) produisait un JSON invalide ou une injection.
  Correction : remplacement de la concaténation manuelle par `JsonDocument` /
  `serializeJson()` (ArduinoJson déjà présent dans `lib_deps`).

### Technique

- Modèle de robustesse adopté : chaque `begin()` exposé publiquement est désormais
  **idempotent** — appelable plusieurs fois sans effet de bord (guard mutex,
  flag `_started`, flag `_callbacksRegistered`).
- `WiFiManager` est le seul point de déclenchement post-reconnexion : il stocke le
  callback applicatif et le rappelle lors de toute transition déconnecté → connecté,
  sans modifier `main.cpp`.
- ArduinoJson étendu à `resultsToJson()` : cohérence avec `/api/status` qui utilisait
  déjà `JsonDocument` dans `web_server.cpp`.

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
