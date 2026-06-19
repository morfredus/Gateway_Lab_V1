# Installation de Gateway Lab

Ce guide explique comment installer et configurer Gateway Lab sur un ESP32-S3.

## Prérequis

### Matériel

* ESP32-S3
* 16 Mo de Flash recommandés
* 8 Mo de PSRAM recommandés
* Câble USB de données

### Logiciel

Pour un premier essai, le plus simple est d'utiliser ESP Launchpad :

https://espressif.github.io/esp-launchpad/

## Télécharger le firmware

Téléchargez la dernière version du firmware depuis la section Releases du dépôt GitHub.

Fichier :

```text
gateway-lab-vX.X.X-firmware.bin
```

## Flash du firmware

### 1. Connecter l'ESP32-S3

Branchez l'ESP32-S3 à votre ordinateur à l'aide d'un câble USB.

### 2. Ouvrir ESP Launchpad

Lancez ESP Launchpad.

### 3. Sélectionner la carte

Choisissez :

```text
ESP32-S3
```

### 4. Charger le firmware

Sélectionnez :

```text
gateway-lab-vX.X.X-firmware.bin
```

### 5. Flasher

Cliquez sur :

```text
Flasher l'appareil
```

Patientez jusqu'à la fin de l'opération.

L'ESP32 redémarrera automatiquement.

## Première configuration

Au premier démarrage, Gateway Lab crée automatiquement un point d'accès WiFi :

```text
GatewayLab-Setup
```

Connectez-vous à ce réseau depuis votre ordinateur, tablette ou smartphone.

Une page de configuration devrait s'ouvrir automatiquement.

Si ce n'est pas le cas, ouvrez :

```text
http://192.168.4.1
```

## Configuration du WiFi

Saisissez :

* le nom du réseau WiFi (SSID)
* le mot de passe

Cliquez sur :

```text
Enregistrer
```

Gateway Lab redémarre automatiquement.

## Accéder à Gateway Lab

Une fois connecté à votre réseau local :

```text
http://gateway-lab-v1.local
```

Si votre réseau ne prend pas en charge mDNS, utilisez l'adresse IP affichée par votre routeur ou votre box Internet.

## Réseaux WiFi enregistrés

Gateway Lab peut mémoriser plusieurs réseaux WiFi.

Les réseaux enregistrés sont conservés en mémoire même après :

* un redémarrage
* une coupure de courant
* une mise à jour OTA

Ils peuvent être gérés depuis :

```text
Paramètres → Réseau WiFi
```

## Mise à jour du firmware

Les versions suivantes peuvent être installées directement depuis l'interface Web grâce à la fonction OTA.

Aucune connexion USB n'est nécessaire après l'installation initiale.

## Dépannage

### Impossible d'accéder à Gateway Lab

Vérifiez :

* que l'ESP32 est connecté au WiFi
* que votre appareil est sur le même réseau
* que mDNS est supporté par votre système

Essayez également l'adresse IP attribuée par votre routeur.

### Le portail de configuration n'apparaît pas

Connectez-vous manuellement au réseau :

```text
GatewayLab-Setup
```

Puis ouvrez :

```text
http://192.168.4.1
```

## Support

Pour signaler un bug ou proposer une amélioration :

* Ouvrez une issue GitHub
* Consultez le README et la documentation du projet
