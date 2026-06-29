# Changelog — Gateway Lab

Toutes les modifications notables sont documentées ici.
Format : [Semantic Versioning](https://semver.org/)

---

## [1.4.8] - 2026-06-29

### Corrige

- **Compteur « vu Nx » (`seenCount`) toujours incohérent après le correctif
  1.4.5 — incrémenté par la surveillance continue en plus des scans**
  (`network_scanner.cpp`, `_updateHistory()`, `_monitorTick()`) : le
  correctif 1.4.5 a bien réinitialisé `online` entre deux sweeps, mais
  `seenCount` était en réalité incrémenté à *deux* endroits distincts —
  une fois par scan complet déclenché par l'utilisateur (`_run()` via
  `_updateHistory()`), et une seconde fois à chaque tick de la
  surveillance continue en arrière-plan (`_monitorTick()`, toutes les
  quelques minutes, indépendamment de tout scan manuel). Résultat : après
  5 scans manuels espacés dans le temps, plusieurs ticks de surveillance
  s'étaient glissés entre chaque scan et le compteur affichait « vu 25x »
  au lieu de « vu 5x ». Correction : `seenCount` n'est désormais incrémenté
  que dans `_updateHistory()` (scans complets), plus jamais dans
  `_monitorTick()` — la surveillance continue met toujours à jour
  `presenceCount`, `lastSeenEpoch`, les compteurs de reconnexion/stabilité
  et `totalOnlineSeconds`/`totalOfflineSeconds`, mais ne touche plus à
  `seenCount`.

## [1.4.7] - 2026-06-29

### Corrige

- **Équipements rattachés à un répéteur affichés par défaut sous la box
  opérateur sur la page Topologie** (`network_scanner.cpp`,
  `_discoverTopologyViaSnmp()`) : la découverte automatique du rattachement
  (table de pontage SNMP, `dot1dTpFdbTable`) n'interrogeait que les
  équipements déjà devinés « Router »/répéteur/point d'accès via leur
  hostname DHCP ou leur réponse SSDP. La plupart des répéteurs mesh grand
  public (Deco, Orbi, eero, etc.) ne sont pas reconnus par cette heuristique
  — hostname générique ou absent — et n'étaient donc jamais interrogés en
  SNMP : leurs clients restaient rattachés par défaut à la racine. La
  découverte interroge désormais tout équipement en ligne (hors la
  passerelle ESP32 elle-même) ; répondre avec une table de pontage non vide
  est en soi la preuve qu'un équipement relaie du trafic pour d'autres MAC,
  donc qu'il joue un rôle d'AP/répéteur/switch, sans qu'il soit nécessaire
  de le deviner au préalable. Un équipement ainsi confirmé mais encore
  classé génériquement se voit également étiqueté `Point d'accès /
  Répéteur (détecté via SNMP)` pour rester cohérent avec l'affichage de la
  page Topologie (v1.4.6). Reste sans effet pour les répéteurs qui
  n'exposent aucun agent SNMP en lecture publique — le rattachement manuel
  par glisser-déposer demeure alors la seule solution.

## [1.4.6] - 2026-06-29

### Ajoute

- **Couleur dédiée pour les équipements rattachés directement à la box
  opérateur ou à un point d'accès / répéteur** (`topology.js`,
  `topology.html`) : sur la page Topologie, `nodeColor()` distinguait déjà la
  racine, les points d'accès/répéteurs et les rattachements automatiques
  (SNMP, fiable/incertain), mais un équipement rattaché manuellement ou par
  défaut à la racine ou à un point d'accès recevait la même couleur grise
  que tout équipement rattaché plus profondément dans l'arbre (rattachement
  « non déterminé »). `buildTree()` mémorise désormais le nœud parent de
  chaque équipement, et `nodeColor()` applique une couleur sarcelle
  (`#2dd4bf` sur fond `#0f3d3a`, contraste suffisant pour rester lisible)
  lorsque le parent direct est la racine ou un point d'accès/répéteur. La
  légende de la page a été mise à jour en conséquence. Le glisser-déposer
  des équipements (`attachDragAndDrop()`) n'a pas été modifié.

## [1.4.5] - 2026-06-29

### Corrige

- **Compteur « vu X fois » (`seenCount`) incohérent et filtre « En ligne »
  toujours actif** (`network_scanner.cpp`) : l'état `online` d'un équipement
  n'était jamais remis à `false` entre deux scans — `_readArpTable()` ne fait
  que positionner `online = true` quand l'appareil répond, mais rien ne
  réinitialisait cet état avant le sweep suivant. Un équipement détecté une
  seule fois restait donc considéré « en ligne » indéfiniment, faussant le
  filtre « En Ligne » et gonflant `seenCount` à chaque scan même après sa
  disparition réelle du réseau. Correction : réinitialisation de `online` à
  `false` pour tous les équipements connus juste avant chaque sweep, à la
  fois dans le scan complet (`_run()`) et dans la surveillance périodique
  légère (`_monitorTick()`).

### Ajoute

- **Filtre « Nouveau » sur la page Équipements** (`scan.html`, `scan.js`,
  `network_scanner.cpp`) : un nouveau champ `isNew` est calculé côté firmware
  (`resultsToJson()`) à partir de `firstSeenEpoch` — un équipement est
  considéré « Nouveau » pendant les 24h suivant sa première détection. Une
  case à cocher dédiée permet de filtrer sur ce statut, et un badge
  « Nouveau » est affiché à côté du nom de l'équipement concerné.
- Les cases à cocher « Favoris uniquement » et « En ligne uniquement » ont
  été renommées respectivement « Favoris » et « En Ligne » (page
  Équipements).

## [1.4.4] - 2026-06-27

### Corrige

- **Page `/debug` : numéro de boot affiché dans le titre incohérent avec le
  corps de l'entrée** (`debug.js`) : le titre calculait un rang relatif
  (`total - index`, position dans la liste affichée) tandis que le corps
  affichait `bootCount`, le compteur absolu persisté en NVS — les deux
  pouvaient diverger (ex. "Boot #10" en titre, "Boot #16" dans le détail
  de la même entrée). Le titre utilise désormais `bootCount` partout.
- **Heap/uptime à 0 o affichés pour un redémarrage volontaire (OTA)**
  (`boot_log.cpp`, `ota_manager.cpp`) : l'instantané périodique
  `RuntimeStats` (heap libre, bloc max, etc.) n'est rafraîchi par
  `service()` que toutes les `BOOT_LOG_STATS_INTERVAL_MS` (30 s) ; un
  redémarrage déclenché par `ESP.restart()` juste après la fin d'un upload
  OTA pouvait donc persister un instantané obsolète ou nul selon le hasard
  du dernier tic. `service()` accepte désormais un paramètre `force`
  (ignorant le délai), appelé juste avant `ESP.restart()` dans le handler
  `/update` pour garantir un instantané à jour au moment du redémarrage.

---

## [1.4.3] - 2026-06-27

### Corrige

- **Fuite de socket potentielle dans le scan de ports** (`port_scanner.cpp` :
  `_httpBanner()`, `_tcpBanner()`, `_httpGet()`) : en cas d'échec de
  `WiFiClient::connect()`, la fonction retournait immédiatement sans appeler
  `client.stop()`, comptant sur le destructeur RAII pour libérer le socket
  lwIP. Sous Arduino-ESP32, cette libération peut être retardée, ce qui
  ajoute de la pression sur le pool de sockets lwIP (`CONFIG_LWIP_MAX_SOCKETS
  = 16`, déjà partagé avec le serveur web) et sur le tas lors de scans
  prolongés (28 équipements × jusqu'à 5 sondes HTTP IoT par équipement dans
  `_probeIoTApis()`, répété à chaque cycle de rescan). Suspecté comme
  contributeur à un crash observé après ~1h d'uptime (heap libre = 0 o juste
  après le démarrage d'un scan de ports, boot #7/crash #1). `client.stop()`
  est maintenant appelé explicitement avant chaque retour anticipé.

---

## [1.4.2] - 2026-06-26

### Ajoute

