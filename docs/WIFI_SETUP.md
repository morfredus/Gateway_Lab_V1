# Configuration WiFi — Gateway Lab

Ce guide explique comment connecter Gateway Lab à votre réseau WiFi
**sans installer aucun logiciel de développement**, simplement avec un
téléphone ou un ordinateur et un navigateur web.

Il s'adresse à toute personne qui a reçu un ESP32 déjà flashé avec
`gateway-lab.bin` et qui veut le mettre en service chez elle.

---

## Pourquoi ce guide existe

Avant la version 0.3.0, le WiFi de Gateway Lab était codé en dur dans le
firmware (`include/secrets.h`). Pour changer de réseau, il fallait :

1. installer PlatformIO ;
2. modifier le code source ;
3. recompiler ;
4. reflasher la carte par USB.

Depuis la **v0.3.0**, ce n'est plus nécessaire : la carte propose un
**portail de configuration web**, comme une box internet ou une imprimante
connectée.

---

## Comment ça fonctionne — vue d'ensemble

À chaque démarrage, l'ESP32 applique les règles suivantes, dans cet ordre :

```
1. Un réseau est-il déjà enregistré dans sa mémoire (NVS) ?
   → Oui : il s'y connecte automatiquement. Rien à faire.

2. Sinon, un réseau de développement est-il défini dans secrets.h ?
   → Oui (cas des développeurs uniquement) : il s'y connecte.

3. Sinon : il active son propre point d'accès WiFi et affiche
   un portail de configuration pour vous laisser choisir votre réseau.
```

La mémoire NVS (*Non-Volatile Storage*) est une petite zone de stockage à
l'intérieur de la puce ESP32 qui **survit aux redémarrages et aux coupures
de courant** — comme le disque dur d'un ordinateur, mais beaucoup plus petit.

---

## Première mise en service — étape par étape

### Étape 1 — Alimenter la carte

Branchez l'ESP32 sur une alimentation USB (chargeur de téléphone, port USB
d'un ordinateur, etc.). Aucune connexion réseau n'est nécessaire pour cette
étape.

### Étape 2 — Se connecter au point d'accès de configuration

Sur votre téléphone ou ordinateur, ouvrez les réglages WiFi et cherchez un
réseau nommé :

```
GatewayLab-Setup
```

Connectez-vous à ce réseau (il n'a pas de mot de passe). Sur la plupart des
téléphones, une fenêtre de configuration s'ouvre automatiquement
(*portail captif*). Si ce n'est pas le cas, ouvrez un navigateur et allez à
l'adresse :

```
http://192.168.4.1
```

### Étape 3 — Choisir votre réseau WiFi domestique

La page affiche la liste des réseaux WiFi détectés à proximité.

1. Sélectionnez votre réseau dans la liste déroulante
   (ou saisissez son nom manuellement s'il n'apparaît pas)
2. Saisissez le mot de passe de ce réseau
3. Cliquez sur **Enregistrer et connecter**

### Étape 4 — Redémarrage automatique

L'ESP32 enregistre les informations dans sa mémoire NVS, puis redémarre
automatiquement. Le point d'accès `GatewayLab-Setup` disparaît : c'est normal,
votre téléphone se reconnectera tout seul à votre WiFi habituel.

### Étape 5 — Retrouver l'interface

Une fois reconnecté à votre propre réseau WiFi, ouvrez un navigateur et allez
à :

```
http://gateway-lab.local
```

Si cette adresse ne fonctionne pas (certains réseaux ou certains systèmes
Windows ne supportent pas mDNS), consultez l'écran de votre routeur/box
internet pour trouver l'adresse IP attribuée à l'appareil
`gateway-lab`, ou utilisez la page **Système → Réseau WiFi** une fois
connecté une première fois.

---

## Gérer plusieurs réseaux WiFi

Une fois connecté à l'interface web, allez dans le menu **Système**
(page `/wifi`). Vous pouvez :

| Action | Description |
|---|---|
| Voir l'état actuel | SSID connecté, adresse IP, force du signal |
| Ajouter un réseau | Enregistrer un nouveau WiFi (ex : domicile, atelier, hotspot de secours) |
| Supprimer un réseau | Retirer un réseau de la mémoire de l'ESP32 |

Vous pouvez enregistrer **plusieurs réseaux**. Au démarrage, l'ESP32 essaie
de se connecter automatiquement au premier réseau connu disponible (celui
avec le signal le plus fort si plusieurs sont à portée).

C'est utile si l'appareil change parfois d'endroit : par exemple un réseau
"Maison" et un réseau "Atelier" peuvent être enregistrés en même temps.

---

## Réinitialiser la configuration WiFi

Pour forcer l'ESP32 à oublier tous ses réseaux et revenir au portail de
configuration :

1. Allez dans **Système → Réseau WiFi**
2. Supprimez tous les réseaux enregistrés un par un (bouton **✕ Supprimer**)
3. Redémarrez la carte (débrancher/rebrancher l'alimentation)

Au prochain démarrage, n'ayant plus aucun réseau enregistré, l'ESP32
réactivera automatiquement `GatewayLab-Setup`.

---

## Et pour les développeurs ?

Si vous développez le firmware (modification du code, recompilation), vous
pouvez éviter de repasser par le portail à chaque flash en définissant un
réseau de développement dans `include/secrets.h` (voir
`include/secrets_example.h`) :

```cpp
#define DEFAULT_WIFI_SSID     "MonWifi"
#define DEFAULT_WIFI_PASSWORD "MonMotDePasse"
```

Ce fichier n'est **jamais** utilisé si un réseau a déjà été enregistré via
le portail (priorité à la configuration NVS) — il ne sert qu'en
développement, sur une carte qui n'a encore aucun réseau enregistré.

`include/secrets.h` est ignoré par Git : il ne sera jamais distribué avec
le firmware `.bin` ni partagé publiquement.

---

## Questions fréquentes

**Le réseau `GatewayLab-Setup` n'apparaît jamais.**
L'ESP32 a probablement déjà un réseau enregistré (NVS) ou un
`DEFAULT_WIFI_SSID` valide dans `secrets.h`, et s'y connecte directement.
Vérifiez la page **Système** une fois connecté, ou réinitialisez la
configuration (voir ci-dessus).

**Le mot de passe que j'ai saisi semble incorrect.**
Le portail ne vérifie pas le mot de passe avant de l'enregistrer : il
redémarre et essaie de s'y connecter. Si la connexion échoue après
redémarrage (délai d'environ 15 secondes), l'ESP32 réactive automatiquement
le portail `GatewayLab-Setup` pour corriger les identifiants.

**Puis-je configurer le WiFi 5 GHz ?**
Non — l'ESP32 ne supporte que le WiFi 2,4 GHz (b/g/n). Assurez-vous que
votre routeur diffuse bien un réseau 2,4 GHz séparé ou en mode mixte.
