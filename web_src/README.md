# web_src — Sources de l'interface web

Ce dossier contient les **sources lisibles et maintenables** de l'interface web embarquée
dans le firmware Gateway Lab V1.

---

## Principe

L'ESP32-S3 sert chaque page en HTML auto-contenu depuis la mémoire flash (PROGMEM).
Il n'existe pas de serveur de fichiers statiques : le CSS et le JavaScript sont
**injectés inline** dans chaque page par le script de minification.

```
web_src/styles.css     ──┐
web_src/index.html     ──┤
web_src/scan.html      ──┼── python tools/minify_web.py ──► include/*.h ──► pio run
web_src/ota.html       ──┤
data/oui.json          ──┘
```

---

## Fichiers

| Fichier | Rôle | Traité par minify ? |
|---|---|---|
| `index.html` | Page Accueil (`GET /`) | ✅ → `include/web_interface.h` |
| `scan.html` | Page Équipements (`GET /scan`) | ✅ → `include/web_interface_scan.h` |
| `ota.html` | Page OTA (`GET /update`) | ✅ → `include/web_interface_ota.h` |
| `styles.css` | CSS commun (reset, body, nav, footer…) | ✅ injecté inline dans chaque page |
| `template.html` | Gabarit de référence documentaire | ❌ (non listé dans PAGES) |

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
  <script> … JavaScript inline … </script>
</body>
```

Le CSS de `styles.css` est injecté à la place de `<link rel="stylesheet" href="styles.css">`.
Chaque page conserve un bloc `<style>` pour ses règles propres (largeur max, composants).

---

## Workflow de développement

### 1. Modifier les sources

- **Styles communs** → `web_src/styles.css`
- **Page Accueil** → `web_src/index.html`
- **Page Équipements** → `web_src/scan.html`
- **Page OTA** → `web_src/ota.html`

> Les fichiers HTML peuvent être ouverts directement dans un navigateur
> (le `<link>` vers `styles.css` est résolu localement).

### 2. Régénérer les headers PROGMEM

```bash
python tools/minify_web.py
```

Le script :
1. Lit `styles.css`, le minifie et l'injecte inline dans chaque HTML à la place du `<link>`
2. Minifie le CSS inline restant et le JavaScript de chaque page
3. Génère les headers `include/web_interface*.h`
4. Met à jour `data/index.html` (copie minifiée pour débogage)
5. Régénère `include/oui_table.h` depuis `data/oui.json`

### 3. Compiler et flasher

```bash
pio run --target upload
```

---

## Règle importante

> **Ne jamais modifier directement les fichiers `include/web_interface*.h`.**
> Ils sont générés automatiquement et seront écrasés au prochain `minify_web.py`.