- **Date et heure des évènements dans le journal de redémarrage** (page
  `/debug`) : chaque entrée affiche désormais la date/heure réelle du
  redémarrage (et non plus seulement l'uptime relatif), nécessaire pour
  évaluer la fréquence des reboots. Deux sources, choisies par ordre de
  fiabilité : `resetEpoch`, capturé au dernier battement périodique (30 s)
  avant la coupure (le plus proche possible de l'instant réel de
  l'évènement) ; à défaut, `bootEpoch`, lu dès le démarrage suivant — cela
  fonctionne dès l'instant T puisque l'horloge interne de l'ESP32
  (`time(nullptr)`) n'est remise à zéro que par une coupure d'alimentation
  ou un reset franc, pas par un redémarrage logiciel/crash/watchdog. Si
  aucune des deux n'est disponible (jamais synchronisé NTP, ex. reseau sans
  accès Internet), l'entrée l'indique explicitement plutôt que d'afficher
  une date fausse.

### Corrige

- Incrément du magic number du buffer RTC du journal de redémarrage
  (ajout du champ `lastEpochSec`) pour éviter une relecture de l'ancien
  layout après mise à jour firmware (cf. avertissement déjà présent dans
  `boot_log.cpp` suite à l'incident similaire du Patch 7→8).

---

## [1.4.1] - 2026-06-25

### Ajoute

- **Taux de confiance pour la découverte automatique de topologie** : chaque
  rattachement déduit par SNMP (`topologyParentAuto`) porte désormais un
  `topologyParentConfidence` (0-100). Une MAC vue dans une seule table de
  pontage (FDB) pendant le sweep reçoit une confiance élevée (85%) ; une MAC
  vue simultanément dans plusieurs tables de pontage d'AP/répéteurs distincts
  (mobilité WiFi en cours de sweep, ou entrée de pontage partiellement
  obsolète) reçoit une confiance dégradée (40%) plutôt qu'un choix arbitraire
  entre les AP candidats.
- **Codes couleur dans la topologie** : la box WiFi et les répéteurs/points
  d'accès restent en vert, la racine reste en bleu marine ; les équipements
  rattachés automatiquement avec une confiance élevée (≥ 60%) apparaissent en
  bleu, ceux avec une confiance faible/ambiguë en orange. Le pourcentage de
  confiance est affiché sous le nom de l'équipement dans l'arbre.

### Corrige

- `topologyParentAuto` et `topologyParentConfidence` n'étaient pas reportés
  d'un cycle de surveillance ARP au suivant (seul `topologyParent` l'était) :
  un rattachement automatique pouvait perdre silencieusement son statut
  "automatique" et sa confiance à chaque tick, sans pour autant être
  recalculé avant le prochain sweep SNMP (30 min). Les deux champs sont
  désormais reportés au même titre que `topologyParent`.
- `topologyParentConfidence` n'était pas persisté dans le stockage LittleFS
  (`device_store.cpp`) : la confiance affichée revenait à 0 après chaque
  redémarrage de l'équipement, même si le rattachement automatique
  lui-même survivait. Le champ est désormais chargé/sauvegardé comme les
  autres champs de topologie.

---

## [1.4.0] - 2026-06-25

### Ajoute

- **Découverte automatique de la topologie réseau par SNMP** : les
  équipements détectés comme routeur/point d'accès/répéteur sont interrogés
  via la Bridge MIB standard (`dot1dTpFdbTable`, table de pontage, OID
  `1.3.6.1.2.1.17.4.3.1.1`) par une marche SNMP (`GetNextRequest` successifs,
  `SnmpScanner::walkBridgeMacTable`). Quand un équipement en amont expose un
  agent SNMP en lecture publique, chaque MAC trouvée dans sa table de
  pontage lui est automatiquement rattachée (`topologyParent`), affranchissant
  l'utilisateur du glisser-déposer manuel pour ces équipements — qu'ils
  soient en mode routeur ou en mode point d'accès transparent. Un nouveau
  drapeau `topologyParentAuto` distingue un rattachement déduit
  automatiquement d'un rattachement déclaré manuellement : ce dernier n'est
  jamais écrasé par la découverte SNMP, qui ne complète/rafraîchit que les
  entrées encore vides ou elles-même automatiques. Sweep périodique (30 min
  par défaut, `TOPOLOGY_SNMP_SWEEP_INTERVAL_MINUTES`), entièrement best-effort
  et silencieux si aucun équipement ne répond — c'est le cas de la plupart
  des répéteurs mesh grand public (TP-Link Deco, Orbi, eero…) qui n'exposent
  pas d'agent SNMP. Sur la carte de topologie, un rattachement déduit par
  SNMP est désormais affiché avec un trait pointillé (trait plein = manuel ou
  racine).

---

## [1.3.0] - 2026-06-25

### Corrige

- **Equipements bloqués indéfiniment en « Identification en cours »** : ce
  libellé de catégorie n'était reconnu comme « générique » (donc remplaçable
  par une catégorie plus précise) par aucun des points d'entrée du pipeline
  d'identification (SSDP, DNS-SD, scan de ports, classification par signaux,
  enrichissement par hostname). Résultat : même un rescan manuel ne pouvait
  jamais faire sortir un équipement de cet état. Centralisé le test dans un
  helper partagé (`isGenericCategory()`, `network_scanner.h`) qui reconnaît
  désormais `""`, `"IoT"` et `"Identification en cours"` comme génériques, et
  appliqué ce helper à tous les points d'entrée concernés
  (`network_scanner.cpp`, `device_enricher.h`).

### Ajoute

- **Re-identification automatique périodique** des équipements restés sur une
  catégorie générique (« IoT » ou « Identification en cours ») : toutes les
  `RESCAN_SWEEP_INTERVAL_MINUTES` (60 min par défaut, `app_config.h`), un
  sweep met en file un scan approfondi pour chacun d'eux, sans intervention
  de l'utilisateur. S'appuie sur la file d'attente différée déjà existante
  (`_queueDeepScanLocked`/`_drainPendingScans`), donc sans jamais entrer en
  conflit avec un scan complet ou un rescan manuel en cours.
- **Bouton « Rescan non identifiés »** (page Équipements) : relance en un
  clic un scan approfondi sur tous les équipements actuellement classés en
  catégorie générique, séquentiellement, avec suivi de progression.

---

## [1.2.0] - 2026-06-24

### Modifie

- **Renommage du projet : « Gateway Lab V1 » devient « Gateway Lab »** (le
  suffixe de version n'avait pas sa place dans le nom). Mis à jour partout :
  titres des pages web, en-tête de l'interface, `User-Agent` HTTP émis par
  les scanners (`port_scanner.cpp`, `media_api_scanner.cpp`, `ssdp_scanner.cpp`),
  nom mDNS (`MDNS_HOSTNAME`, désormais `gateway-lab` — accessible via
  `http://gateway-lab.local`), `PROJECT_NAME` (`platformio.ini`), et
  documentation (`README.md`, `ROADMAP.md`, `INSTALLATION.md`,
  `CONTRIBUTING.md`, `docs/*.md`). Les entrées d'historique de ce fichier
  antérieures à cette version ne sont pas réécrites : elles reflètent le nom
  du projet tel qu'il était à l'époque.

### Ajoute

- **Cartographie graphique de la topologie réseau** (`/topology`) : rendu en
  arbre SVG fait maison (racine, points d'accès/répéteurs WiFi détectés,
  équipements rattachés), remplaçant l'ancien rendu textuel.
- **Détection automatique des répéteurs mesh** (TP-Link Deco, Orbi, eero,
  Nest WiFi, Velop…) par mot-clé sur le nom d'hôte (`device_enricher.h`),
  classés « Point d'accès / Répéteur mesh ».
- **Rattachement manuel des équipements WiFi** à leur point d'accès/répéteur
  via glisser-déposer directement sur la carte SVG (un scan ARP/SSDP seul ne
  peut pas déterminer à quel répéteur un appareil WiFi est relié) — persisté
  par équipement (`topologyParent`, `NetworkScanner::setTopologyParent`).
- **Racine de l'arbre configurable** : par défaut la box opérateur (catégorie
  `Router`) est choisie automatiquement plutôt que l'ESP32 lui-même
  (catégorie `Gateway`, qui n'est qu'un équipement du réseau comme un autre
  du point de vue de la topologie) ; un sélecteur dédié permet de forcer une
  racine différente, persistée en NVS (`setTopologyRoot`/`getTopologyRoot`,
  `GET`/`POST /api/topology/root`).

### Corrige

