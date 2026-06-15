# Backlog — Gateway Lab V1

Fonctionnalités planifiées par ordre de priorité.

---

## En cours / Prochaine version

- **Scan de ports** (`v0.1.x`) : détection des services actifs sur chaque équipement (HTTP, SSH, MQTT...)
- **Identification automatique des fabricants** : élargissement de la table OUI embarquée
  (actuellement ~40 entrées) — import de la base IEEE complète tronquée

---

## Roadmap produit

### Découverte réseau
- **mDNS/SSDP/Bonjour** : annonces de services sur le réseau local (v0.3.0)
- **DNS-SD** : découverte des services annoncés (_http._tcp, _mqtt._tcp...)
- **Historique des équipements** : persistance des appareils vus entre les redémarrages (NVS ou SD)
- **Détection de nouveaux équipements** : notification à l'apparition d'un MAC inconnu

### Intégrations domotiques
- **Support Philips Hue** (`v0.4.0`) : liste des ampoules, état on/off, couleur
- **Support Tado** : lecture des thermostats et zones de chauffe
- **Support X-Sense** : capteurs de fumée et CO connectés
- **Détection caméras Xiaomi** : identification par signature HTTP/mDNS

### Connectivité
- **MQTT** (`v0.5.0`) : publication des équipements détectés sur un broker MQTT
- **MQTT Broker intégré** : broker embarqué sur l'ESP32 pour les réseaux isolés
- **Export JSON** : endpoint `/api/export` pour récupérer l'inventaire complet

### Matériel
- **Écran OLED** : affichage local du nombre d'équipements et de l'IP
- **Analyse Bluetooth** : découverte des équipements BLE à proximité

---

## ✅ Réalisé

- ~~Interface Web~~ → v0.0.2 (page d'accueil PROGMEM, cartouche réseau, OTA)
- ~~Identifier automatiquement les fabricants via les adresses MAC (OUI)~~ → v0.0.3 (table OUI embarquée ~40 entrées)
- ~~Scan réseau~~ → v0.0.3 (sweep UDP + ARP lwIP, FreeRTOS async)
