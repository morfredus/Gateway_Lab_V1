#!/usr/bin/env python3
"""
HTML Validator — Gateway Lab V1

Valide la structure HTML des pages sources dans web_src/ :
  - index.html, scan.html, ota.html (pages réelles)
  - template.html (gabarit de référence)

Vérifie :
  - Structure HTML de base (DOCTYPE, head, body, meta…)
  - Balises correctement fermées (pas de mismatch)
  - IDs uniques
  - Présence du <link rel="stylesheet" href="styles.css"> dans les pages réelles
  - Présence des éléments structurels communs (.site-hdr, nav, footer)
  - Absence du <link styles.css> dans template.html (déjà présent manuellement)

Usage :
    python tools/validate_html.py
"""

import re
import sys
from html.parser import HTMLParser
from pathlib import Path

# ---------------------------------------------------------------------------
# Chemins
# ---------------------------------------------------------------------------
PROJECT_ROOT = Path(__file__).resolve().parent.parent
WEB_SRC_DIR  = PROJECT_ROOT / "web_src"

# Pages à valider : (fichier, est une page réelle servie par l'ESP32)
PAGES = [
    (WEB_SRC_DIR / "index.html",   True),
    (WEB_SRC_DIR / "scan.html",    True),
    (WEB_SRC_DIR / "ota.html",     True),
    (WEB_SRC_DIR / "template.html", False),
]

# ---------------------------------------------------------------------------
# Validateur HTML
# ---------------------------------------------------------------------------
class HTMLValidator(HTMLParser):
    SELF_CLOSING = {
        'meta', 'link', 'img', 'br', 'hr', 'input',
        'area', 'base', 'col', 'embed', 'source', 'track', 'wbr',
    }

    def __init__(self):
        super().__init__()
        self.errors   = []
        self.warnings = []
        self.tag_stack = []
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        if tag.lower() not in self.SELF_CLOSING:
            self.tag_stack.append(tag)
        attr_dict = dict(attrs)
        if 'id' in attr_dict:
            eid = attr_dict['id']
            if eid in self.ids:
                self.warnings.append(f"ID dupliqué : #{eid}")
            else:
                self.ids.add(eid)

    def handle_endtag(self, tag):
        if tag.lower() in self.SELF_CLOSING:
            return
        if not self.tag_stack:
            self.errors.append(f"Balise fermante inattendue : </{tag}>")
            return
        if self.tag_stack[-1] != tag:
            self.errors.append(
                f"Balise mal fermée : attendu </{self.tag_stack[-1]}>, reçu </{tag}>"
            )
        else:
            self.tag_stack.pop()

    def validate_complete(self):
        if self.tag_stack:
            self.errors.append(f"Balises non fermées : {', '.join(f'<{t}>' for t in self.tag_stack)}")

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------
def _check_required_elements(html: str) -> list[str]:
    """Éléments HTML obligatoires."""
    checks = {
        'DOCTYPE'       : r'<!DOCTYPE\s+html>',
        '<html>'        : r'<html[^>]*>',
        '<head>'        : r'<head[^>]*>',
        '<body>'        : r'<body[^>]*>',
        'meta charset'  : r'<meta\s[^>]*charset=["\']UTF-8["\']',
        'meta viewport' : r'<meta\s[^>]*name=["\']viewport["\']',
        '<title>'       : r'<title>',
    }
    return [name for name, pat in checks.items()
            if not re.search(pat, html, re.IGNORECASE)]


def _check_styles_link(html: str) -> bool:
    """Vérifie la présence du <link rel="stylesheet" href="styles.css">."""
    return bool(re.search(
        r'<link\s[^>]*href=["\']styles\.css["\'][^>]*>',
        html, re.IGNORECASE
    ))


def _check_common_structure(html: str) -> list[str]:
    """Vérifie les éléments structurels communs à toutes les pages Gateway Lab V1."""
    missing = []
    if 'class="site-hdr"' not in html and "class='site-hdr'" not in html:
        missing.append('en-tête (.site-hdr)')
    if '<nav>' not in html and '<nav ' not in html:
        missing.append('navigation (<nav>)')
    if '<footer>' not in html and '<footer ' not in html:
        missing.append('pied de page (<footer>)')
    if 'id="site-ver"' not in html:
        missing.append('pastille version (#site-ver)')
    return missing


