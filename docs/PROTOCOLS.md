# Protocoles réseau utilisés — Gateway Lab V1

Ce document explique les protocoles réseau mis en œuvre dans le projet,
en partant des bases. Aucune connaissance préalable n'est requise.

---

## ARP — Address Resolution Protocol

### C'est quoi ?

Sur un réseau local, les équipements communiquent en réalité avec des **adresses MAC**
(identifiant physique gravé dans la carte réseau), pas avec des adresses IP.

ARP est le protocole qui fait la correspondance entre une adresse IP et une adresse MAC.

### Comment ça marche ?

Quand un équipement A veut parler à l'équipement B (192.168.1.10) :

```
A → Tout le réseau : "Qui est 192.168.1.10 ? Répondez à A8:B3:CC..."
B → A            : "C'est moi ! Mon MAC est B8:27:EB:..."
A mémorise : 192.168.1.10 = B8:27:EB:...
```

La réponse est mémorisée dans une **table ARP** (cache de correspondances IP↔MAC).

### Comment Gateway Lab l'utilise ?

Le scanner envoie des ARP Request pour **chaque adresse IP possible du réseau** :

```
192.168.1.1  → "Qui est là ?"
192.168.1.2  → "Qui est là ?"
192.168.1.3  → "Qui est là ?"
...
192.168.1.254 → "Qui est là ?"
```

Les équipements présents répondent. Lire leur IP et leur MAC.

**Limitation** : la table ARP de lwIP (le stack réseau de l'ESP32) ne peut stocker
que 10 entrées à la fois. C'est pourquoi le scan se fait par lots de 5 : après chaque lot,
lire la table avant qu'elle ne soit réécrasée.

---

## DHCP — fingerprinting passif

### C'est quoi ?

DHCP (Dynamic Host Configuration Protocol) est le protocole par lequel un
équipement obtient automatiquement une adresse IP en rejoignant le réseau.
Pour cela, il envoie un paquet **DHCPDISCOVER** (puis **DHCPREQUEST**) en
**broadcast** sur `255.255.255.255:67` — visible par tous les équipements
du même réseau local, pas seulement par le serveur DHCP (la box).

Ce paquet contient, en clair, des informations que l'équipement déclare
lui-même :

- **Option 12 (Host Name)** : le nom choisi par son système d'exploitation
  (souvent plus parlant que le hostname DNS/PTR, parfois absent côté box)
- **Option 60 (Vendor Class Identifier)** : une chaîne identifiant
  fréquemment l'OS ou le firmware du client DHCP (`MSFT 5.0` = Windows,
  `android-dhcp-13` = Android, `dhcpcd-9.4.1` = Linux, `udhcp 1.31.1` =
  Linux embarqué/IoT…)

### Comment ça marche ?

```
Un smartphone Android rejoint le WiFi :
Smartphone → Tout le réseau (255.255.255.255:67) :
  DHCPDISCOVER
    Option 12 (Host Name)            : "Galaxy-S23"
    Option 60 (Vendor Class)          : "android-dhcp-13"
    chaddr (adresse MAC du client)    : A2:3B:...
```

Gateway Lab écoute passivement ce trafic — il ne répond jamais aux
DHCPDISCOVER captés (il ne joue jamais le rôle de serveur DHCP) et n'émet
aucune requête. Un simple socket UDP, lié en écoute sur le port serveur
DHCP (67) en `INADDR_ANY`, suffit à lwIP pour délivrer ces broadcasts au
firmware au même titre qu'à n'importe quel autre équipement du réseau.

### Comment Gateway Lab l'utilise ?

`DhcpSniffer` (`src/modules/dhcp_sniffer.*`) tourne en continu, indépendamment
de tout scan, dès la connexion WiFi établie :

