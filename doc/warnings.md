# Avertissements & Limitations — Gateway Lab V1

Catalogue des limitations connues, comportements non évidents et points
de vigilance pour la maintenance et l'évolution du projet.

---

## Réseau

### ⚠️ Résolution des noms d'hôtes non fonctionnelle (gethostbyaddr)

**Symptôme** : la colonne "Nom" de la page Équipements affiche "—" pour tous les appareils.

**Cause** : `gethostbyaddr()` (résolution DNS inverse PTR) n'est pas compilé dans le
lwIP fourni par le framework Arduino ESP32. Seul `gethostbyname()` (résolution par nom)
est disponible.

**État** : `NetworkScanner::_resolveHostnames()` est un no-op intentionnel (v0.0.4+).

**Solution envisagée** : construire manuellement la requête PTR en formant le nom
`x.x.x.x.in-addr.arpa` et appeler `dns_gethostbyname()` depuis `lwip/dns.h`.
Documenté dans ROADMAP.md v0.0.6.

---

### ⚠️ Table ARP lwIP limitée à 10 entrées (ARP_TABLE_SIZE)

**Symptôme** : sur un réseau /24 avec plus de 10 équipements actifs, des appareils
peuvent ne pas apparaître si leurs réponses ARP arrivent pendant qu'un autre lot est en cours.

**Cause** : `ARP_TABLE_SIZE = 10` est une constante lwIP non modifiable sans recompiler
le SDK. La table est écrasée circulairement par les nouvelles entrées.

**Mitigation actuelle** : sweep par lots de 5 avec 100 ms de pause + lecture de la table
ARP après chaque lot (`_sweepSubnet()`, `_readArpTable()`). Efficace pour les réseaux
domestiques (< 50 équipements répondant rapidement).

**Cas limite** : réseaux d'entreprise denses ou équipements à temps de réponse ARP > 100 ms
(appareils en veille profonde, VMs, conteneurs). Augmenter `BATCH_DELAY_MS` si nécessaire.

---

### ⚠️ Débordement de millis() après ~49 jours

**Symptôme** : la colonne "Vu il y a" peut afficher des valeurs incorrectes si l'ESP32
tourne en continu plus de 49,7 jours sans redémarrage.

**Cause** : `millis()` retourne un `uint32_t` qui déborde à 2^32 ms ≈ 49,7 jours.
`elapsedMs = millis() - d.lastSeen` reste correct par arithmétique modulaire **tant que
l'écart réel est < 49,7 jours**, mais un `lastSeen` antérieur au débordement et un
`millis()` post-débordement peuvent produire une durée anormalement grande.

**Impact** : cosmétique uniquement (affichage "Vu il y a Xh" erroné). Aucun crash.

---

### ℹ️ Scan ARP ne détecte pas les équipements hors sous-réseau

Le sweep ARP est limité au sous-réseau local (masque de l'interface WiFi).
Les équipements sur d'autres VLANs ou sous-réseaux ne sont pas visibles.

---

### ℹ️ Les adresses MAC randomisées (iOS 14+, Android 10+) changent à chaque connexion

Les smartphones modernes utilisent des adresses MAC aléatoires par réseau WiFi.
La déduplication par MAC peut créer plusieurs entrées pour le même appareil.
La colonne "Fabricant" affichera "—" car le OUI ne correspond à aucun constructeur réel.

---

## Sécurité

### 🔴 Interface web sans authentification

L'interface web (port 80) est accessible à tout équipement du réseau local sans
authentification. N'exposez pas ce port sur Internet.

Routes sensibles :
- `POST /api/scan` — déclenche un scan réseau (charge CPU ~5 s)
- `POST /update` — upload et flash d'un firmware `.bin` sans vérification de signature

**Recommandation** : utiliser sur un réseau domestique de confiance uniquement.
Une authentification HTTP Basic est prévue dans une version future.

---

### 🔴 Ne jamais committer include/secrets.h

`include/secrets.h` contient les identifiants WiFi en clair. Ce fichier est listé dans
`.gitignore` mais une erreur est toujours possible (`git add -f`, IDE tiers...).

Vérification préalable à tout push :
```bash
git status | grep secrets.h  # ne doit rien afficher
```

Utiliser `include/secrets_example.h` comme modèle versionné.

---

### ℹ️ Firmware OTA sans signature

Le mécanisme OTA (ArduinoOTA et HTTP POST `/update`) accepte tout fichier `.bin`
sans vérification de signature cryptographique. Un attaquant sur le réseau local
pourrait flasher un firmware malveillant.

---

## Pipeline de génération

### ⚠️ Les headers générés doivent être committés

`include/web_interface*.h` et `include/oui_table.h` sont des fichiers générés
**intentionnellement versionnés** dans Git. La compilation PlatformIO (`pio run`)
les utilise directement sans exécuter le script de minification.

Toujours exécuter `python tools/minify_web.py` après modification de :
- `web_src/index.html`
- `web_src/scan.html`
- `web_src/ota.html`
- `data/oui.json`

Puis committer les headers générés avec les sources modifiées.

---

### ℹ️ extra_scripts retiré intentionnellement

`extra_scripts = pre:tools/minify_web.py` a été retiré de `platformio.ini` car
le module `__file__` n'est pas défini dans le contexte SCons de PlatformIO,
provoquant une erreur `NameError` à la compilation. Voir CHANGELOG v0.0.3.

---

## Matériel

### ℹ️ Cible exclusive ESP32-S3 N16R8

Le projet est configuré pour l'ESP32-S3 DevKitC-1 N16R8 (16 Mo flash, 8 Mo PSRAM).
Pour porter sur un autre modèle ESP32 :
- Modifier `board` dans `platformio.ini`
- Vérifier `include/board_config.h` (brochage SPI, I2C, NeoPixel, GPIO)
- Ajuster les partitions si la flash est < 16 Mo

### ℹ️ USB CDC requis pour le port série

`board_build.usb_cdc_on_boot = 1` est activé pour que `Serial.begin()` sorte sur le
port USB natif (CDC). Sur certains clones, cette option peut nécessiter un double-appui
sur le bouton BOOT pour entrer en mode upload si le firmware plante au démarrage.
