# web_src — Sources de l'interface web

Ce dossier contient les **sources lisibles et maintenables** de l'interface web embarquée
dans le firmware Gateway Lab V1.

---

## Principe

L'ESP32-S3 sert chaque page en HTML auto-contenu depuis la mémoire flash (PROGMEM).
Il n'existe pas de serveur de fichiers statiques : le CSS et le JavaScript sont
**injectés inline** dans chaque page par le script de minification.

Les fichiers `*.html` ne contiennent que du HTML (+ une référence
`<script src="page.js">`), le CSS commun vit entièrement dans `styles.css`,
et chaque page a son propre fichier `*.js`.

```
web_src/styles.css     ──┐
web_src/index.html     ──┤
web_src/index.js       ──┤
web_src/scan.html      ──┤
web_src/scan.js         ──┼── python tools/minify_web.py ──► include/*.h ──► pio run
web_src/ota.html       ──┤
web_src/ota.js          ──┤
web_src/history.html   ──┤
web_src/history.js      ──┤
web_src/wifi.html       ──┤
web_src/wifi.js         ──┤
web_src/topology.html   ──┤
web_src/topology.js     ──┤
data/oui.json          ──┘
```

---

## Fichiers

| Fichier | Rôle | Traité par minify ? |
|---|---|---|
| `index.html` / `index.js` | Page Accueil (`GET /`) | ✅ → `include/web_interface.h` |
| `scan.html` / `scan.js` | Page Équipements (`GET /scan`) | ✅ → `include/web_interface_scan.h` |
| `ota.html` / `ota.js` | Page OTA (`GET /update`) | ✅ → `include/web_interface_ota.h` |
| `history.html` / `history.js` | Page Historique (`GET /history`) | ✅ → `include/web_interface_history.h` |
| `wifi.html` / `wifi.js` | Page Paramètres → Réseau WiFi (`GET /wifi`) | ✅ → `include/web_interface_wifi.h` |
| `topology.html` / `topology.js` | Page Topologie (`GET /topology`) | ✅ → `include/web_interface_topology.h` |
| `styles.css` | CSS commun (reset, body, nav, footer…) | ✅ injecté inline dans chaque page |
| `template.html` | Gabarit de référence documentaire | ❌ (non listé dans PAGES) |
| `extracted/` | Sortie de `extract_web_sources.py` (récupération d'urgence) | — (non versionné, ne pas modifier à la main) |

> Note : la page du **portail de configuration WiFi** (`GatewayLab-Setup`,
> mode point d'accès uniquement) n'appartient pas à ce dossier. Elle est
> définie directement dans `src/modules/wifi_manager.cpp`, car elle doit
> pouvoir s'afficher avant toute connexion réseau.

---

## Structure commune des pages

Toutes les pages partagent la même architecture HTML :

```
<head>
  <link rel="stylesheet" href="styles.css">   ← injecté inline par minify_web.py
  <style> … CSS spécifique à la page … </style>
</head>
<body>
  <header class="site-hdr">
    "Gateway Lab V1"  [vX.X.X]       ← version récupérée via /api/status
    <span class="site-tag">…</span>  ← titre contextuel
  </header>
  <nav> Accueil / Équipements / OTA </nav>
  <!-- contenu de la page -->
  <footer>
    <span id="footer-ts">…</span>    ← horodatage ou version firmware
    <a href="/">← Accueil</a>
  </footer>
  <script src="page.js"></script>   ← injecté inline par minify_web.py
</body>
```

Le CSS de `styles.css` est injecté à la place de `<link rel="stylesheet" href="styles.css">`,
et le JavaScript de `page.js` est injecté à la place de `<script src="page.js"></script>`.
Chaque page conserve un bloc `<style>` pour ses règles propres (largeur max, composants).

---

## Workflow de développement

### 1. Modifier les sources

- **Styles communs** → `web_src/styles.css`
- **Page Accueil** → `web_src/index.html` + `web_src/index.js`
- **Page Équipements** → `web_src/scan.html` + `web_src/scan.js`
- **Page OTA** → `web_src/ota.html` + `web_src/ota.js`
- **Page Historique** → `web_src/history.html` + `web_src/history.js`
- **Page Paramètres WiFi** → `web_src/wifi.html` + `web_src/wifi.js`
- **Page Topologie** → `web_src/topology.html` + `web_src/topology.js`

> Les fichiers HTML ne contiennent que du HTML/PHP (markup) : tout le CSS va
> dans `styles.css`, tout le JavaScript va dans le `*.js` correspondant à la page.

### 2. Régénérer les headers PROGMEM

```bash
python tools/minify_web.py
```

Le script :
1. Lit `styles.css`, le minifie et l'injecte inline dans chaque HTML à la place du `<link>`
2. Lit le `*.js` de chaque page, le minifie et l'injecte inline à la place du `<script src="...">`
3. Minifie le CSS/JS inline restant
4. Génère les headers `include/web_interface*.h`
5. Met à jour `data/index.html` (copie minifiée pour débogage)
6. Régénère `include/oui_table.h` depuis `data/oui.json`

### Récupération d'urgence

Si `web_src/*.html` ou `*.js` sont perdus mais que `include/web_interface*.h`
sont encore présents :

```bash
python tools/extract_web_sources.py --force
```

Le HTML/JS est extrait depuis les headers PROGMEM et écrit dans
`web_src/extracted/` (jamais dans `web_src/` directement, pour ne pas
écraser les sources originales).

### 3. Compiler et flasher

```bash
pio run --target upload
```

---

## Règle importante

> **Ne jamais modifier directement les fichiers `include/web_interface*.h`.**
> Ils sont générés automatiquement et seront écrasés au prochain `minify_web.py`.