```
1. Socket UDP lié sur 0.0.0.0:67, non bloquant
2. A chaque paquet recu :
   a. Verifie op=BOOTREQUEST + cookie magique DHCP
   b. Extrait chaddr (MAC client), puis les options 12/53/60
   c. Ne retient que les DHCPDISCOVER/DHCPREQUEST (option 53 = 1 ou 3)
   d. Stocke hostname + vendorClass + osGuess dans une table MAC -> empreinte
      (bornee a 64 entrees, eviction de la plus ancienne au-dela)
3. NetworkScanner consulte cette table (simple lecture memoire, _enrichDevices())
   pour chaque equipement deja decouvert par ARP, et complete son hostname/os
   s'ils sont encore vides — sans jamais ecraser une source plus fiable
```

`osGuess` se limite volontairement à une petite table de signatures
Vendor Class connues et stables (Windows/Android/Linux) : la liste de
paramètres demandés (option 55), souvent utilisée en fingerprinting OS
avancé, est trop ambiguë/instable sans base de signatures externe pour
être exploitée de façon fiable ici.

**Particularité par rapport aux autres modules** : `DhcpSniffer` n'est
**jamais** déclenché par un scan (complet ou ciblé) — il n'émet aucune
requête, donc n'ajoute aucun coût réseau au scan complet ni à la passe
précise. C'est une source d'enrichissement purement passive et continue,
comparable dans son principe au mDNS passif historique (retiré en v0.8.2
pour une raison différente : conflit de socket avec le responder mDNS
d'ESP-IDF, qui ne se pose pas ici puisqu'aucun service ESP-IDF n'occupe le
port 67).

---

## mDNS — Multicast DNS

### C'est quoi ?

DNS classique = un serveur central qui traduit `google.com` → `142.250.74.46`.

mDNS = DNS sans serveur central, pour les réseaux locaux.
Chaque équipement s'annonce lui-même sur le réseau avec un nom en `.local`.

Exemples : `raspberrypi.local`, `freebox-server.local`, `gateway-lab-v1.local`.

### Comment ça marche ?

mDNS utilise l'**adresse multicast** `224.0.0.251` sur le port `5353`.
Une adresse multicast est une adresse que tout le monde peut écouter simultanément.

```
Raspberry Pi → Tout le réseau (224.0.0.251) :
  "Je m'appelle raspberrypi.local et mon IP est 192.168.1.5"
```

### Comment Gateway Lab l'utilise ?

Depuis v0.8.2, `HostnameResolver` n'écoute plus passivement
`224.0.0.251:5353` : ce port reste détenu exclusivement par le composant
mDNS d'ESP-IDF (responder `MDNS.begin()`, voir `wifi_manager.cpp`), et il
n'existe pas d'API publique permettant d'observer ses annonces reçues
(voir `docs/ARCHITECTURE.md` et `docs/WARNINGS.md`). La résolution de
hostname repose désormais uniquement sur PTR DNS (ci-dessous) ; `DnsSdScanner`
(découverte de services, voir plus bas) fournit également un hostname via
les enregistrements SRV lorsque disponible.

---

## PTR DNS — Reverse DNS

### C'est quoi ?

DNS classique : `mon-pc.local` → `192.168.1.42` (nom → IP)
PTR DNS (reverse) : `192.168.1.42` → `mon-pc` (IP → nom)

Le routeur/box DHCP enregistre généralement le nom de chaque équipement quand
il lui attribue une IP. Une requête PTR permet de récupérer ce nom.

### Comment ça marche ?

La requête PTR inverse l'adresse IP et ajoute `.in-addr.arpa` :

```
Pour trouver le nom de 192.168.1.42 :
Envoyer au DNS (la box) : "Qu'est-ce que 42.1.168.192.in-addr.arpa ?"
Réponse : "mon-pc"
```

### Comment Gateway Lab l'utilise ?

Après le sweep ARP, une liste d'IPs sans hostname est disponible.
`HostnameResolver` envoie **toutes les requêtes PTR en parallèle** au serveur DNS
de la box, puis attend les réponses pendant 500 ms.

Ce batch parallèle est crucial : 50 requêtes séquentielles × 500 ms = 25 secondes.
50 requêtes simultanées × 500 ms = 0,5 seconde.

---

## SSDP / UPnP — Simple Service Discovery Protocol