- **La racine de la topologie était toujours l'ESP32**, jamais la box
  opérateur, même quand celle-ci était correctement détectée (catégorie
  `Router`) : la résolution automatique cherchait en priorité la catégorie
  `Gateway` (l'ESP32 lui-même) avant `Router`. Inversé l'ordre de préférence.

---

## [1.1.1] - 2026-06-24

### Corrige

- **Page Historique : aucune déconnexion jamais visible pour les appareils
  mobiles, malgré des reconnexions répétées.** Dans `NetworkScanner::_monitorTick()`
  (`src/modules/network_scanner.cpp`), une absence courte (<30 min) d'un
  équipement classé mobile ne journalisait strictement aucun événement côté
  déconnexion (silence volontaire pour éviter le bruit), alors que la
  réapparition journalisait systématiquement `"reconnected"`. Résultat :
  l'historique affichait des chaînes de "Reconnecté" sans jamais la
  déconnexion correspondante. Une absence courte de mobile journalise
  désormais un événement discret `"offline_brief"`, symétrique au
  `"reconnected"` qui suit.

### Ajoute

- **Regroupement des reconnexions en chaîne sur la page Historique**
  (`web_src/history.js`) : lorsque plusieurs événements de reconnexion
  consécutifs d'un même équipement se suivent à moins de 20 minutes
  d'intervalle **sans aucune déconnexion explicite** entre eux (`offline`,
  `disappeared`, `mobile_left`, ou désormais `offline_brief`), ils sont
  fusionnés dans l'affichage en une seule entrée « ⚠️ Connexion instable
  détectée », avec le nombre de reconnexions et la fenêtre de temps
  concernée — au lieu d'empiler des "Reconnecté" sans cause visible. Si une
  déconnexion (même discrète) existe entre deux reconnexions, aucune fusion
  n'a lieu : chaque événement reste affiché individuellement, la cause étant
  déjà visible.
  - Nouveau type d'événement `offline_brief` (icône ⚪, catégorie de filtre
    "Déconnexions") et nouveau style `.hist-offline-brief`/`.hist-unstable`
    (`web_src/styles.css`).

---

## [1.1.0] - 2026-06-23

### Ajoute

- **Fingerprinting passif DHCP** (`src/modules/dhcp_sniffer.h/.cpp`) :
  module continu et indépendant du scanner réseau, démarré une fois le
  WiFi connecté. Ouvre un socket UDP non bloquant sur `0.0.0.0:67` et
  écoute les paquets DHCPDISCOVER/REQUEST émis par les autres équipements
  du réseau — sans jamais émettre de requête ni répondre. Extrait le
  hostname déclaré (option 12) et devine l'OS à partir du vendor class
  identifier (option 60 : `MSFT*` → Windows, `android-dhcp*` → Android,
  `dhcpcd*`/`udhcp*` → Linux). Table interne MAC → fingerprint bornée à
  64 entrées. `NetworkScanner::_enrichDevices()` consulte cette table en
  mémoire (lecture seule, aucune requête) pour compléter `hostname`/`os`
  des équipements quand ils sont encore vides, sans jamais écraser une
  source plus fiable (SSDP, API, mDNS/PTR…). Source affichée : `DHCP`.
  Zéro coût ajouté au scan complet, au scan ciblé ou à la surveillance
  continue.

## [1.0.9] - 2026-06-23

### Ajoute

- **Sondage de broker MQTT lors de la passe précise approfondie**
  (`src/modules/mqtt_scanner.h/.cpp`) : déclenché uniquement quand le profil
  déduit (`SmartHome`/`Unknown`) a le port 1883 ouvert. Connexion TCP
  unicast directe vers l'IP visée — CONNECT MQTT v3.1.1 anonyme, puis,
  si accepté sans authentification, souscription aux topics standards
  `$SYS/broker/version` et `$SYS/broker/clients/connected` (jamais aux
  topics applicatifs des appareils). Le modèle/catégorie de l'équipement
  sont enrichis avec la version du broker et son nombre de clients
  connectés, source `MQTT`. Aucun impact sur le scan complet ni sur la
  surveillance continue.

---

## [1.0.8] - Patch 8 - 2026-06-21

### Ajoute

