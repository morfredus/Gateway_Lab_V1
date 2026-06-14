#!/usr/bin/env python3
"""
Web Assets Minifier — Gateway Lab V1

Minifie web_src/index.html vers data/index.html pour SPIFFS.
Peut être exécuté en standalone ou comme pre-script PlatformIO.

Usage standalone :
    python tools/minify_web.py

PlatformIO (extra_scripts = pre:tools/minify_web.py) :
    Exécuté automatiquement avant chaque compilation.

Requirements :
    pip install rcssmin rjsmin   (optionnels — fallback intégré si absents)
"""

import os
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR   = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
WEB_SRC_DIR  = PROJECT_ROOT / "web_src"
DATA_DIR     = PROJECT_ROOT / "data"
HTML_SRC     = WEB_SRC_DIR  / "index.html"
HTML_DST     = DATA_DIR     / "index.html"

# ---------------------------------------------------------------------------
# Optional minifiers (graceful fallback)
# ---------------------------------------------------------------------------
try:
    import rcssmin
    HAS_RCSSMIN = True
except ImportError:
    HAS_RCSSMIN = False

try:
    import rjsmin
    HAS_RJSMIN = True
except ImportError:
    HAS_RJSMIN = False

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _minify_css_block(css: str) -> str:
    if HAS_RCSSMIN:
        return rcssmin.cssmin(css)
    # Fallback: strip comments and collapse whitespace
    css = re.sub(r'/\*.*?\*/', '', css, flags=re.DOTALL)
    css = re.sub(r'\s*([{}:;,>~+])\s*', r'\1', css)
    css = re.sub(r'\s+', ' ', css).strip()
    return css


def _minify_js_block(js: str) -> str:
    if HAS_RJSMIN:
        return rjsmin.jsmin(js)
    # Fallback: strip single-line comments, collapse whitespace
    js = re.sub(r'//[^\n]*', '', js)
    js = re.sub(r'\s+', ' ', js).strip()
    return js


def minify_html(html: str) -> str:
    """
    Minifie le HTML :
      - supprime les commentaires HTML
      - minifie les blocs <style>…</style>
      - minifie les blocs <script>…</script>
      - compresse les espaces inter-balises
    """
    # Supprimer les commentaires HTML (hors IE conditionals)
    html = re.sub(r'<!--(?!\[if).*?-->', '', html, flags=re.DOTALL)

    # Minifier CSS inline
    def _repl_style(m):
        return '<style>' + _minify_css_block(m.group(1)) + '</style>'
    html = re.sub(r'<style>(.*?)</style>', _repl_style, html, flags=re.DOTALL)

    # Minifier JS inline
    def _repl_script(m):
        return '<script>' + _minify_js_block(m.group(1)) + '</script>'
    html = re.sub(r'<script>(.*?)</script>', _repl_script, html, flags=re.DOTALL)

    # Supprimer les espaces superflus entre balises
    html = re.sub(r'>\s+<', '><', html)
    # Condenser les espaces multiples (hors balises)
    html = re.sub(r'[ \t]{2,}', ' ', html)
    # Supprimer les lignes vides
    html = re.sub(r'\n\s*\n', '\n', html)
    html = html.strip()

    return html


def run():
    print("=" * 55)
    print("Gateway Lab V1 — Minification HTML")
    print("=" * 55)

    if not HTML_SRC.exists():
        print(f"ERREUR : {HTML_SRC} introuvable.")
        return False

    DATA_DIR.mkdir(parents=True, exist_ok=True)

    src = HTML_SRC.read_text(encoding='utf-8')
    dst = minify_html(src)

    ratio = 100 * (1 - len(dst) / len(src))
    print(f"  Source  : {len(src):,} octets  ({HTML_SRC.relative_to(PROJECT_ROOT)})")
    print(f"  Minifié : {len(dst):,} octets  ({HTML_DST.relative_to(PROJECT_ROOT)})")
    print(f"  Gain    : {ratio:.1f}%")

    HTML_DST.write_text(dst, encoding='utf-8')
    print("OK\n")
    return True


# ---------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------
# PlatformIO pre-script
try:
    Import("env")   # type: ignore  # noqa: F821
    run()
except NameError:
    # Standalone execution
    if __name__ == "__main__":
        ok = run()
        sys.exit(0 if ok else 1)