### C'est quoi ?

UPnP (Universal Plug and Play) est un ensemble de protocoles qui permettent
aux équipements d'un réseau de s'annoncer et de décrire leurs capacités automatiquement.

SSDP est la partie "découverte" de UPnP : comment un équipement annonce sa présence
et comment les trouver.

Appareils compatibles : box Internet (Freebox, Livebox…), NAS Synology, enceintes Sonos,
téléviseurs Samsung, Philips Hue Bridge, Chromecast, etc.

DIAL (DIscovery And Launch, `urn:dial-multiscreen-org:device:dial:1`) est un
profil SSDP utilisé pour le "second écran" (Netflix, YouTube…) : Amazon Fire
TV / Fire Stick l'annoncent et y exposent `manufacturer=Amazon` dans le
descripteur XML, ce qui permet de les classer en catégorie *Streaming* sans
logique dédiée à Amazon (voir `SsdpScanner::_categorize()`).

### Comment ça marche — Annonce spontanée (NOTIFY)

Quand un équipement UPnP démarre ou se connecte au réseau :

```
Freebox → Tout le réseau (239.255.255.250:1900) :
  NOTIFY * HTTP/1.1
  HOST: 239.255.255.250:1900
  NT: urn:schemas-upnp-org:device:InternetGatewayDevice:2
  LOCATION: http://192.168.1.254:49000/freebox.xml
```

Il dit : "Je suis là, et pour en savoir plus sur moi, consultez cette URL."

### Comment ça marche — Recherche active (M-SEARCH)

On peut aussi provoquer les annonces en diffusant une recherche :

```
Gateway Lab → Tout le réseau (239.255.255.250:1900) :
  M-SEARCH * HTTP/1.1
  HOST: 239.255.255.250:1900
  MAN: "ssdp:discover"
  MX: 3
  ST: ssdp:all
```

`MX: 3` = attendre jusqu'à 3 secondes avant de répondre (évite les tempêtes de réponses).
`ST: ssdp:all` = tous les équipements UPnP répondent.

### Le descripteur XML

Chaque réponse SSDP contient un champ `LOCATION` : une URL HTTP qui pointe vers
un fichier XML décrivant l'équipement en détail.

```xml
<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <device>
    <friendlyName>Freebox Server</friendlyName>
    <manufacturer>Free SAS</manufacturer>
    <modelName>Freebox Server</modelName>
    <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:2</deviceType>
    <presentationURL>http://192.168.1.254/</presentationURL>
  </device>
</root>
```

Gateway Lab fait un HTTP GET sur cette URL et parse le XML pour extraire ces informations.

### Comment Gateway Lab l'utilise ?

```
1. Envoie M-SEARCH → 239.255.255.250:1900
2. Attend les réponses (3 secondes)
3. Pour chaque réponse unique :
   a. Extrait l'IP et l'URL LOCATION
   b. HTTP GET du XML descripteur
   c. Parse : friendlyName, manufacturer, modelName, deviceType
   d. Catégorise automatiquement
   e. Appelle l'API spécifique si c'est un Hue/Synology/Freebox
```

---

## DNS-SD — DNS Service Discovery

### C'est quoi ?

DNS-SD (RFC 6763) est construit **au-dessus de mDNS**. Alors que mDNS résout des noms
(`raspberrypi.local` → IP), DNS-SD répond à la question :
**"Qui sur le réseau propose tel service ?"**

Par exemple : "Qui offre AirPlay ? Qui a un serveur HTTP ? Qui est HomeKit ?"

C'est le protocole utilisé par Bonjour (Apple), avahi (Linux), et la plupart des devices
connectés modernes.

### Comment ça marche ?

DNS-SD utilise un type de record DNS particulier : `PTR` (pointeur).

Pour trouver tous les appareils offrant SSH :

```
Requête : _ssh._tcp.local  (type PTR)
Réponse : "MonNAS._ssh._tcp.local"
          "raspberrypi._ssh._tcp.local"
```