- **Extension du journal de redémarrage (`BootLog`, FONCTIONNALITÉ TEMPORAIRE)**
  avec des informations supplémentaires pour le débogage de crashs/watchdogs,
  sans toucher à l'architecture existante (`src/modules/boot_log.h/.cpp`) :
  - Compteurs persistants `boot_count`/`crash_count` en NVS (`Preferences`,
    survivent aussi à une coupure d'alimentation), incrémentés à chaque
    démarrage et à chaque reset classé "anormal" (panic, watchdog, brownout).
  - Température interne du SoC (`temperatureRead()`) capturée à chaque boot.
  - Dernier état connu avant le reset (heap libre, plus gros bloc libre,
    uptime, état/RSSI/IP WiFi, dernière "tâche" signalée via
    `bootLog.setLastTask(...)`), grâce à un instantané RTC rafraîchi en
    continu par la nouvelle méthode `BootLog::service()` (à appeler depuis
    `loop()`).
  - Instantané périodique `RuntimeStats` (uptime, heap libre, plus gros bloc
    libre, nombre d'équipements connus, pages servies, appels API) rafraîchi
    toutes les `BOOT_LOG_STATS_INTERVAL_MS` (30 s par défaut) et persisté
    avec chaque entrée de boot.
  - Chaque ligne du buffer circulaire de logs est désormais un objet JSON
    compact incluant le heap libre et le plus gros bloc libre au moment du
    log, en plus du timestamp/niveau/tag/message.
  - Nouveaux réglages dans `include/app_config.h` : `LOG_BUFFER_SIZE`,
    `LOG_LINE_MAX_LEN`, `BOOT_LOG_STATS_INTERVAL_MS`.
  - `main.cpp` fournit le nombre d'équipements connus via
    `bootLog.setDevicesCountProvider(...)` et trace quelques tâches clés
    via `bootLog.setLastTask(...)` (scan réseau, sauvegarde manuelle).
  - `web_server.cpp` comptabilise désormais `pagesServed`/`apiCalls` via un
    petit wrapper interne `_on()` autour de l'enregistrement des routes.
  - Page `/debug` (`debug.html`/`debug.js`) mise à jour pour afficher toutes
    ces nouvelles données par démarrage enregistré.
  - Limite documentée : pas de capture de stack trace au PANIC (hors de
    portée d'un module Arduino autonome sans `esp_core_dump` — voir le
    commentaire dédié dans `boot_log.h`) ; la trace ESP-IDF reste visible
    uniquement sur le moniteur série, comme avant.

---

## [1.0.7] - Patch 7 - 2026-06-21

### Ajoute

- **Journal de redémarrage pour le débogage, sans moniteur série
  (FONCTIONNALITÉ TEMPORAIRE).** Nouveau module `src/modules/boot_log.h/.cpp` :
  un buffer circulaire des derniers logs (`Log::i/w/e/d`) est conservé en
  RAM `RTC_NOINIT_ATTR` (survit à un reboot logiciel, un crash, un
  watchdog ou un brownout — pas à une coupure d'alimentation). Au
  démarrage suivant, la raison du reset (`esp_reset_reason()` : panic,
  watchdog, brownout, reset logiciel/externe...) et le contenu du buffer
  précédent sont persistés dans `/bootlog.json` sur LittleFS (10 derniers
  démarrages, FIFO), consultables sur une nouvelle page `/debug`
  (lien "Debug" dans le menu) et via `GET`/`DELETE /api/bootlog`.
  Pensé pour être retiré facilement une fois le débogage terminé :
  toutes les additions sont regroupées et signalées par le commentaire
  « DEBOGAGE TEMPORAIRE » (voir `docs/DEVELOPMENT.md` pour la procédure
  de retrait complète).

---

## [1.0.6] - Patch 6 - 2026-06-19

### Corrige

- **Page Historique : le filtre « Favoris uniquement » restait vide même
  après le Patch 5.** La vraie cause : `loadFavorites()` traitait la
  réponse de `GET /api/devices` comme un tableau brut
  (`(list || []).forEach(...)`), alors que cet endpoint renvoie un objet
  `{ scanning, stats, devices: [...] }` (comme `scan.js` le fait déjà
  correctement via `data.devices`). `list.forEach` n'existe pas sur cet
  objet, l'exception était silencieusement absorbée par le `.catch()`, et
  `favoriteMacs` restait toujours vide — la correction du Patch 5
  (indexation par MAC et IP) ne pouvait donc jamais s'appliquer à aucune
  donnée. Correction : `loadFavorites()` lit désormais `data.devices`.

---

## [1.0.5] - Patch 5 - 2026-06-19

### Corrige

- **Page Historique : le filtre « Favoris uniquement » n'affichait jamais
  aucun résultat.** `history.js` indexait les équipements favoris
  exclusivement par adresse MAC (`favoriteMacs[d.mac]`), sans repli sur
  l'IP — contrairement à la convention utilisée partout ailleurs dans le
  projet (`scan.js` : `favKey = d.mac || d.ip` ; backend `network_scanner.cpp` :
  `(!d.mac.isEmpty() && d.mac == macOrIp) || d.ip == macOrIp`). Les
  équipements favoris dont la MAC n'était pas (encore) résolue, ainsi que
  les entrées d'historique enregistrées avec une MAC vide, ne pouvaient
  donc jamais correspondre, et le filtre restait systématiquement vide.
  Correction : `loadFavorites()` indexe désormais chaque équipement favori
  par sa MAC **et** son IP, et `renderHistory()` vérifie la correspondance
  sur les deux champs (`favoriteMacs[e.mac] || favoriteMacs[e.ip]`).

---

## [1.0.4] - Patch 4 - 2026-06-19

### Corrige

- **Page Historique : filtrage cassé, affichage incomplet par défaut.**
  Depuis l'ajout de la surveillance continue (v1.0.0), le firmware émet
  9 types d'évènements d'historique (`new`, `online`, `reconnected`,
  `mobile_returned`, `offline`, `disappeared`, `mobile_left`, `changed`,
  `identification_improved`), mais la page Historique (`history.js`)
  n'en connaissait que 4 (`new`, `online`, `offline`, `changed`) : les
  5 autres types — désormais les plus fréquents, car émis par chaque tick
  de surveillance ARP au lieu d'un scan complet seulement — n'avaient
  aucune case de filtre correspondante et étaient donc systématiquement
  exclus de l'affichage, quel que soit l'état des cases cochées. En
  pratique, seuls les évènements `changed` restaient visibles par défaut,
  et le filtre « Favoris uniquement » semblait sans effet puisqu'il
  s'appliquait à une liste déjà presque vide.
  Correction : chaque type d'évènement réel est désormais rattaché à l'une
  des 4 catégories de filtre existantes (`reconnected`/`mobile_returned`
  → "Reconnexions", `disappeared`/`mobile_left` → "Déconnexions",
  `identification_improved` → "Changements de champs"), via une table de
  correspondance (`EVENT_FILTER_CATEGORY`) ; tous les types ont également
  une icône et un libellé dédiés dans `EVENT_LABEL` pour un affichage
  correct (au lieu du repli générique `•`).

---

## [1.0.3] - Patch 3 - 2026-06-19

### Corrige

- **Scan complet qui semblait se relancer en boucle au démarrage.**
  `NetworkScanner::serviceMonitor()` ignorait correctement le tick de
  surveillance pendant qu'un scan complet/rescan était en cours
  (`_scanning == true`), mais ne mettait pas à jour `_lastMonitorTickMs`
  dans ce cas. Résultat : dès que le scan complet de démarrage (souvent
  long, 60-90 s) se terminait, le tick suivant était immédiatement déclaré
  "dû" (`_lastMonitorTickMs` encore à 0) et relançait un sweep ARP complet
  — visible dans les journaux comme un second scan (mêmes lignes "ARP passe
  1/2/3") démarrant à l'instant exact où le premier se terminait, donnant
  l'impression d'un scan qui boucle sur lui-même. `_lastMonitorTickMs` est
  désormais aussi horodaté lorsque le tick est sauté pour cause de scan en
  cours, reportant l'échéance d'un intervalle complet (même traitement que
  le mode dégradé).
- **Boucle infinie de passes précises sur les mêmes équipements.**
  `NetworkScanner::_updateHistory()` — appelée à la fois en fin de scan
  complet *et* en fin de chaque passe précise (`rescanDevice()`) — remettait
  en file un nouveau scan rapide (`_queueQuickScanLocked()`) dès que la
  confiance d'identification d'un équipement restait sous 35 %. Or un scan
  rapide ne recueille volontairement que très peu d'informations (ARP +
  hostname) et ne peut jamais faire remonter la confiance de certains
  profils au-dessus de ce seuil : chaque passe rapide se remettait donc
  elle-même en file indéfiniment, produisant un cycle ininterrompu sur le
  même petit groupe d'équipements (observé en journal : six IP enchaînées en
  boucle toutes les ~0,7 s, avec écriture de `/devices.json` à chaque
  itération). `_updateHistory()` accepte désormais un paramètre
  `allowRequeue` (vrai uniquement depuis un scan complet) : une passe
  précise ne se reprogramme plus jamais elle-même.

---

## [1.0.2] - Patch 2 - 2026-06-19

### Corrige

- **Surveillance continue : suppression des scans automatiques en boucle.**
  Le tick de surveillance (`NetworkScanner::_monitorTick()`) mettait
  automatiquement en file des passes rapides/approfondies (changement de
  champ détecté, confiance d'identification faible, nouvel équipement) ;
  cette file était ensuite vidée à *chaque* itération de `loop()`, donc en
  continu et indépendamment de l'intervalle de surveillance choisi par
  l'utilisateur. La surveillance continue se limite désormais strictement à
  la détection de présence par sweep ARP (déjà le cas du tick lui-même) :
  plus aucun scan rapide ou approfondi n'est déclenché automatiquement.
  Un scan approfondi reste accessible à tout moment, mais uniquement à
  l'initiative explicite de l'utilisateur (`/scan` ou rescan manuel d'un
  équipement via `/api/devices/rescan`).
- Les événements d'historique `"identification_improved"` et `"reconnected"`
  restent journalisés à titre informatif, mais ne déclenchent plus de scan.

---

## [1.0.1] - Patch 1 - 2026-06-19

### Ajoute

- **Activation/désactivation de la surveillance automatique du réseau**,
  réglable depuis la page Système : nouvel état persistant
  `NetworkScanner::setMonitorEnabled()`/`getMonitorEnabled()` (NVS,
  espace de noms `monitor`, clé `enabled`) ; lorsqu'elle est désactivée,
  `serviceMonitor()` ne fait plus rien (aucun tick, aucun drainage de la
  file différée).
- **Intervalle de scan configurable de 5 minutes à 1 heure** (au lieu de
  1-60 min libre) exposé dans l'interface — la borne API reste 1-60 min
  côté `NetworkScanner::setMonitorInterval()`.
- `GET /api/monitor` retourne désormais `{"enabled":bool,"intervalMinutes":int}`
  (auparavant `{"intervalMinutes":int}` seul).
- `POST /api/monitor` accepte désormais les paramètres optionnels `enabled`
  (`1`/`0`/`true`/`false`) et `minutes`, et renvoie l'état appliqué des deux
  réglages.
- `GET /api/system/backup` inclut désormais `monitorEnabled` et
  `monitorIntervalMinutes` dans la sauvegarde des paramètres de
  fonctionnement ; `POST /api/system/restore` les restaure si présents.

### Modifie

- **Réorganisation de l'interface — page Système** : ajout d'une carte
  « Surveillance automatique du réseau » (case d'activation + sélecteur
  d'intervalle 5 min/10 min/15 min/30 min/1 h) et déplacement de la carte
  « Sauvegarde / Restauration » des paramètres de fonctionnement
  (`/api/system/backup`, `/api/system/restore`), auparavant sur la page
  Équipements.
- **Page Équipements** : le menu « Données ▾ » ne propose plus que
  « Export CSV » (`/api/devices/export.csv`) et « Export JSON »
  (`/api/backup`, sauvegarde de l'inventaire) — les actions Sauvegarde et
  Restauration des paramètres de fonctionnement en ont été retirées (voir
  ci-dessus).

---

## [1.0.0] - 2026-06-19

> **Note (1.0.2)** : le comportement de mise en file automatique de passes
> rapides/approfondies décrit ci-dessous a été retiré au Patch 2 — voir
> [1.0.2](#102---patch-2---2026-06-19). La surveillance continue ne fait
> plus que sonder la présence (ARP).

### Ajoute

- **Surveillance continue du réseau et score de stabilité** : la passerelle
  passe d'un outil d'inventaire à scan manuel à un observateur permanent du
  réseau.
  - `NetworkScanner::serviceMonitor()` exécute, sans tâche FreeRTOS dédiée,
    un sweep ARP léger à intervalle configurable (1 à 60 min, 5 min par
    défaut, persisté en NVS) — jamais de SSDP/DNS-SD/WS-Discovery/SNMP ni
    d'appel aux API fabricants, et la passe est sautée (puis retentée au
    tour suivant) si un scan complet ou une passe précise est déjà en
    cours.
  - Un équipement inconnu détecté pendant la surveillance est immédiatement
    ajouté à l'inventaire (« Identification en cours »), puis une passe
    rapide est mise en file ; une passe approfondie suit si la confiance
    reste sous 60 % ou si le type est toujours vide.
  - Un équipement déjà connu qui réapparaît met simplement à jour
    `lastSeenEpoch`/`seenCount` et journalise un évènement `"reconnected"`,
    sans déclencher de scan.
  - Un changement important (hostname, IP, fabricant, type, ou confiance
    anormalement basse) met en file une passe rapide de rafraîchissement.
  - Nouveaux types d'évènements d'historique : `"reconnected"`,
    `"disappeared"`, `"identification_improved"`, `"mobile_left"`,
    `"mobile_returned"`.
  - Nouveaux compteurs de stabilité par équipement (persistés dans
    `/devices.json`) : `presenceCount`, `absenceCount`,
    `reconnectionCount`, `lastDisconnectEpoch`, `totalOnlineSeconds`,
    `totalOfflineSeconds`, `mobilityOverride`, `mobileAwayNotified`.
  - Score de stabilité (0-100 %) calculé pour les équipements fixes à
    partir du ratio temps en ligne / hors ligne, pénalisé par la
    fréquence de reconnexion ; les équipements mobiles renvoient `-1`
    (« N/A — non pénalisé ») et ne sont jamais comptés dans les
    statistiques de stabilité.
  - Classification mobile/fixe automatique par catégorie/type
    (smartphone, tablette, montre connectée, portable = mobile probable ;
    NAS, imprimante, caméra, routeur, TV, enceinte, hub/maison
    connectée, serveur, SBC, streaming = fixe probable), avec
    possibilité de forcer manuellement via `setMobility()` /
    `POST /api/mobility` (même logique que `setFavorite`/`setAlias`).
  - Gestion dédiée des absences d'équipements mobiles : aucune pénalité
    avant 30 min d'absence ; au-delà de 2h, évènement `"mobile_left"`
    journalisé une seule fois, puis `"mobile_returned"` au retour.
  - File d'attente différée (`_pendingQuickScan`/`_pendingDeepScan`),
    dédupliquée et protégée par mutex, vidée une entrée à la fois pour
    éviter toute tempête de scans.
  - Nouvel indicateur de santé réseau exposé via `networkHealthToJson()`
    et `GET /api/network/health` : équipements présents/connus, nouveaux
    équipements/reconnexions/instabilités des dernières 24h, et
    classement des équipements les moins stables.
  - Nouvelles routes API : `POST /api/mobility`, `GET /api/network/health`,
    `GET /api/monitor`, `POST /api/monitor` (lecture/écriture de la
    fréquence de surveillance).

## [0.9.2] - 2026-06-19

### Corrige

- **Export CSV inutilisable à cause de retours à la ligne dans les
  colonnes** : la colonne `hostname` de `/api/devices/export.csv`
  réutilisait `_enrichedHostname()`, une chaîne sur plusieurs lignes
  (nom déduit + hostname brut + source) prévue pour l'affichage UI, ce
  qui faisait apparaître les informations d'un même équipement sur
  plusieurs lignes physiques dans le fichier — même correctement
  échappée entre guillemets, la plupart des tableurs/scripts qui lisent
  une « ligne » comme un enregistrement coupent le CSV au mauvais
  endroit. `network_scanner.cpp` :
  - la colonne `hostname` utilise désormais directement `d.hostname`
    (valeur brute, une seule ligne) — le nom déduit reste disponible via
    le JSON (`hostnameDisplay`, page Équipements).
  - `csvField()` aplatit systématiquement tout retour à la ligne (`\n`,
    `\r`, `\r\n`) en espace avant échappement, quelle que soit la
    colonne (y compris les notes libres saisies par l'utilisateur), pour
    garantir qu'un équipement occupe toujours exactement une ligne dans
    l'export, quel que soit le tableur ou le script utilisé pour le lire.

## [0.9.1] - 2026-06-19

### Corrige

- **Passe précise réellement ciblée sur un seul équipement** : la passe
  approfondie (v0.9.0) lançait encore SSDP/UPnP, DNS-SD et WS-Discovery —
  des protocoles multicast qui sondent tout le sous-réseau et ne peuvent
  pas être restreints à une IP — ce qui faisait remonter des informations
  sur d'autres équipements (ex: « Synology détecté / Imprimante détectée »
  en scannant un PC) et prenait 20-30s. La logique est entièrement
  réécrite autour du principe « poser les bonnes questions à l'équipement
  ciblé » plutôt que « relancer tous les moteurs de découverte réseau » :
  - **Scan rapide** (1-3s, inchangé dans son objectif) : ARP/ICMP, PTR
    DNS, mise à jour du hostname, vérification de présence — rien
    d'autre.
  - **Scan approfondi** (<3s si rien d'exploitable, sinon quelques
    secondes au lieu de 20-30s) : étape 1, scan TCP unicast des ports de
    la cible uniquement (nouvelle liste dédiée `kRescanTargetPorts` :
    22, 53, 80, 135, 139, 443, 445, 515, 554, 631, 8080, 8443, 9100,
    5000, `port_scanner.h`/`.cpp`). **Si aucun port/service exploitable
    n'est trouvé, la passe s'arrête immédiatement** (message « Aucun
    service exploitable détecté. Passe approfondie terminée. »). Sinon,
    le profil d'équipement (`_profileFor()`) est réévalué à partir des
    ports découverts (plus fiable que les informations préexistantes),
    et seuls les modules pertinents pour ce profil sont lancés, toujours
    en requête unicast directe sur l'IP visée : NetBIOS (Computer/
    Unknown), API multimédia Cast/Sonos/Roku/Samsung (Streaming/
    SmartHome), SNMP (Printer/Unknown). **Plus aucun module SSDP, DNS-SD
    ou WS-Discovery n'est jamais lancé depuis une passe précise.**
  - **Fingerprint HTTP enrichi** : `port_scanner.cpp` capture désormais
    aussi le `<title>` de la page (en plus de l'en-tête `Server:`), et
    sonde les API constructeur Synology DSM et Philips Hue (en plus de
    Shelly/Tasmota/FritzBox déjà présents) une fois un port HTTP ouvert
    identifié.
  - `network_scanner.h`/`.cpp` : suppression de l'include
    `ws_discovery_scanner.h` (devenu inutile dans ce fichier), profil
    déduit enrichi avec des indices issus des ports ouverts (IPP/LPD →
    Printer, port 5000/Synology/QNAP → NAS, RPC/NetBIOS/RDP → Computer).

## [0.9.0] - 2026-06-19

### Ajoute

- **Scan précis en deux passes, orienté profil d'équipement** : le
  rafraîchissement ciblé d'un seul équipement (bouton ⟲ de la page
  Matériel) se décline désormais en deux passes distinctes :
  - **Scan rapide** (2-5s) : confirme l'identité et améliore le score de
    confiance avec un minimum de modules ciblés.
  - **Scan approfondi** (15-60s) : interroge l'ensemble des modules de
    découverte disponibles pour maximiser les informations exploitables
    (modèle précis, services exposés, ports...).
  - `network_scanner.h`/`.cpp` : nouvelle fonction `_profileFor()` qui
    déduit un profil probable (Computer, NAS, Printer, Mobile, Streaming,
    SmartHome, Network, IoT, Unknown) à partir des informations déjà
    connues (catégorie, services, fabricant, OS) — une simple hypothèse
    utilisée pour choisir les sondes les plus pertinentes, pas une
    classification figée. `rescanDevice(ip, deep)` prend désormais un
    paramètre de profondeur ; `_runRescan()` active sélectivement les
    modules (Ports TCP, NetBIOS, SSDP, DNS-SD, WS-Discovery, API
    multimédia, SNMP) selon le profil déduit et la passe demandée.
  - **Journal d'enrichissement** : chaque passe précise produit un
    journal des changements détectés (ex: « Modèle détecté : Google Nest
    Hub », « Service détecté : Cast », « Confiance : 30% → 70% »), ou
    « Aucune information supplémentaire détectée » si rien n'a changé.
    Exposé via `/api/devices/rescan/status` (champs `mode`, `profile`,
    `log`).
  - `web_server.h`/`.cpp`, `main.cpp` : `ScanProvider::rescanDevice`
    transmet désormais le paramètre `deep` ; `POST
    /api/devices/rescan` accepte un paramètre `mode` (`quick` par défaut,
    ou `deep`).
  - `web_src/scan.js` : le bouton « Rescanner » devient deux actions
    « Scan rapide » et « Scan approfondi » ; affichage en cours de
    « Passe rapide sur 192.168.x.x » / « Analyse approfondie sur
    192.168.x.x » puis du journal d'enrichissement en fin de passe.

## [0.8.10] - 2026-06-19

### Documentation

- Mise à jour de l'ensemble de la documentation utilisateur pour refléter
  la fonctionnalité "Nom humain" introduite en v0.8.9 et la version
  courante du projet : `README.md` (badge de version, tableau des
  fonctionnalités), `ROADMAP.md` (entrée v0.8.9 dans le tableau « Réalisé »),
  `docs/ARCHITECTURE.md`, `docs/PROTOCOLS.md` (champ `hostnameDisplay`
  exporté par `/api/devices`).

## [0.8.9] - 2026-06-19

### Ajoute

- **Nom "humain" du materiel dans la colonne Nom de la page Materiel** :
  quand le hostname reste generique (ex: `device-72` faute de mDNS/PTR),
  une premiere ligne deduite de `model` / `manufacturer` (+ `type`) /
  `category` affiche desormais un intitule comprehensible (ex: "Google
  Nest Hub"), suivi du hostname/alias et de la badge de source deja
  affiches auparavant.
  - `web_src/scan.js` : nouvelle fonction `humanDeviceName()`, colonne Nom
    rendue sur 2 blocs (`.name-human` + `.name-raw`).
  - Export CSV (`/api/devices/export.csv`) et JSON (`/api/devices`) :
    la colonne/le champ `hostname` (CSV) et un nouveau champ
    `hostnameDisplay` (JSON) reprennent le meme enrichissement sur 3
    lignes (nom humain, hostname/alias, source) - le champ `hostname` brut
    du JSON et la sauvegarde `/api/backup` restent inchanges pour ne pas
    casser la restauration.

## [0.8.8] - 2026-06-18

### Corrige

- **Barre de progression de la mise à jour firmware disparaissant juste
  après le transfert**, sans indication de ce qui se passait ensuite
  (vérification, flash, redémarrage) : `wifi.js` masquait tout le
  formulaire `#ota-form` (qui contient aussi la barre et le message) une
  fois le transfert terminé.
  - La barre reste désormais affichée à 100% et le message passe par les
    étapes « Transfert du firmware : X% » → « Vérification du firmware… »
    → « Firmware vérifié — redémarrage en cours… » → « Redémarrage en
    cours… » → « Redémarrage terminé — retour à l'accueil… ».
  - Seuls le sélecteur de fichier et le bouton « Mettre à jour » sont
    désactivés pendant la mise à jour (au lieu de masquer tout le bloc).
  - Une fois le redémarrage détecté (`/api/status` répond à nouveau), la
    page redirige vers l'Accueil (`/`) au lieu de rester sur `/wifi`.

## [0.8.7] - 2026-06-18

### Modifie

- **Restructuration de la page Paramètres, renommée Système** : la page
  regroupe désormais le réseau (état de connexion, réseaux enregistrés,
  ajout d'un réseau), la LED d'état, la mise à jour du firmware (OTA, ex
  page dédiée) et l'état système (heap libre, PSRAM libre, nombre
  d'équipements / maximum, nombre d'événements d'historique).
  - `web_src/wifi.html` / `web_src/wifi.js` réécrits pour intégrer le
    formulaire de mise à jour firmware et le nouveau bloc « État système ».
  - `src/modules/network_scanner.cpp` : `diagnosticsToJson()` retourne
    désormais `deviceCount`, `maxDevices` et `historyCount` en plus des
    champs existants.

### Supprime

- **Page OTA dédiée** (`GET /update`, `web_src/ota.html` / `ota.js`,
  `include/web_interface_ota.h`) : la fonctionnalité de mise à jour du
  firmware est intégrée à la page Système. La route `POST /update`
  (réception du fichier `.bin`) reste inchangée côté backend.

### Ajoute

- **`web_src/menu.html`** : bloc de navigation partagé, inliné dans chaque
  page via le nouveau marqueur `<!-- include:menu.html -->` (traité par
  `tools/minify_web.py` et simulé par `tools/validate_html.py`). Le lien
  actif est désormais marqué au runtime par un script JS embarqué dans
  `menu.html`, au lieu d'un `class="active"` statique dupliqué par page —
  simplifie la maintenance du menu sur l'ensemble des pages.

## [0.8.6] - 2026-06-18

### Corrige

- **Compatibilité `.name-cell`** : ajout de la propriété standard
  `line-clamp: 2` en complément du préfixe `-webkit-line-clamp: 2`
  (`web_src/styles.css`), pour les navigateurs ayant adopté la version
  non préfixée de la spécification.

## [0.8.5] - 2026-06-18

### Ajoute

- **Avertissement sur la page Équipements** rappelant que le scan réseau
  (initial ou global) découvre et identifie automatiquement les équipements
  présents, mais que certaines informations avancées peuvent nécessiter un
  scan ciblé de l'équipement pour interroger des services spécifiques ou
  des API propriétaires — pour clarifier que le scan de départ n'est pas
  conçu comme « agressif ».
  - `web_src/scan.html` : nouveau paragraphe `.scan-hint` sous l'en-tête de
    carte.
  - `web_src/styles.css` : style `.scan-hint` (texte discret, italique).

## [0.8.4] - 2026-06-18

### Corrige

- **Largeur de la page Équipements incohérente avec le reste de l'UI** et
  **barre de défilement horizontale** apparaissant en bas de la page lors de
  l'affichage d'un nom d'hôte long : la colonne « Nom d'hôte » (`.name-cell`)
  n'avait aucune contrainte de largeur ni de retour à la ligne, contrairement
  aux colonnes IP/MAC dont le contenu est intrinsèquement borné — un nom
  d'hôte sans espace pouvait forcer le tableau (`table-layout` auto) à
  dépasser la largeur de page standard de 960px.
  - `web_src/styles.css` : `.name-cell` limitée à `max-width: 220px`, repli
    sur deux lignes maximum (`-webkit-line-clamp: 2`) avec coupure de mot
    (`word-break`/`overflow-wrap`) plutôt que troncature brutale.
  - `web_src/scan.js` : ajout d'un attribut `title` sur `.name-cell` pour
    conserver le nom d'hôte complet au survol.
  - Commentaire de section 2 de `styles.css` mis à jour : la page Équipements
    partage désormais une largeur strictement identique aux autres pages
    (`.page-scan`, 960px), sans exception de « largeur adaptable ».

## [0.8.3] - 2026-06-18

### Corrige

- **Scan DNS-SD systématiquement vide** (`[INF][DNSSD] DNS-SD terminé — 0
  IP(s) résolue(s)` malgré des services réellement présents : Philips Hue,
  Echo, Synology…) : la fenêtre d'attente par type de service interrogé via
  `mdns_query_ptr()` était plancher à 100 ms, trop courte face au délai
  aléatoire de réponse de 20 à 120 ms imposé par la RFC 6762 §6 sur les
  enregistrements partagés (cas des PTR de découverte de service) — une
  marge quasi nulle restait pour l'aller-retour réseau, et la quasi-totalité
  des réponses arrivait hors fenêtre.
  - `src/modules/dns_sd_scanner.cpp` : plancher relevé à 300 ms
    (`MIN_QUERY_TIMEOUT_MS`), valeur par défaut de `scan()` et des deux
    appelants de `network_scanner.cpp` ajustée de 4000/5000 ms à 9000 ms
    pour conserver une fenêtre réaliste par type de service sur les ~29
    types interrogés (le scan tourne dans sa propre tâche FreeRTOS, non
    bloquant pour le reste du firmware — voir `docs/WARNINGS.md`).
  - Ajout d'une journalisation des échecs `mdns_query_ptr()`
    (`esp_err_to_name()`) — silencieusement ignorés auparavant — pour
    faciliter le diagnostic d'un futur scan vide.

### Documentation

- Reformulation à l'infinitif des commentaires et de la documentation
  utilisant des tournures « on fait ceci, on regarde cela » (commentaires
  C++ dans `dns_sd_scanner.*`, `hostname_resolver.cpp`, `network_scanner.cpp`,
  `ssdp_scanner.cpp`, `netbios_scanner.cpp`, et `docs/PROTOCOLS.md`).

---

## [0.8.2] - 2026-06-18

### Corrige

- **Conflit mDNS persistant après v0.8.1 — cause racine non traitée** :
  `MdnsManager` (v0.8.1) mutualisait correctement un socket entre
  `HostnameResolver` et `DnsSdScanner`, mais ne tenait pas compte d'un
  troisième consommateur, toujours actif : le composant mDNS d'ESP-IDF
  lui-même (`MDNS.begin()`, appelé dans `wifi_manager.cpp`), qui garde
  `224.0.0.251:5353` exclusivement pour son responder dès que le Wi-Fi est
  connecté (log `[INF][WiFi] mDNS actif : http://gateway-lab-v1.local`).
  Résultat : `MdnsManager::acquire()` échouait systématiquement
  (`[WRN][MdnsMgr] Impossible de rejoindre 224.0.0.251:5353`) — la
  découverte DNS-SD et la résolution mDNS passive ne fonctionnaient en
  réalité jamais dès que le responder mDNS était actif, c'est-à-dire en
  pratique en permanence.
  - `DnsSdScanner` (`src/modules/dns_sd_scanner.cpp`) est réécrit pour
    interroger directement le composant mDNS d'ESP-IDF via son API C
    (`mdns_query_ptr()` / `mdns_query_results_free()`, `<mdns.h>`) au lieu
    d'ouvrir un socket multicast applicatif — passe par le service mDNS
    déjà initialisé par `MDNS.begin()`, donc aucun risque de conflit de
    bind. Le comportement public (`scan()`, table de services, déduction
    de catégorie) est inchangé.
  - `HostnameResolver` (`src/modules/hostname_resolver.h/.cpp`) retire son
    écoute mDNS passive : il n'existe pas d'API publique ESP-IDF pour
    observer passivement les annonces reçues par le responder partagé.
    `begin()`/`update()`/`end()` deviennent des no-op (conservés pour
    compatibilité des appelants). Seule la résolution PTR DNS (port 53,
    unicast, sans rapport avec ce conflit) reste active.
  - `MdnsManager` (`src/modules/mdns_manager.h/.cpp`, introduit en v0.8.1)
    est supprimé — devenu inutile, plus aucun module n'ouvre de socket
    multicast applicatif.
  - Voir `docs/WARNINGS.md` (section « Socket mDNS multicast — conflit
    avec ESPmDNS ») pour le détail de la cause racine et de la correction.

---

## [0.8.1] - 2026-06-18

### Corrige

- **Conflit de bind mDNS/DNS-SD sur 224.0.0.251:5353**
  (`src/modules/mdns_manager.h/.cpp`, nouveau) : `HostnameResolver` et
  `DnsSdScanner` ouvraient chacun leur propre `WiFiUDP` et appelaient
  indépendamment `beginMulticast(224.0.0.251, 5353)`. Lorsqu'une repasse
  précise (rescan ciblé d'un équipement) déclenchait `DnsSdScanner::scan()`
  pendant qu'un scan principal gardait `HostnameResolver` actif (écoute
  passive pendant tout le sweep ARP), le second appel échouait
  (`[E][WiFiUdp.cpp:71] begin(): could not bind socket: 112`). Un nouveau
  module `MdnsManager` mutualise désormais un unique socket multicast,
  partagé par comptage de références entre les deux scanners et protégé par
  un mutex FreeRTOS contre les accès concurrents — corrige le conflit de
  bind et réduit l'empreinte mémoire (un seul `WiFiUDP` au lieu de deux).
- **LOCATION SSDP invalide pointant vers une adresse inutilisable**
  (`src/modules/ssdp_scanner.cpp`) : certains équipements UPnP mal
  configurés annoncent une URL `LOCATION` inutilisable depuis l'ESP32,
  provoquant une tentative de récupération HTTP du descripteur XML vouée à
  l'échec (`socket error ... Connection reset by peer sur 127.0.0.1`,
  `[WRN][SSDP] Pas de réponse XML de 127.0.0.1`). Ces LOCATION sont
  désormais rejetées avant tout essai de connexion, avec un avertissement
  journalisé, pour trois cas : boucle locale (`127.0.0.0/8`), adresse non
  initialisée (`0.0.0.0`) et lien-local APIPA (`169.254.0.0/16`).

---

## [0.8.0] - 2026-06-18

### Ajoute

- **Mode dégradé mémoire** (`src/modules/system_health.h/.cpp`, nouveau) :
  remplace l'ancien redémarrage automatique sur heap critique par un mode
  dégradé piloté manuellement. Sous le seuil `HEAP_CRITICAL_BYTES` (20 000
  octets libres), le mode dégradé s'active : refus des nouveaux scans, des
  rescans ciblés, des nouvelles notes, de la journalisation d'historique et
  des modifications de configuration (alias, favoris, réseaux WiFi,
  réinitialisation/restauration). L'inventaire déjà acquis reste pleinement
  consultable. Sortie du mode dégradé avec hystérésis (`HEAP_RECOVERY_MARGIN`)
  une fois la mémoire redevenue suffisante, ou redémarrage manuel via le
  nouveau bouton « Redémarrer l'appareil » (page Paramètres, cartouche « État
  système »). Nouvelles routes `GET /api/system/health` et
  `POST /api/system/restart`.
- **Bornes mémoire explicites** (`include/app_config.h`) : `MAX_HISTORY_EVENTS`
  (1000), `MAX_NOTES_PER_DEVICE` (20), `MAX_NOTE_LENGTH` (256 caractères),
  en plus de `MAX_TRACKED_DEVICES` (300) déjà existant — empêchent toute
  croissance non bornée du journal d'historique, des notes par équipement et
  de la liste d'équipements suivis (éviction LRU sous mutex).

### Corrige

- **Barre de progression du scan non affichée lors d'un scan automatique**
  (`web_src/scan.js`) : la page Équipements n'animait la barre de progression
  que si le scan avait été déclenché par un clic sur « Scanner » dans cette
  même page. Le scan automatique lancé à la connexion WiFi (`main.cpp`,
  `netScanner.startScan()`) n'était donc jamais matérialisé visuellement si
  l'utilisateur ouvrait/rechargeait la page pendant que ce scan tournait
  déjà. `fetchDevices()` démarre désormais l'animation dès qu'un scan en
  cours est détecté (`data.scanning`), quelle que soit son origine.
- **NeoPixel pouvant repasser à `Ready` pendant un incident** (`src/main.cpp`) :
  l'acquittement des nouveaux équipements (visite de `/scan`) ramenait
  toujours la LED de `NewDevice` à `Ready`, y compris en mode dégradé. La
  LED ne revient désormais à `Ready` que si aucun incident mémoire n'est en
  cours (`!systemHealth.isDegraded()`).
- **Croissance mémoire non bornée** : export CSV (`NetworkScanner::devicesToCsv()`)
  pré-alloue son tampon (`String::reserve()`) au lieu de concaténer sans
  borne ; journal d'historique et notes par équipement désormais plafonnés
  (voir ci-dessus) ; suivi du stack FreeRTOS (high-water-mark) ajouté dans
  les tâches de scan pour détecter tout risque de dépassement de pile.

### Modifie

- **`PROJECT_VERSION`** → `0.8.0` (`platformio.ini`).

---

## [0.7.4] - 2026-06-18

### Modifie

- **Largeur de page uniformisée** (`web_src/index.html`, `web_src/ota.html`,
  `web_src/wifi.html`) : les pages Accueil, OTA et Paramètres utilisent
  désormais la classe `page-scan` (max-width 960px), comme les pages
  Équipements, Historique et Topologie. Seule la page Équipements conserve
  une largeur réellement adaptable au contenu (tableau pouvant défiler
  horizontalement) ; les autres pages affichent simplement leur carte sur
  la même largeur fixe.

---

## [0.7.3] - 2026-06-18

### Ajoute

- **Filtre "Favoris uniquement" sur la page Historique** (`web_src/history.html`,
  `web_src/history.js`) : permet de n'afficher que les événements concernant
  des équipements actuellement marqués comme favoris. Le statut favori est
  récupéré depuis `/api/devices` et croisé avec le champ `mac` de chaque
  entrée d'historique (l'historique lui-même ne stocke pas le statut favori,
  qui peut changer après l'événement).

### Modifie

- **Étoile favoris éclaircie sur la page Équipements** (`web_src/styles.css`) :
  la couleur de l'étoile non favorite (`☆`) passe d'un gris foncé peu visible
  (`#475569`) à un gris clair (`#94a3b8`) pour une meilleure lisibilité.

---

## [0.7.2] - 2026-06-18

### Ajoute

- **Sauvegarde/restauration des paramètres de fonctionnement**
  (`GET /api/system/backup`, `POST /api/system/restore`,
  `src/modules/web_server.cpp`) : distincte de la sauvegarde de l'inventaire
  (`/api/backup`), cette nouvelle sauvegarde couvre les réseaux WiFi
  enregistrés (SSID + mot de passe), la luminosité NeoPixel et le nom mDNS
  (informatif uniquement — fixé à la compilation, non restaurable). Les
  réseaux WiFi restaurés sont ajoutés/mis à jour, jamais supprimés
  automatiquement.

### Modifie

- **Menu "Données ▾" réorganisé** (`web_src/scan.html`, `web_src/scan.js`) :
  un séparateur distingue désormais "Export CSV"/"Export JSON" (export de
  l'inventaire réseau, inchangé) de "Sauvegarde"/"Restauration" (nouveaux,
  paramètres de fonctionnement du projet).
- **Gestion des accents dans les exports** : l'export CSV
  (`/api/devices/export.csv`) inclut désormais un BOM UTF-8 en tête de
  fichier pour qu'Excel et les tableurs affichent correctement les
  caractères accentués ; les réponses JSON (`/api/backup`,
  `/api/system/backup`) déclarent explicitement `charset=utf-8`. La
  restauration (`/api/restore`, `/api/system/restore`) lit le fichier en
  UTF-8 côté navigateur.

---

## [0.7.1] - 2026-06-18

### Modifie

- **Menu "Données ▾" unifié sur la page Équipements** (`web_src/scan.html`,
  `web_src/scan.js`) : le menu "Exporter ▾" (CSV/JSON) et la carte
  "Sauvegarde des équipements" (alors sur la page Accueil) sont regroupés en
  un seul menu déroulant "Données ▾" — Export CSV, Export JSON, Restaurer…
  — pour éviter d'étaler les actions sur deux pages et deux menus différents.
  La carte Sauvegarde/Restauration est retirée de `web_src/index.html`.
- **Export CSV plus lisible** (`NetworkScanner::devicesToCsv()`,
  `src/modules/network_scanner.cpp`) : les dates `firstSeen`/`lastSeenAt`
  sont converties en date lisible (`AAAA-MM-JJ HH:MM:SS`) au lieu de l'epoch
  brut, et les colonnes `online`/`favorite` affichent `Yes`/`No` au lieu de
  `1`/`0`. L'export JSON (`/api/backup`) garde les epochs bruts pour rester
  réutilisable par `/api/restore`.
- **Notes et niveau de confiance dans les exports** : l'export CSV ajoute une
  colonne `notes` (notes utilisateur concaténées) et `confidence` (score de
  confiance 0-100). L'export/sauvegarde JSON (`backupToJson()`) ajoute
  également `confidence`/`confidenceLabel` (les notes y étaient déjà
  présentes).

---

## [0.7.0] - 2026-06-18

### Ajoute

- **Filtres sur la page Équipements** (`web_src/scan.html`, `web_src/scan.js`) :
  une barre de filtres (type, fabricant, favoris uniquement, en ligne
  uniquement) permet désormais de réduire la liste affichée sans relancer de
  scan. Les listes déroulantes Type/Fabricant sont construites
  automatiquement à partir des équipements actuellement connus. Filtrage
  appliqué côté client (les données complètes restent disponibles via
  `/api/devices`).
- **Export CSV de l'inventaire** (`GET /api/devices/export.csv`,
  `NetworkScanner::devicesToCsv()`) : en plus de la sauvegarde JSON complète
  déjà existante (`/api/backup`), un export CSV (une ligne par équipement :
  IP, MAC, hôte, alias, fabricant, modèle, catégorie, type, OS, services,
  ports, en ligne, favori, dates, compteur de vues) facilite l'utilisation de
  l'inventaire dans un tableur ou un autre outil. Un menu "Exporter ▾" sur la
  page Équipements propose les deux formats (CSV et JSON).
- **Page Topologie** (`GET /topology`, `web_src/topology.html/js`) : nouvelle
  page (vue simplifiée, sans représentation graphique) qui prépare le terrain
  pour la cartographie réseau prévue en roadmap (v0.4.x) — elle distingue
  pour l'instant la ou les passerelles/routeurs détectés du reste des
  équipements, à partir des données déjà collectées (`category`). Cette page
  sera étoffée avec la détection des points d'accès/répéteurs WiFi et une
  visualisation graphique des relations entre équipements.

### Modifie

- **Cartouche diagnostics déplacée vers la page Accueil** (`web_src/index.html`,
  `web_src/index.js`) : l'affichage du heap libre, de la PSRAM, de l'usage
  LittleFS et des temps moyens de scan/passe précise (`GET /api/diagnostics`)
  quitte la page Équipements — où il n'avait d'intérêt que pour le
  développement — pour rejoindre la page Accueil, sous le cartouche
  "Informations réseau", dans une nouvelle carte "Diagnostics système".

## [0.6.2] - 2026-06-18

### Modifie

- **Simplification de la NeoPixel d'état et du bouton BOOT** (`src/modules/status_led.*`,
  `src/modules/boot_button.*`) : le comportement est réduit au strict
  minimum utile. La NeoPixel n'a plus que 6 états (bleu pulsé au démarrage,
  bleu fixe une fois prête, vert clignotant pendant un scan, jaune clignotant
  tant qu'un nouvel équipement n'a pas été consulté, violet pendant le
  portail WiFi, cyan pendant une sauvegarde) — les états avertissement/erreur
  et les effets de flash en surimpression sont retirés. Le bouton BOOT n'a
  plus que deux gestes : appui court = lance un scan, maintien 3 s =
  sauvegarde immédiate. Le double appui et les rescans automatiques des
  équipements inconnus sont retirés.
- **Réglage de la luminosité dans la page Paramètres** (`web_src/wifi.html`,
  `web_src/wifi.js`) : un curseur (0-100 %) permet désormais de régler la
  luminosité de la NeoPixel directement depuis l'interface web, au lieu de
  passer uniquement par l'API (`GET`/`POST /api/led/brightness`, déjà
  existante).

