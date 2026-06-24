# Roadmap — Gateway Lab V1

Fonctionnalités planifiées par ordre de priorité décroissante.

---

## En cours / Prochaine version

* Authentification du portail de configuration WiFi et de l'API `/api/wifi`
* Reconnexion automatique vers le portail si tous les réseaux enregistrés
  échouent durablement (actuellement uniquement au tout premier démarrage)
* Détection des points d'accès / répéteurs WiFi et visualisation graphique
  de la topologie réseau (la page `/topology` ajoutée en v0.7.0 n'en est que
  la première étape : liste texte passerelle/équipements)

---

## Roadmap produit

### v0.1.x — Inventaire enrichi (terminé en v0.2.0)

* ~~Historique persistant des équipements~~
* ~~Première apparition / dernière apparition~~
* ~~Compteur de détections~~
* ~~Équipements favoris~~ (terminé en v0.6.0)
* ~~Notes utilisateur sur un équipement~~ (alias + notes datées, v0.2.0 et v0.6.0)
* ~~Export JSON complet~~ (sauvegarde/restauration)
* ~~Export CSV~~ (terminé en v0.7.0)
* ~~Filtres sur la page Équipements (type, fabricant, favoris)~~ (terminé en v0.7.0)

### v0.2.x — Découverte avancée
* Fingerprinting TCP/IP stack avancé (TTL + TCP options + window size)
* ~~SNMP v1/v2c (community "public") pour équipements réseau (port UDP 161)~~ (terminé en v0.6.0, sondage `sysDescr` lors de la passe précise)

### v0.3.x — Configuration WiFi sans recompilation (terminé en v0.3.0)