Pour chaque instance trouvée, des records additionnels précisent :

```
SRV : MonNAS._ssh._tcp.local → port 22, hostname "nas.local"
TXT : MonNAS._ssh._tcp.local → ["md=DS224+", "fn=Mon NAS Synology"]
A   : nas.local → 192.168.1.10
```

Tout cela arrive souvent dans un **seul paquet mDNS** (section "Additional").

### Exemples de services courants

| Type DNS-SD | Ce qu'il identifie |
|---|---|
| `_http._tcp` | Tout appareil avec une interface web |
| `_ssh._tcp` | Serveurs SSH (NAS, Raspberry Pi, Mac, Linux) |
| `_smb._tcp` | Partages réseau Windows/Samba |
| `_airplay._tcp` | Apple TV, HomePod, AirPlay 2 |
| `_raop._tcp` | AirPlay audio (HomePod mini, Apple TV) |
| `_homekit._tcp` | Tous les accessoires HomeKit |
| `_googlecast._tcp` | Chromecast, Google Home, Android TV |
| `_sonos._tcp` | Enceintes Sonos |
| `_spotify-connect._tcp` | Enceintes Spotify Connect |
| `_hue._tcp` | Philips Hue Bridge |
| `_esphome._tcp` | Devices ESPHome (domotique DIY) |
| `_home-assistant._tcp` | Home Assistant |
| `_mqtt._tcp` | Brokers MQTT |
| `_ipp._tcp` | Imprimantes (IPP) |

### Comment Gateway Lab l'utilise ?

```
1. Un seul paquet mDNS avec N questions PTR (22 services)
         ↓ → 224.0.0.251:5353
2. Écoute pendant 4 secondes
3. Pour chaque réponse reçue :
   a. PTR → nom de l'instance + type de service
   b. SRV → port + hostname
   c. TXT → champs md=, fn= → modèle du device
   d. A   → hostname.local → IP
4. Résolution IP : A record → hostname map → fallback nom instance
5. Résultat : IP → liste de services labels ["HTTP","SSH","SMB"]
```

### Différence avec SSDP

| | SSDP/UPnP | DNS-SD |
|---|---|---|
| Protocole | HTTP over UDP multicast | DNS over UDP multicast |
| Résultat obtenu | 1 identité par device (XML) | N services par device |
| Métadonnées | friendlyName, manufacturer, modelName | TXT records (md=, fn=…) |
| Devices compatibles | Box, NAS, TV, Smart Home (UPnP) | Apple, Google, Sonos, ESPHome, Linux… |
| Complémentaires ? | ✅ Oui — sources différentes |

---

## OUI — Organizationally Unique Identifier

### C'est quoi ?

Une adresse MAC est composée de 6 octets : `B8:27:EB:AA:BB:CC`

Les 3 premiers octets (`B8:27:EB`) sont attribués par l'IEEE à un fabricant.
`B8:27:EB` est réservé à la Raspberry Pi Foundation.

Ce préfixe de 3 octets s'appelle l'OUI.

### Comment Gateway Lab l'utilise ?

Le fichier `data/oui.json` contient une base de 151 OUI courants :

```json
{"oui": "B8:27:EB", "manufacturer": "Raspberry Pi Foundation", "category": "SBC"},
{"oui": "DC:A6:32", "manufacturer": "Raspberry Pi Foundation", "category": "SBC"},
{"oui": "DC:BF:E9", "manufacturer": "Espressif Inc.",          "category": "IoT"},
...
```

Lors du scan ARP, pour chaque adresse MAC trouvée, chercher son OUI dans cette base
pour identifier le fabricant et la catégorie, sans aucune requête réseau.

---

## APIs spécifiques (HTTP REST)

Certains équipements exposent une API HTTP locale permettant d'obtenir des informations
détaillées sans authentification.

### Philips Hue Bridge — `/api/config`

```
GET http://192.168.1.x/api/config

Réponse :
{
  "name": "Mon Bridge Hue",
  "modelid": "BSB002",
  "swversion": "1.60.1.60010",
  "apiversion": "1.60.0"
}
```

