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
include/web_interface_ota.h
include/oui_table.h
```

sont volontairement versionnés dans Git.

Ils doivent être mis à jour après modification de :

```text
web_src/index.html
web_src/scan.html
web_src/ota.html
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
