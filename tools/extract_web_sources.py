#!/usr/bin/env python3
"""
Web Sources Extractor — Gateway Lab V1

Outil de récupération d'urgence : extrait le HTML embarqué depuis les
headers PROGMEM (include/web_interface*.h) et le réécrit dans web_src/.

Utile si les fichiers web_src/*.html sont perdus mais que les headers
générés sont encore présents dans le dépôt.

Usage :
    python tools/extract_web_sources.py           # prévisualise sans écrire
    python tools/extract_web_sources.py --force   # écrase les fichiers existants

Avertissement :
    Les headers contiennent du HTML minifié (commentaires supprimés,
    espaces réduits, styles.css injecté inline). Les fichiers extraits
    seront fonctionnels mais moins lisibles que les sources originales,
    et ne contiendront pas le <link> vers styles.css.

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

# Headers à extraire : (header source, constante PROGMEM, fichier web_src de destination)
SOURCES = [
    (INCLUDE_DIR / "web_interface.h",      "INDEX_HTML", WEB_SRC_DIR / "index.html"),
    (INCLUDE_DIR / "web_interface_scan.h", "SCAN_PAGE",  WEB_SRC_DIR / "scan.html"),
    (INCLUDE_DIR / "web_interface_ota.h",  "OTA_PAGE",   WEB_SRC_DIR / "ota.html"),
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
    """Reformate le HTML minifié si jsbeautifier est disponible."""
    if not HAS_JSBEAUTIFIER:
        return html
    opts = jsbeautifier.default_options()
    opts.indent_size = 2
    opts.end_with_newline = True
    try:
        return jsbeautifier.beautify_html(html, opts)
    except Exception:
        return html  # fallback silencieux


def _extract_progmem(header_text: str, const_name: str) -> str | None:
    """Extrait le contenu R"HTML(...)HTML" de la constante PROGMEM donnée."""
    pattern = rf'static const char\s+{re.escape(const_name)}\[\]\s+PROGMEM\s*=\s*R"HTML\((.*?)\)HTML"'
    m = re.search(pattern, header_text, re.DOTALL)
    if m:
        return m.group(1).strip()
    return None


def run(force: bool = False) -> bool:
    print("=" * 55)
    print("Gateway Lab V1 — Extracteur de sources web")
    print("=" * 55)

    if not force:
        print("\n  Mode prévisualisation (dry-run) — aucun fichier écrit.")
        print("  Relancer avec --force pour écraser les fichiers existants.")

    if HAS_JSBEAUTIFIER:
        print("\n  jsbeautifier disponible — HTML reformaté")
    else:
        print("\n  jsbeautifier absent — HTML extrait tel quel (minifié)")
        print("  (pip install jsbeautifier pour un résultat lisible)")

    print()

    ok = True

    for header_path, const_name, dest_path in SOURCES:
        print(f"  [{header_path.name}]  →  {dest_path.relative_to(PROJECT_ROOT)}")

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

        beautified = _beautify_html(html)
        print(f"    Extrait  : {len(html):,} octets (minifié)  →  {len(beautified):,} octets")

        if dest_path.exists():
            if not force:
                print(f"    ⚠️  {dest_path.name} existe — sera écrasé avec --force")
                continue
            else:
                print(f"    ⚠️  {dest_path.name} existe — écrasement")

        if force:
            dest_path.write_text(beautified, encoding='utf-8')
            print(f"    ✓ Écrit")

    print()
    print("  ⚠️  Rappel : styles.css a été injecté inline lors de la")
    print("     minification. Les fichiers extraits contiennent le CSS complet")
    print("     dans chaque <style> et ne référencent plus styles.css.")
    if force:
        print("     Nettoyez manuellement si vous souhaitez rétablir le <link>.")
    print()

    if ok:
        print("OK\n")
    else:
        print("Terminé avec des erreurs\n")

    return ok


if __name__ == "__main__":
    force = '--force' in sys.argv
    sys.exit(0 if run(force=force) else 1)