Pas de token requis. `BSB002` = Bridge v2 (carré blanc).

### Synology DSM — `/webapi/query.cgi`

```
GET http://192.168.1.x:5000/webapi/query.cgi?api=SYNO.API.Info&version=1&method=query

Réponse :
{
  "success": true,
  "data": { ... }
}
```

Confirme que c'est bien un DSM. Le modèle exact est récupéré depuis le XML UPnP.

### Freebox — `/api_version`

```
GET http://192.168.1.x/api_version

Réponse :
{
  "uid": "xxxx",
  "device_name": "Freebox Ultra",
  "api_version": "12.0",
  "api_base_url": "/api/",
  "device_type": "FreeboxV9/8.3.6",
  "firmware_version": "4.8.3"
}
```

`device_type` contient la génération : V9 = Ultra, V8 = Pop, V7 = Révolution, etc.
Accessible sans aucune authentification.

---

## NetBIOS Node Status

### C'est quoi ?

NetBIOS est un protocole historique de Windows pour le nommage et le partage
réseau (toujours actif aujourd'hui via Samba sur Linux/NAS). Une requête
**Node Status** (UDP 137) permet d'obtenir le nom NetBIOS d'un poste sans
authentification.

### Comment Gateway Lab l'utilise ?

```
Gateway Lab → IP cible:137 :
  NBSTAT (requête Node Status)
PC Windows → Gateway Lab :
  Liste des noms NetBIOS enregistrés (nom de machine, groupe de travail…)
```

Utilisé en repli quand mDNS/PTR n'ont donné aucun nom — efficace sur les PC
Windows et les NAS Samba qui n'annoncent pas toujours en mDNS.

---

## Scan de ports TCP + sondage d'API IoT

### C'est quoi ?

Un scan de ports vérifie quels services réseau sont actifs sur un équipement
en tentant une connexion TCP sur une liste de ports courants (HTTP, SSH,
SMB, RTSP, MQTT, RDP, IPP…). Un port qui accepte la connexion est "ouvert".

### Comment Gateway Lab l'utilise ?

`PortScanner` sonde 14 ports via des sockets non bloquants + `select()` (par
lots de 8, contrainte lwIP). Sur le premier port HTTP ouvert détecté, le
scanner lit l'en-tête `Server:` (banner grabbing) puis tente des requêtes
ciblées vers des API IoT non authentifiées connues :

| Équipement | Requête | Champs récupérés |
|---|---|---|
| Shelly | `GET /shelly` | type, firmware, MAC |
| Tasmota | `GET /cm?cmnd=Status%200` | module, version |
| FritzBox | `GET /jason_boxinfo.xml` | nom, version |

---

## SNMP — Simple Network Management Protocol

### C'est quoi ?

SNMP (UDP 161) est le protocole standard d'administration des équipements
réseau professionnels (switches, routeurs, imprimantes, NAS). En lecture
publique (community `public`), l'OID `1.3.6.1.2.1.1.1.0` (`sysDescr`) renvoie
une description texte libre de l'équipement, généralement explicite
(fabricant, modèle, version firmware).

### Comment Gateway Lab l'utilise ?

Implémenté entièrement en interne (encodage/décodage ASN.1 BER manuel d'une
requête `GetRequest` SNMPv1), en requête unicast directe sur l'IP visée,
uniquement lors de la **passe précise approfondie** (profil Imprimante ou
Inconnu, une fois un service exploitable confirmé par le scan de ports) —
jamais pendant le scan complet ni en diffusion réseau :

```
Gateway Lab → IP cible:161 :
  GetRequest community="public" OID=1.3.6.1.2.1.1.1.0
Equipement  → Gateway Lab :
  "HP LaserJet Pro M404dn ; FW 2406..." (texte libre)
```

Le texte est analysé par des heuristiques de mots-clés pour en déduire le
fabricant (HP, Cisco, Synology, Ubiquiti…) quand l'OUI MAC est ambigu.

---

## MQTT — broker (passe précise approfondie)

### C'est quoi ?

MQTT est un protocole de messagerie publish/subscribe très utilisé en
domotique (Home Assistant, Zigbee2MQTT, Tasmota, ESPHome…). Un **broker**
centralise les messages : les appareils publient sur des topics, d'autres
s'y abonnent. Le broker écoute en général sur le port TCP 1883.

Le scan de ports (Phase complète et passe précise) détecte déjà le port
1883 ouvert (libellé `MQTT`). La passe précise approfondie va plus loin en
s'y connectant brièvement comme client.

### Comment ça marche ?

```
Gateway Lab → IP cible:1883 :
  CONNECT (anonyme, client id "GatewayLab-<millis>")
Broker     → Gateway Lab :
  CONNACK (code 0 = accepté sans authentification, sinon refusé)

Si accepté :
Gateway Lab → Broker :
  SUBSCRIBE "$SYS/broker/version", "$SYS/broker/clients/connected"
Broker     → Gateway Lab :
  PUBLISH "$SYS/broker/version"           → "mosquitto version 2.0.18"
  PUBLISH "$SYS/broker/clients/connected" → "4"
```

`$SYS/#` est l'espace de topics standard que la plupart des brokers
(Mosquitto en tête) publient sur eux-mêmes. Gateway Lab se limite
volontairement à ces deux topics : il n'interroge jamais les topics
applicatifs des appareils (`#`), qui relèveraient de l'inventaire des
données du foyer plutôt que de l'identification du broker.

### Comment Gateway Lab l'utilise ?

`MqttScanner` (`src/modules/mqtt_scanner.*`) est appelé uniquement depuis
`_runRescan(ip, deep)`, lors de la passe précise approfondie, et seulement
si le profil déduit est `SmartHome` ou `Unknown` **et** que le port 1883 a
été trouvé ouvert par le scan de ports ciblé. Jamais de diffusion
multicast, jamais de scan systématique — une seule connexion TCP unicast
vers l'IP visée. Le résultat (version du broker, nombre de clients
connectés) enrichit le modèle et la catégorie (`Smart Hub`) de
l'équipement, avec la source `MQTT`.