## [0.6.0] - 2026-06-18

### Ajoute

- **NeoPixel d'état et gestes du bouton BOOT** (`src/modules/status_led.*`,
  `src/modules/boot_button.*`) : une NeoPixel unique (`board_config.h::NEOPIXEL_PIN`)
  affiche en continu l'état de la passerelle — bleu pulsé au démarrage, bleu
  fixe une fois prête, vert clignotant pendant un scan, jaune clignotant tant
  qu'un nouvel équipement n'a pas été consulté (page Équipements), violet
  pendant le portail WiFi de première configuration, cyan pendant une
  sauvegarde. Luminosité réglable de 0 à 100 %
  (`GET`/`POST /api/led/brightness`), persistée en NVS (`Preferences`,
  namespace `led`), 15 % par défaut au premier démarrage. Le bouton BOOT
  (`board_config.h::BUTTON_BOOT_PIN`) pilote le scanner sans passer par
  l'interface web : appui court = lance un scan, maintien 3 s = sauvegarde
  immédiate (`saveNow()`).
- **Cartouche diagnostics** (`GET /api/diagnostics`, `NetworkScanner::diagnosticsToJson()`)
  : nouvelle barre d'information sur la page Équipements affichant heap libre,
  PSRAM libre, espace LittleFS utilisé/total, et le temps moyen d'un scan
  complet / d'une passe précise — pour suivre l'impact mémoire et performance
  du scan dans la durée. Les temps moyens sont calculés à partir de compteurs
  cumulés (durée totale / nombre d'exécutions), réinitialisés au redémarrage.
- **Favoris et notes d'inventaire par équipement** (`POST /api/favorite`,
  `POST /api/notes`, `DELETE /api/notes`) : chaque équipement peut désormais
  être marqué comme favori (étoile, bouton dédié sur la page Équipements) et
  porter une liste de notes libres datées (epoch NTP), par exemple "cartouche
  d'encre changée le 12/05" ou "firmware mis à jour" — utile pour le suivi
  d'inventaire personnel. Favoris et notes sont persistés dans
  `/devices.json` (`DeviceStore::save()`/`load()`) au même titre que le reste
  de la fiche équipement, et inclus dans la sauvegarde/restauration complète
  (`/api/backup`, `/api/restore`).
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