* ~~Portail de configuration WiFi (point d'accès + page captive)~~
* ~~Persistance NVS multi-réseaux (`Preferences`)~~
* ~~Page Paramètres → Réseau WiFi (liste, ajout, suppression)~~
* Authentification du portail / de l'API `/api/wifi`
* Tado (thermostats)
* X-Sense (capteurs)
* Détection des caméras Xiaomi
* Intégrations d'objets connectés locaux supplémentaires

### v0.4.x — Topologie réseau

* ~~Page dédiée et détection automatique de la passerelle~~ (terrain préparé
  en v0.7.0 : page `/topology`, vue texte passerelle/équipements à partir des
  données déjà collectées)
* Détection des points d'accès / répéteurs WiFi
* Identification des switches connus
* Cartographie logique du réseau
* Relations entre équipements (qui est connecté à quel répéteur/AP)
* Visualisation graphique du réseau (graphe interactif)

### v0.5.x — Connectivité

* MQTT
* Webhooks
* Événements réseau temps réel
* API d'intégration externe

### v0.6.x — Détection avancée (terminé en v0.6.0)

* ~~Sous-catégorie (`type`) au sein d'une catégorie~~
* ~~Score de confiance unique et plus prudent (minimum des signaux), détail par champ~~
* ~~Passe précise asynchrone avec suivi de progression affiché sous la ligne de l'équipement~~
* ~~Sondage SNMP `sysDescr`~~
* ~~Découverte WS-Discovery / ONVIF (caméras, imprimantes, NAS)~~
* ~~API HTTP propriétaires des appareils multimédia courants (Cast, Sonos, Roku, Samsung)~~
* ~~Service DNS-SD Matter (`_matterc._udp`)~~

### v0.7.x — Matériel

* Écran OLED
* Analyse Bluetooth Low Energy
* Découverte des équipements BLE
* Interface locale embarquée

### v1.0.0 — Gateway réseau complète

* ~~Surveillance continue du réseau et score de stabilité~~ ✅ v1.0.0
* Cartographie réseau
* Intégrations domotiques
* Interface web unifiée (tableau de bord dédié à la surveillance continue)

---

## ✅ Réalisé

| Version | Fonctionnalité                                                                                                                                                     |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| v0.0.1  | Structure PlatformIO, board_config.h, app_config.h, secrets_example.h, outils Python                                                                               |
| v0.0.2  | WiFiMulti, ArduinoOTA, WebServer, mDNS, HTML PROGMEM, pipeline de génération                                                                                       |
| v0.0.3  | Architecture modulaire, NetworkScanner FreeRTOS, sweep ARP, lookup OUI                                                                                             |
| v0.0.4  | Page Équipements dédiée, navigation multi-pages, struct NetworkDevice, infrastructure hostname                                                                     |
| v0.0.5  | Base OUI externalisée, catégories d'équipements, pipeline unifié, documentation des limitations                                                                    |
| v0.0.6  | Corrections de robustesse, reconnexion WiFi, mDNS, OTA, JSON sécurisé, modules idempotents                                                                         |
| v0.0.7  | Résolution des noms d'hôtes (mDNS + PTR), détection des box FAI, modèle/source/catégorie, ESP32 visible dans sa propre liste, amélioration complète de l'interface |
| v0.0.8  | Scanner SSDP/UPnP complet, parsing XML robuste, catégorisation automatique, APIs Hue Bridge / Synology DSM / Freebox, nouveaux badges source UI |
| v0.0.9  | Scanner DNS-SD (RFC 6763) — 22 types de services, badges services HTTP/SSH/AirPlay/Cast/Sonos/HomeKit…, champ services dans NetworkDevice et API |
| v0.1.0  | Persistance LittleFS, statistiques online/offline, UI barre de statistiques et colonne statut |
| v0.1.1  | Scanner TCP 14 ports (sockets non-bloquants), banner HTTP, TTL → OS, badges ports TCP colorés dans l'UI |
| v0.1.2  | Scanner NetBIOS (UDP 137), enrichissement par patterns hostname, bannières SSH/FTP, détection API IoT (Shelly/Tasmota/FritzBox), DNS-SD étendu à 31 types de services, badge source NetBIOS |
| v0.2.0  | Historique des équipements (NTP, firstSeen/lastSeen/seenCount), alias utilisateur, classification intelligente multi-signaux, vue chronologique (/history), détection des changements, sauvegarde/restauration JSON |
| v0.2.1  | Niveau de confiance de l'identification (score 0-100%, badge couleur, libellé de source au survol) |
| v0.2.2  | Séparation HTML/CSS/JS dans `web_src/` (scripts extraits dans `*.js`, minifiés et injectés inline par `minify_web.py`), correction du lien Historique manquant sur la page OTA |
| v0.3.0  | Portail de configuration WiFi (point d'accès + page captive), persistance NVS multi-réseaux, page Paramètres → Réseau WiFi (`/wifi`), API `/api/wifi` |
| v0.4.0  | Détection des adresses MAC privées/aléatoires (catégorie `Mobile/Aléatoire`) |
| v0.5.0  | Réinitialisation de l'inventaire avec options de conservation (alias, fabricant), réinterrogation ciblée d'un équipement sans scan complet, filtrage et effacement (avec sauvegarde) de l'historique |
| v0.6.0  | Sous-catégorie (`type`), score de confiance unique et plus prudent, passe précise asynchrone avec barre de progression sous la ligne de l'équipement, sondage SNMP, découverte WS-Discovery/ONVIF, API appareils multimédia (Cast/Sonos/Roku/Samsung), service DNS-SD Matter, amélioration du contraste des textes/badges en thème sombre |
| v0.6.2  | Simplification de la NeoPixel d'état et des gestes du bouton BOOT, réglage de la luminosité NeoPixel depuis la page Paramètres |
| v0.7.0  | Filtres sur la page Équipements (type, fabricant, favoris, en ligne), export CSV de l'inventaire (`/api/devices/export.csv`), cartouche diagnostics déplacée de la page Équipements vers la page Accueil, page Topologie (`/topology`) en préparation du futur v0.4.x |
| v0.7.1  | Menu "Données ▾" unifié (export CSV/JSON + restauration) sur la page Équipements, export CSV avec dates lisibles et colonnes Yes/No, ajout des notes et du niveau de confiance dans les exports CSV et JSON |
| v0.7.2  | Sauvegarde/restauration des paramètres de fonctionnement (réseaux WiFi, luminosité NeoPixel) distincte de l'inventaire, menu "Données ▾" réorganisé (Export CSV/JSON puis Sauvegarde/Restauration), BOM UTF-8 et charset explicite pour la gestion correcte des accents dans les exports |
| v0.7.3  | Filtre "Favoris uniquement" sur la page Historique, éclaircissement de l'étoile favoris non active sur la page Équipements pour une meilleure lisibilité |
| v0.7.4  | Largeur de page uniformisée (960px) entre Accueil, OTA, Paramètres, Historique et Topologie ; la page Équipements conservait alors une largeur adaptable au contenu du tableau (corrigé en v0.8.4) |
| v0.8.0  | Mode dégradé mémoire (refus scans/rescans/notes/historique/config sous `HEAP_CRITICAL_BYTES`, redémarrage manuel uniquement), bornes mémoire explicites (historique, notes, équipements suivis), correction de la barre de progression du scan automatique au démarrage, correction de la NeoPixel ne devant pas revenir à l'état normal pendant un incident mémoire |
| v0.8.1  | Socket mDNS multicast (224.0.0.251:5353) mutualisé entre `HostnameResolver` et `DnsSdScanner` via le nouveau module `MdnsManager` (corrige un conflit de bind lors d'un rescan ciblé concurrent au scan principal, réduit l'empreinte mémoire), rejet des LOCATION SSDP invalides pointant vers la boucle locale (127.0.0.0/8) |
| v0.8.2  | Correction de la cause racine du conflit mDNS (le composant mDNS d'ESP-IDF garde 224.0.0.251:5353 exclusivement pour son responder, non pris en compte par `MdnsManager` en v0.8.1) : `DnsSdScanner` réécrit pour interroger directement le composant mDNS d'ESP-IDF (`mdns_query_ptr()`) sans socket dédié, retrait de l'écoute mDNS passive de `HostnameResolver` (aucune API ESP-IDF équivalente), suppression de `MdnsManager` devenu inutile |
| v0.8.3  | Correction du scan DNS-SD systématiquement vide (`DnsSdScanner`) : plancher de fenêtre d'attente par type de service relevé de 100 ms à 300 ms (RFC 6762 — délai aléatoire de réponse 20-120 ms sur les enregistrements partagés), journalisation des échecs de requête `mdns_query_ptr()`, valeur par défaut/appelants ajustés à 9000 ms ; reformulation à l'infinitif des commentaires et de la documentation utilisant des tournures "on fait" |
| v0.8.4  | Largeur de la page Équipements rendue strictement identique aux autres pages (960px, plus d'exception « largeur adaptable »), correction de la barre de défilement horizontale causée par les noms d'hôte longs (colonne `.name-cell` limitée à deux lignes avec coupure de mot, infobulle conservant le nom complet) |
| v0.8.5  | Avertissement sur la page Équipements précisant que le scan de départ/global n'est pas agressif : certaines informations avancées nécessitent un scan ciblé de l'équipement pour interroger des services spécifiques ou des API propriétaires |
| v0.8.6  | Ajout de la propriété standard `line-clamp` en complément de `-webkit-line-clamp` sur `.name-cell`, pour une meilleure compatibilité navigateur |
| v0.8.7  | Page Paramètres restructurée et renommée Système (réseau, LED, mise à jour du firmware, état système), suppression de la page OTA dédiée (fonctionnalité intégrée), nouveau bloc de navigation partagé `web_src/menu.html` inliné sur toutes les pages pour simplifier la maintenance |
| v0.8.8  | Correction de la barre de progression de mise à jour firmware qui disparaissait sans indication après le transfert ; étapes affichées (transfert, vérification, redémarrage) et redirection vers l'Accueil une fois le redémarrage terminé |
| v0.8.9  | Nom "humain" du matériel (déduit du modèle/fabricant/catégorie) affiché au-dessus du hostname brut sur la page Équipements, repris dans les exports CSV/JSON (`hostnameDisplay`) |
| v0.8.10 | Mise à jour de l'ensemble de la documentation utilisateur (README, ROADMAP, docs/) |
| v0.9.0  | Scan précis en deux passes (rapide / approfondi), orienté profil d'équipement déduit (Computer, NAS, Printer, Mobile, Streaming, SmartHome, Network, IoT, Unknown) ; journal d'enrichissement affiché en fin de passe |
| v0.9.1  | Correction de la passe précise : suppression de SSDP/DNS-SD/WS-Discovery (protocoles multicast non ciblables) du scan approfondi, qui interroge désormais uniquement l'IP visée (scan de ports dédié `kRescanTargetPorts`, arrêt immédiat si aucun service exploitable, modules ciblés selon le profil déduit des ports) ; fingerprint HTTP enrichi (`<title>`, API Synology/Hue) |
| v0.9.2  | Correction de l'export CSV (`/api/devices/export.csv`) : la colonne hostname utilisait une chaîne multi-lignes (UI), ce qui répartissait les informations d'un équipement sur plusieurs lignes physiques ; `csvField()` aplatit désormais systématiquement tout retour à la ligne en espace, garantissant un équipement = une ligne |
| v1.0.0  | Surveillance continue du réseau et score de stabilité : sweep ARP léger périodique (`serviceMonitor()`, 1-60 min, persisté NVS), identification automatique différée des nouveaux équipements (file rapide/approfondie), compteurs de présence/absence/reconnexion, score de stabilité 0-100% pour les équipements fixes, classification mobile/fixe automatique avec override manuel (`POST /api/mobility`), nouveaux évènements d'historique (`reconnected`, `disappeared`, `identification_improved`, `mobile_left`, `mobile_returned`), tableau de bord réseau (`GET /api/network/health`) |
| v1.0.0  | Surveillance continue du réseau (sweep ARP léger configurable 1-60 min, sans tâche FreeRTOS dédiée) et score de stabilité par équipement (0-100 %, mobiles exclus) ; classification mobile/fixe automatique + override manuel (`setMobility`) ; gestion des absences mobiles (`mobile_left`/`mobile_returned`) ; file de scans différés ; nouvel indicateur de santé réseau (`/api/network/health`) et endpoints `/api/mobility`, `/api/monitor` |
| v1.0.1 (Patch 1) | Activation/désactivation de la surveillance automatique du réseau et intervalle configurable de 5 min à 1 h depuis la page Système (`GET`/`POST /api/monitor` étendus au champ `enabled`), réglage inclus dans la sauvegarde des paramètres de fonctionnement (`monitorEnabled`/`monitorIntervalMinutes`) ; déplacement de la carte Sauvegarde/Restauration des paramètres vers la page Système ; page Équipements recentrée sur Export CSV / Export JSON uniquement |
| v1.0.2 (Patch 2) | Correction : la surveillance continue mettait automatiquement en file des passes rapides/approfondies (nouvel équipement, changement de champ, confiance faible), draînées à chaque itération de `loop()` indépendamment de l'intervalle configuré — la surveillance se limite désormais strictement à la détection de présence ARP, tout scan approfondi restant à l'initiative explicite de l'utilisateur |
| v1.0.3 (Patch 3) | Correction du scan complet qui semblait se relancer en boucle au démarrage (tick de surveillance immédiatement "dû" dès la fin du scan initial, faute d'horodatage lors d'un tick sauté) ; correction de la boucle infinie de passes précises sur un même petit groupe d'équipements (un scan rapide se remettait lui-même en file indéfiniment dès que la confiance restait sous 35 %, seuil qu'il ne peut volontairement jamais dépasser) |
| v1.0.4 (Patch 4) | Correction du filtrage de la page Historique : les 5 types d'évènements introduits par la surveillance continue (v1.0.0 — `reconnected`, `mobile_returned`, `disappeared`, `mobile_left`, `identification_improved`) n'avaient pas de case de filtre correspondante et étaient toujours exclus de l'affichage, ne laissant visibles par défaut que les évènements `changed` ; chaque type est désormais rattaché à l'une des 4 catégories de filtre existantes |
| v1.0.5 (Patch 5) | Correction du filtre "Favoris uniquement" de la page Historique, systématiquement vide : `history.js` n'indexait les favoris que par adresse MAC, sans repli sur l'IP comme partout ailleurs dans le projet ; le filtre vérifie désormais la correspondance par MAC ou par IP |
| v1.0.6 (Patch 6) | Correction de la véritable cause du filtre "Favoris uniquement" toujours vide après le Patch 5 : `loadFavorites()` lisait la réponse de `GET /api/devices` comme un tableau brut alors que cet endpoint renvoie `{ scanning, stats, devices }` ; lit désormais `data.devices` comme `scan.js` |
| v1.0.7 (Patch 7) | Journal de redémarrage temporaire pour le débogage sans moniteur série : buffer circulaire des derniers logs en RAM `RTC_NOINIT_ATTR`, persisté avec la raison du reset (`esp_reset_reason`) au boot suivant (`/bootlog.json`, 10 derniers démarrages), page `/debug` et API `GET`/`DELETE /api/bootlog` ; conçu pour un retrait facile (voir `docs/DEVELOPMENT.md`) |
| v1.0.8 (Patch 8) | Extension du journal de redémarrage : compteurs `boot_count`/`crash_count` persistés en NVS, température interne, dernier état connu avant reset (heap/bloc libre/uptime/WiFi/dernière tâche via `setLastTask()`), instantané périodique `RuntimeStats` (toutes les 30 s, incluant équipements connus/pages servies/appels API), lignes de log au format JSON enrichi (heap + bloc libre par ligne), page `/debug` mise à jour pour afficher toutes ces données |
| v1.1.1 (Patch 9) | Correction de la page Historique : les absences courtes (<30 min) d'un équipement mobile ne journalisaient aucune déconnexion, faisant apparaître des chaînes de "Reconnecté" sans cause visible — nouvel évènement discret `offline_brief`, symétrique au `reconnected` qui suit ; côté UI, les reconnexions consécutives sans déconnexion explicite entre elles sont désormais regroupées en une seule entrée "Connexion instable détectée" plutôt que d'être empilées |