---

## WS-Discovery (ONVIF)

### C'est quoi ?

WS-Discovery est le protocole de découverte utilisé par la quasi-totalité
des caméras IP et imprimantes compatibles **ONVIF**, indépendamment de SSDP.
Un appareil répond à une requête **Probe** SOAP par un **ProbeMatch**
contenant ses types (rôle) et ses adresses de service.

### Comment Gateway Lab l'utilise ?

`WsDiscoveryScanner` (`src/modules/ws_discovery_scanner.*`) implémente le
probe SOAP multicast, mais **n'est plus invoqué depuis la v0.9.1** : c'est
un protocole de diffusion (toujours envoyé à `239.255.255.250:3702`, jamais
restreint à une IP unique), incompatible avec le principe de la passe
précise qui n'interroge que l'équipement visé. Le module reste dans le code
en vue d'une éventuelle réintégration au scan complet (cf. `ROADMAP.md`),
mais n'est actuellement appelé par aucune route ni aucun scan.

```
Gateway Lab → Tout le réseau (239.255.255.250:3702) :
  SOAP <d:Probe/>
Caméra ONVIF → Gateway Lab :
  ProbeMatch — Types: "dn:NetworkVideoTransmitter", XAddrs: "http://192.168.1.x:..."
```

---

## API HTTP propriétaires des appareils multimédia courants

Certains appareils multimédia grand public exposent une API HTTP locale sur
un port fixe non couvert par le scan de ports standard. Lors de la passe
précise approfondie (profil Streaming/Domotique, une fois un service
exploitable confirmé), Gateway Lab les interroge directement en requête
unicast sur l'IP visée :

| Appareil | Requête | Champs récupérés |
|---|---|---|
| Google Cast / Chromecast | `GET :8008/setup/eureka_info` | nom, modèle |
| Sonos | `GET :1400/xml/device_description.xml` | modèle, nom de pièce |
| Roku | `GET :8060/query/device-info` | modèle, nom convivial |
| Samsung Smart TV (Tizen) | `GET :8001/api/v2/` | nom, modèle |

