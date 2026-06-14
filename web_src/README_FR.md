# Interface Web – Guide Développeur (FR) – v1

Ce dossier contient le **code source lisible et maintenable** de l’interface web du projet

## 👋 Pour les débutants

Si vous découvrez ce projet ou le développement web embarqué, **pas d’inquiétude !** Ce guide vous accompagne pas à pas.

**Principe clé :** L’ESP32 a une mémoire limitée, donc l’interface web est stockée en version compressée (« minifiée ») dans le firmware. Mais le code minifié est illisible. Ce système vous permet :
1. **D’éditer** du code propre et lisible (dans ce dossier)
2. **D’exécuter un script** pour le compresser automatiquement
3. **De compiler** et téléverser sur l’ESP32 comme d’habitude

**Ce que vous pouvez modifier :**
- `styles.css` – Tout le style visuel (couleurs, polices, mise en page, etc.)
- `app.js` – JavaScript complet pour ESP32-S3 (toutes les fonctionnalités interactives)
- `app-lite.js` – JavaScript allégé pour ESP32 Classic (fonctionnalités de base)
- `template.html` – Structure HTML de référence (documentation uniquement – le HTML réel est généré en C++)

**Ce que vous NE DEVEZ PAS modifier directement :**
- `include/web_interface.h` – Généré automatiquement. Ne jamais l’éditer à la main !

## 📁 Structure du dossier

```
web_src/
├── README_FR.md         # Ce fichier
├── template.html        # Modèle HTML (référence/documentation)
├── styles.css           # CSS lisible (13 Ko)
├── app.js               # JavaScript lisible – version complète (115 Ko)
└── app-lite.js          # JavaScript lisible – version allégée (3,8 Ko)
```

## 🎯 Objectif

Le code de l’interface web (HTML, CSS, JavaScript) est embarqué directement dans le firmware ESP32. Pour minimiser l’usage mémoire et maximiser les performances, ce code **doit être minifié** avant compilation.

Mais le code minifié est très difficile à maintenir. Ce dossier résout ce problème en fournissant :

1. **Sources lisibles** – Faciles à éditer et comprendre
2. **Minification automatique** – Scripts pour convertir le code lisible en code minifié
3. **Compatible avec le contrôle de version** – Suivi des modifications sur du code humainement lisible

### 📝 Note sur le HTML

La structure HTML est **générée dynamiquement** en C++ (`generateHTML()` dans `web_interface.h`) car elle inclut :
- Sélection dynamique de la langue
- Informations système en temps réel
- Adresses IP et données réseau
- Configuration spécifique à l’appareil

Le fichier `template.html` sert de :
- **Documentation** de la structure HTML
- **Référence** pour les développeurs
- **Validation** de la conformité HTML

Le CSS et le JavaScript sont extraits et minifiés, tandis que le HTML reste généré dynamiquement mais documenté ici.

## 🔄 Workflow

1. **Éditez** les fichiers dans `web_src/` (HTML, CSS, JS)
2. **Lancez** le script Python pour minifier :
   ```bash
   python tools/minify_web.py
   ```
3. **Compilez et téléversez** le firmware :
   ```bash
   pio run --target upload
   ```

Pour plus de détails, consultez le guide complet dans ce dossier et la documentation principale.

---

*Version : 1.0.0 – Traduction et harmonisation 100% FR/EN. Dernière mise à jour : 15/06/2026.*
