#!/usr/bin/env python3
"""
Web Sources Extractor — Gateway Lab V1

Outil de récupération d'urgence : extrait le HTML/JS embarqué depuis les
headers PROGMEM (include/web_interface*.h) et les écrit dans
web_src/extracted/ (jamais dans web_src/ directement, pour ne pas
écraser les sources originales lisibles).

Utile si les fichiers web_src/*.html|*.js sont perdus mais que les headers
générés sont encore présents dans le dépôt.

Usage :
    python tools/extract_web_sources.py           # prévisualise sans écrire
    python tools/extract_web_sources.py --force   # écrit dans web_src/extracted/

Avertissement :
    Les headers contiennent du HTML minifié (commentaires supprimés,
    espaces réduits, styles.css injecté inline). Les fichiers extraits
    seront fonctionnels mais moins lisibles que les sources originales,
    et le HTML ne contiendra pas le <link> vers styles.css (CSS inline).
    Le JavaScript inline est cependant ressorti dans un fichier .js séparé,
    référencé depuis le HTML extrait via <script src="...">.

Requirements (optionnels — fallback si absent) :
    pip install jsbeautifier
"""

import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Chemins
# ---------------------------------------------------------------------------
PROJECT_ROOT = Path(__file__).resolve().parent.parent
WEB_SRC_DIR  = PROJECT_ROOT / "web_src"
INCLUDE_DIR  = PROJECT_ROOT / "include"
EXTRACT_DIR  = WEB_SRC_DIR / "extracted"

# Headers à extraire : (header source, constante PROGMEM, html_dest, js_dest)
SOURCES = [
    (INCLUDE_DIR / "web_interface.h",         "INDEX_HTML",   EXTRACT_DIR / "index.html",   EXTRACT_DIR / "index.js"),
    (INCLUDE_DIR / "web_interface_scan.h",     "SCAN_PAGE",    EXTRACT_DIR / "scan.html",    EXTRACT_DIR / "scan.js"),
    (INCLUDE_DIR / "web_interface_ota.h",      "OTA_PAGE",     EXTRACT_DIR / "ota.html",     EXTRACT_DIR / "ota.js"),
    (INCLUDE_DIR / "web_interface_history.h",  "HISTORY_PAGE", EXTRACT_DIR / "history.html", EXTRACT_DIR / "history.js"),
    (INCLUDE_DIR / "web_interface_wifi.h",     "WIFI_PAGE",    EXTRACT_DIR / "wifi.html",    EXTRACT_DIR / "wifi.js"),
]

# ---------------------------------------------------------------------------
# Beautifier optionnel
# ---------------------------------------------------------------------------
try:
    import jsbeautifier
    HAS_JSBEAUTIFIER = True
except ImportError:
    HAS_JSBEAUTIFIER = False


def _beautify_html(html: str) -> str:
    if not HAS_JSBEAUTIFIER:
        return html
    opts = jsbeautifier.default_options()
    opts.indent_size = 2
    opts.end_with_newline = True
    try:
        return jsbeautifier.beautify_html(html, opts)
    except Exception:
        return html  # fallback silencieux


def _beautify_js(js: str) -> str:
    if not HAS_JSBEAUTIFIER:
        return js
    opts = jsbeautifier.default_options()
    opts.indent_size = 2
    opts.end_with_newline = True
    try:
        return jsbeautifier.beautify(js, opts)
    except Exception:
        return js  # fallback silencieux


def _extract_progmem(header_text: str, const_name: str) -> str | None:
    """Extrait le contenu R"HTML(...)HTML" de la constante PROGMEM donnée."""
    pattern = rf'static const char\s+{re.escape(const_name)}\[\]\s+PROGMEM\s*=\s*R"HTML\((.*?)\)HTML"'
    m = re.search(pattern, header_text, re.DOTALL)
    if m:
        return m.group(1).strip()
    return None


def _split_script(html: str, js_filename: str) -> tuple[str, str]:
    """Sépare le premier bloc <script>...</script> du HTML.

    Retourne (html_sans_script_inline, js_content). Si aucun script n'est
    trouvé, js_content est une chaîne vide et le HTML est inchangé.
    """
    m = re.search(r'<script>(.*?)</script>', html, flags=re.DOTALL)
    if not m:
        return html, ''
    js_content = m.group(1)
    html_out = html[:m.start()] + f'<script src="{js_filename}"></script>' + html[m.end():]
    return html_out, js_content


def run(force: bool = False) -> bool:
    print("=" * 55)
    print("Gateway Lab V1 — Extracteur de sources web")
    print("=" * 55)

    if not force:
        print("\n  Mode prévisualisation (dry-run) — aucun fichier écrit.")
        print("  Relancer avec --force pour écrire dans web_src/extracted/.")

    if HAS_JSBEAUTIFIER:
        print("\n  jsbeautifier disponible — HTML/JS reformatés")
    else:
        print("\n  jsbeautifier absent — HTML/JS extraits tels quels (minifiés)")
        print("  (pip install jsbeautifier pour un résultat lisible)")

    print()
    print(f"  Destination : {EXTRACT_DIR.relative_to(PROJECT_ROOT)}/")
    print("  (les sources originales de web_src/ ne sont jamais modifiées)")
    print()

    ok = True

    if force:
        EXTRACT_DIR.mkdir(parents=True, exist_ok=True)

    for header_path, const_name, html_dest, js_dest in SOURCES:
        print(f"  [{header_path.name}]  →  {html_dest.relative_to(PROJECT_ROOT)} + {js_dest.name}")

        if not header_path.exists():
            print(f"    ERREUR : fichier introuvable — ignoré")
            ok = False
            continue

        header_text = header_path.read_text(encoding='utf-8')
        html = _extract_progmem(header_text, const_name)

        if html is None:
            print(f"    ERREUR : constante PROGMEM '{const_name}' non trouvée dans {header_path.name}")
            ok = False
            continue

        html_part, js_part = _split_script(html, js_dest.name)
        beautified_html = _beautify_html(html_part)
        beautified_js   = _beautify_js(js_part)

        print(f"    Extrait  : {len(html):,} octets (minifié) → "
              f"{len(beautified_html):,} o (html) + {len(beautified_js):,} o (js)")

        if not force:
            continue

        html_dest.write_text(beautified_html, encoding='utf-8')
        js_dest.write_text(beautified_js, encoding='utf-8')
        print(f"    ✓ Écrit")

    print()
    print("  ⚠️  Rappel : styles.css a été injecté inline lors de la")
    print("     minification. Le HTML extrait contient le CSS complet")
    print("     dans un <style> et ne référence plus styles.css.")
    print()

    if ok:
        print("OK\n")
    else:
        print("Terminé avec des erreurs\n")

    return ok


if __name__ == "__main__":
    force = '--force' in sys.argv
    sys.exit(0 if run(force=force) else 1)
