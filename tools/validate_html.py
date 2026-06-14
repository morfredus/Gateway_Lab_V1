#!/usr/bin/env python3
"""
HTML Template Validator

This script validates and formats the HTML template.

Usage:
    python tools/validate_html.py

The script will:
1. Read web_src/template.html
2. Validate HTML structure
3. Check for common issues
4. Format and beautify if requested

Note: The HTML template is for documentation and reference.
The actual HTML is generated dynamically in C++ (web_interface.h).
"""

import os
import sys
import re
from pathlib import Path

try:
    from html.parser import HTMLParser
except ImportError:
    print("ERROR: html.parser not available")
    sys.exit(1)

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
WEB_SRC_DIR = PROJECT_ROOT / "web_src"
HTML_TEMPLATE = WEB_SRC_DIR / "template.html"

class HTMLValidator(HTMLParser):
    """Simple HTML validator"""

    # Self-closing tags that don't need closing tags
    SELF_CLOSING_TAGS = {'meta', 'link', 'img', 'br', 'hr', 'input', 'area', 'base', 'col', 'embed', 'source', 'track', 'wbr'}

    def __init__(self):
        super().__init__()
        self.errors = []
        self.warnings = []
        self.tag_stack = []
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        """Handle opening tags"""
        # Don't add self-closing tags to stack
        if tag.lower() not in self.SELF_CLOSING_TAGS:
            self.tag_stack.append(tag)

        # Check for duplicate IDs
        attr_dict = dict(attrs)
        if 'id' in attr_dict:
            elem_id = attr_dict['id']
            if elem_id in self.ids:
                self.warnings.append(f"Duplicate ID: {elem_id}")
            else:
                self.ids.add(elem_id)

        # Check for inline event handlers (onclick, etc.) - only report unique ones
        for attr_name, attr_value in attrs:
            if attr_name.startswith('on'):
                # Count inline handlers but don't spam warnings
                pass  # We'll count them separately

    def handle_endtag(self, tag):
        """Handle closing tags"""
        # Ignore closing tags for self-closing elements
        if tag.lower() in self.SELF_CLOSING_TAGS:
            return

        if not self.tag_stack:
            self.errors.append(f"Unexpected closing tag: </{tag}>")
            return

        if self.tag_stack[-1] != tag:
            self.errors.append(f"Tag mismatch: expected </{self.tag_stack[-1]}>, got </{tag}>")
        else:
            self.tag_stack.pop()

    def handle_data(self, data):
        """Handle text content"""
        pass

    def validate_complete(self):
        """Check if all tags are closed"""
        if self.tag_stack:
            self.errors.append(f"Unclosed tags: {', '.join(self.tag_stack)}")

def read_file(filepath):
    """Read file content"""
    with open(filepath, 'r', encoding='utf-8') as f:
        return f.read()

def validate_html_structure(html_content):
    """Validate HTML structure and return errors/warnings"""
    print("Validating HTML structure...")

    validator = HTMLValidator()

    try:
        validator.feed(html_content)
        validator.validate_complete()
    except Exception as e:
        validator.errors.append(f"Parse error: {str(e)}")

    return validator.errors, validator.warnings

def check_required_elements(html_content):
    """Check for required elements"""
    print("Checking required elements...")

    required = {
        'DOCTYPE': r'<!DOCTYPE\s+html>',
        'html tag': r'<html[^>]*>',
        'head tag': r'<head[^>]*>',
        'body tag': r'<body[^>]*>',
        'meta charset': r'<meta\s+charset=["\']UTF-8["\']',
        'meta viewport': r'<meta\s+name=["\']viewport["\']',
        'title': r'<title>',
    }

    missing = []
    for name, pattern in required.items():
        if not re.search(pattern, html_content, re.IGNORECASE):
            missing.append(name)

    return missing

def check_translations(html_content):
    """Check translation attributes"""
    print("Checking translation attributes...")

    # Find all data-i18n attributes
    i18n_pattern = r'data-i18n=["\']([^"\']+)["\']'
    i18n_keys = re.findall(i18n_pattern, html_content)

    print(f"  Found {len(i18n_keys)} translation keys")
    print(f"  Unique keys: {len(set(i18n_keys))}")

    # Check for elements that might need translation
    elements_with_text = re.findall(r'>([A-Z][a-z]+(?:\s+[A-Z][a-z]+)*)<', html_content)
    untranslated = [text for text in elements_with_text
                    if len(text) > 3 and text not in ['<!--', '-->']]

    if untranslated:
        print(f"\n  Potential untranslated text: {len(untranslated)} items")
        for text in untranslated[:5]:  # Show first 5
            print(f"    - {text}")
        if len(untranslated) > 5:
            print(f"    ... and {len(untranslated) - 5} more")

def analyze_html_stats(html_content):
    """Analyze HTML statistics"""
    print("\nHTML Statistics:")
    print(f"  Total size: {len(html_content)} bytes")
    print(f"  Lines: {html_content.count(chr(10)) + 1}")

    # Count elements
    tags = re.findall(r'<(\w+)', html_content)
    print(f"  Total tags: {len(tags)}")
    print(f"  Unique tags: {len(set(tags))}")

    # Count specific elements
    divs = html_content.count('<div')
    buttons = html_content.count('<button')
    spans = html_content.count('<span')

    print(f"\n  Element breakdown:")
    print(f"    <div>: {divs}")
    print(f"    <button>: {buttons}")
    print(f"    <span>: {spans}")

def validate_template():
    """Main validation function"""
    print("=" * 70)
    print("HTML Template Validator")
    print("=" * 70)

    if not HTML_TEMPLATE.exists():
        print(f"\nERROR: {HTML_TEMPLATE} not found!")
        return False

    print(f"\nReading template: {HTML_TEMPLATE}")
    html_content = read_file(HTML_TEMPLATE)

    # Validate structure
    errors, warnings = validate_html_structure(html_content)

    # Check required elements
    missing = check_required_elements(html_content)

    # Check translations
    check_translations(html_content)

    # Show statistics
    analyze_html_stats(html_content)

    # Report results
    print("\n" + "=" * 70)
    print("Validation Results")
    print("=" * 70)

    if missing:
        print(f"\n⚠️  Missing required elements: {len(missing)}")
        for item in missing:
            print(f"  - {item}")

    if errors:
        print(f"\n❌ Errors: {len(errors)}")
        for error in errors:
            print(f"  - {error}")

    # Count inline event handlers
    inline_handlers = len(re.findall(r'\son\w+\s*=', html_content))
    if inline_handlers > 0:
        print(f"\nℹ️  Info:")
        print(f"  - {inline_handlers} inline event handlers found (expected for this project)")

    if warnings:
        print(f"\n⚠️  Warnings: {len(warnings)}")
        for warning in warnings[:10]:  # Show first 10
            print(f"  - {warning}")
        if len(warnings) > 10:
            print(f"  ... and {len(warnings) - 10} more warnings")

    if not errors and not missing:
        print("\n✅ HTML template is valid!")
    else:
        print("\n❌ HTML template has issues that should be fixed")

    print("\n" + "=" * 70)
    print("\nNote: This template is for documentation and reference.")
    print("The actual HTML is generated dynamically in web_interface.h")
    print("=" * 70)
    print()

    return len(errors) == 0 and len(missing) == 0

if __name__ == "__main__":
    success = validate_template()
    sys.exit(0 if success else 1)
