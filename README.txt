Gateway Lab V1 — v0.8.7
========================

⚠️ IMPORTANT : ce firmware n'a PAS été compilé ni testé dans un navigateur
dans cet environnement (PlatformIO/compilateur indisponible ici). Compilez
et testez normalement avant de flasher.

Changements
-----------
La page "Paramètres" est restructurée et renommée "Système". Elle regroupe
maintenant :
  - Réseau : état de la connexion (SSID/IP/RSSI), réseaux enregistrés,
    ajout d'un réseau
  - Appareil : LED d'état, mise à jour du firmware (OTA, ex page dédiée),
    état système (heap libre, PSRAM libre, équipements N/MAX, historique)

La page OTA dédiée (GET /update, web_src/ota.html/.js,
include/web_interface_ota.h) est supprimée — la fonctionnalité est
intégrée à la page Système. La route POST /update (réception du fichier
.bin) est inchangée côté backend.

Un nouveau bloc de navigation partagé web_src/menu.html est introduit,
inliné dans toutes les pages via le marqueur <!-- include:menu.html -->
(traité par tools/minify_web.py, simulé par tools/validate_html.py) — le
lien actif est désormais marqué au runtime par un petit script JS au lieu
d'un class="active" dupliqué par page.

`NetworkScanner::diagnosticsToJson()` retourne désormais aussi
deviceCount, maxDevices et historyCount (utilisés par la nouvelle carte
"État système").

Fichiers À SUPPRIMER manuellement dans votre copie locale (ce zip ne
contient que les fichiers ajoutés/modifiés, pas les suppressions) :
  - web_src/ota.html
  - web_src/ota.js
  - include/web_interface_ota.h

Fichier NOUVEAU dans ce zip :
  - web_src/menu.html

Étapes après extraction
------------------------
1. Copier les fichiers de ce zip par-dessus votre dépôt local
   (en respectant l'arborescence : src/, include/, web_src/, tools/, docs/,
   data/, et les .md/.ini à la racine).
2. Supprimer les 3 fichiers listés ci-dessus.
3. Vérifier (optionnel, les headers sont déjà régénérés dans ce zip) :
     python tools/validate_html.py
     python tools/minify_web.py
4. Compiler et flasher :
     pio run --target upload

Version : 0.8.6 → 0.8.7 (platformio.ini, README.md, CHANGELOG.md,
ROADMAP.md mis à jour).
