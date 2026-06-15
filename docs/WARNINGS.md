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
    String   ip;
    String   mac;
    String   manufacturer;
    String   hostname;
    String   type;
    uint32_t lastSeen;
    bool     online;
};
```

Cette structure constitue la source de vérité utilisée par :

* le scanner réseau
* l'API REST
* l'interface web
* les futurs modules mDNS, SSDP, Hue et MQTT

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

## Résolution des noms d'hôtes non fonctionnelle

### ⚠️ gethostbyaddr() indisponible

**Symptôme**

La colonne "Nom" affiche généralement "—".

**Cause**

`gethostbyaddr()` (résolution DNS inverse PTR) n'est pas disponible dans la version
de lwIP intégrée au framework Arduino ESP32.

**État**

`NetworkScanner::_resolveHostnames()` est actuellement un no-op volontaire.

**Évolution prévue**

Construction manuelle des requêtes PTR via :

```text
x.x.x.x.in-addr.arpa
```

et utilisation de `dns_gethostbyname()`.

Voir ROADMAP v0.0.6.

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

Ne jamais committer :

```text
include/secrets.h
```

Le fichier contient les identifiants Wi-Fi.

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