Ces API ne nécessitent aucune authentification et renvoient le modèle exact,
bien plus fiable que l'OUI MAC seul pour ces marques (Apple, Samsung…
justement signalées comme ambiguës en détection OUI).

---

## Matter (DNS-SD)

### C'est quoi ?

Matter est le standard d'interopérabilité IoT (Google, Apple, Amazon,
Samsung…) qui s'annonce, comme les autres services Apple/Google, via DNS-SD.
Un appareil Matter commissionable (pas encore appairé) s'annonce via le
service `_matterc._udp` ; un appareil déjà appairé via `_matter._tcp`.

### Comment Gateway Lab l'utilise ?

`DnsSdScanner` interroge désormais ces deux types de service au même titre
que `_googlecast._tcp` ou `_homekit._tcp` — aucune requête supplémentaire,
juste deux entrées de plus dans la table des services interrogés.

---

## Récapitulatif des protocoles

| Protocole | Port | Transport | Usage dans Gateway Lab |
|---|---|---|---|
| ARP | — | Ethernet L2 | Découverte des équipements (sweep) |
| ICMP | — | IP | Repli si l'ARP ne répond pas (TTL → OS) |
| DHCP | 67 | UDP broadcast 255.255.255.255 | Fingerprinting passif (hostname/OS) — écoute continue, jamais de requête |
| mDNS | 5353 | UDP multicast 224.0.0.251 | Résolution de noms `.local` |
| DNS-SD | 5353 | UDP multicast 224.0.0.251 | Services (`_matter._tcp`, `_matterc._udp`, `_googlecast._tcp`…) |
| DNS PTR | 53 | UDP → serveur DNS box | Résolution de noms DHCP |
| SSDP | 1900 | UDP multicast 239.255.255.250 | Découverte UPnP |
| WS-Discovery | 3702 | UDP multicast 239.255.255.250 | Découverte ONVIF — non invoqué actuellement (cf. ci-dessus) |
| NetBIOS | 137 | UDP | Node Status — nom de machine Windows/Samba |
| SNMP | 161 | UDP | `sysDescr` (fabricant/modèle) — passe précise approfondie, unicast |
| TCP ports (scan) | 21/22/23/80/443/445/554/1883/3389/5000/8080/8123/8443/9100 | TCP | Bannières + API IoT (Shelly, Tasmota, FritzBox, Synology, Hue) |
| TCP ports (passe précise) | 22/53/80/135/139/443/445/515/554/631/8080/8443/9100/5000 | TCP | `kRescanTargetPorts` — scan ciblé d'une seule IP, passe précise approfondie |
| API Cast | 8008 | TCP | `/setup/eureka_info` — passe précise approfondie, unicast |
| API Sonos | 1400 | TCP | `/xml/device_description.xml` — passe précise approfondie, unicast |
| API Roku | 8060 | TCP | `/query/device-info` — passe précise approfondie, unicast |
| API Samsung TV | 8001 | TCP | `/api/v2/` — passe précise approfondie, unicast |
| MQTT (broker) | 1883 | TCP | CONNECT + `$SYS/broker/version`/`clients/connected` — passe précise approfondie, unicast |
| HTTP | 80/443/49000/… | TCP | Descripteurs XML + APIs spécifiques |
| ArduinoOTA | 3232 | UDP | Mise à jour firmware réseau |
| HTTP (OTA web) | 80 | TCP | Mise à jour firmware navigateur |
| mDNS (service) | 5353 | UDP | Annonce `gateway-lab-v1.local` |

---

## Adresses multicast importantes

| Adresse | Usage |
|---|---|
| `224.0.0.251` | mDNS (Bonjour, Avahi) |
| `239.255.255.250` | SSDP / UPnP |
| `224.0.0.22` | IGMP (gestion des groupes multicast) |
| `255.255.255.255` | Broadcast général |
