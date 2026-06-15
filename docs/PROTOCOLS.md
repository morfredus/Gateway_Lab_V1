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

Les équipements présents répondent. On lit leur IP et leur MAC.

**Limitation** : la table ARP de lwIP (le stack réseau de l'ESP32) ne peut stocker
que 10 entrées à la fois. C'est pourquoi le scan se fait par lots de 5 : après chaque lot,
on lit la table avant qu'elle ne soit réécrasée.

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

Le module `HostnameResolver` ouvre un socket UDP sur `224.0.0.251:5353` et **écoute
passivement** pendant le sweep ARP. Quand un device reçoit notre ARP Request,
il peut annoncer son nom mDNS dans la foulée. On le capture.

C'est passif : on ne demande rien, on capte les annonces spontanées.

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

Après le sweep ARP, on dispose d'une liste d'IPs sans hostname.
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
et comment on peut les trouver.

Appareils compatibles : box Internet (Freebox, Livebox…), NAS Synology, enceintes Sonos,
téléviseurs Samsung, Philips Hue Bridge, Chromecast, etc.

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

Lors du scan ARP, pour chaque adresse MAC trouvée, on cherche son OUI dans cette base
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

## Récapitulatif des protocoles

| Protocole | Port | Transport | Usage dans Gateway Lab |
|---|---|---|---|
| ARP | — | Ethernet L2 | Découverte des équipements (sweep) |
| mDNS | 5353 | UDP multicast 224.0.0.251 | Résolution de noms `.local` |
| DNS PTR | 53 | UDP → serveur DNS box | Résolution de noms DHCP |
| SSDP | 1900 | UDP multicast 239.255.255.250 | Découverte UPnP |
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