def _check_nav_active(html: str) -> bool:
    """Vérifie qu'un lien de navigation est marqué actif."""
    return 'class="active"' in html or "class='active'" in html


def _analyze_stats(html: str) -> dict:
    tags = re.findall(r'<(\w+)', html)
    return {
        'size'    : len(html),
        'lines'   : html.count('\n') + 1,
        'tags'    : len(tags),
        'unique'  : len(set(tags)),
        'scripts' : html.count('<script'),
        'styles'  : html.count('<style'),
    }

# ---------------------------------------------------------------------------
# Validation d'une page
# ---------------------------------------------------------------------------
def validate_page(path: Path, is_real_page: bool) -> bool:
    rel = path.relative_to(PROJECT_ROOT)
    print(f"\n  ── {rel} {'(page réelle)' if is_real_page else '(gabarit)'} ──")

    if not path.exists():
        print(f"    ✗ Fichier introuvable")
        return False

    html = path.read_text(encoding='utf-8')
    page_ok = True

    # Structure HTML
    validator = HTMLValidator()
    try:
        validator.feed(html)
        validator.validate_complete()
    except Exception as e:
        validator.errors.append(f"Erreur de parsing : {e}")

    # Éléments obligatoires
    missing_required = _check_required_elements(html)

    # Éléments structurels Gateway Lab V1
    missing_structure = _check_common_structure(html)

    # Lien styles.css (pages réelles seulement)
    has_styles_link = _check_styles_link(html)

    # Nav active
    has_active = _check_nav_active(html)

    # Stats
    stats = _analyze_stats(html)
    print(f"    Taille : {stats['size']:,} o  |  Lignes : {stats['lines']}  |  "
          f"Balises : {stats['tags']} ({stats['unique']} uniques)  |  "
          f"<script> : {stats['scripts']}  |  <style> : {stats['styles']}")

    # Rapport erreurs de structure HTML
    if validator.errors:
        page_ok = False
        for e in validator.errors:
            print(f"    ✗ {e}")
    if validator.warnings:
        for w in validator.warnings:
            print(f"    ⚠ {w}")

    # Éléments manquants
    if missing_required:
        page_ok = False
        for item in missing_required:
            print(f"    ✗ Manque : {item}")

    # Structure commune
    if missing_structure:
        page_ok = False
        for item in missing_structure:
            print(f"    ✗ Structure manquante : {item}")

    # Lien styles.css
    if is_real_page:
        if has_styles_link:
            print(f"    ✓ <link styles.css> présent")
        else:
            page_ok = False
            print(f"    ✗ <link rel=\"stylesheet\" href=\"styles.css\"> absent")

    # Nav active
    if has_active:
        print(f"    ✓ Lien nav actif (class=\"active\") présent")
    else:
        print(f"    ⚠ Aucun lien nav marqué actif (class=\"active\")")

    if page_ok and not validator.warnings:
        print(f"    ✓ Valide")

    return page_ok

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def run() -> bool:
    print("=" * 55)
    print("Gateway Lab V1 — Validateur HTML")
    print("=" * 55)
    print(f"\n  Répertoire : {WEB_SRC_DIR.relative_to(PROJECT_ROOT)}")

    all_ok = True
    results = {}

    for path, is_real in PAGES:
        ok = validate_page(path, is_real)
        results[path.name] = ok
        if not ok:
            all_ok = False

    # Récap
    print(f"\n{'=' * 55}")
    print("  Récapitulatif")
    print(f"{'=' * 55}")
    for name, ok in results.items():
        status = "✓" if ok else "✗"
        print(f"  {status}  {name}")

    print()
    if all_ok:
        print("OK\n")
    else:
        print("Terminé avec des erreurs\n")
        print("  Note : les fichiers sources sont dans web_src/.")
        print("  Après correction, relancer : python tools/minify_web.py")
        print()

    return all_ok


if __name__ == "__main__":
    sys.exit(0 if run() else 1)
