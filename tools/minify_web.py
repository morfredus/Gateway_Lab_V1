#!/usr/bin/env python3
"""
Web Assets Minifier

This script minifies CSS and JavaScript files and generates the web_interface.h header file.

Usage:
    python tools/minify_web.py

The script will:
1. Read source files from web_src/
2. Minify CSS and JavaScript
3. Update include/web_interface.h with minified content
4. Preserve the structure and comments of the header file

Requirements:
    pip install csscompressor rjsmin
"""

import os
import sys
import re
from pathlib import Path

try:
    import rcssmin
    import rjsmin
except ImportError:
    print("ERROR: Required modules not installed.")
    print("Please install: pip install rcssmin rjsmin")
    sys.exit(1)

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
WEB_SRC_DIR = PROJECT_ROOT / "web_src"
INCLUDE_DIR = PROJECT_ROOT / "include"
WEB_INTERFACE_H = INCLUDE_DIR / "web_interface.h"

# Source files
CSS_FILE = WEB_SRC_DIR / "styles.css"
JS_FILE_FULL = WEB_SRC_DIR / "app.js"
JS_FILE_LITE = WEB_SRC_DIR / "app-lite.js"

def read_file(filepath):
    """Read file content"""
    with open(filepath, 'r', encoding='utf-8') as f:
        return f.read()

def write_file(filepath, content):
    """Write content to file"""
    with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)

def minify_css(css_content):
    """Minify CSS content"""
    print("  Minifying CSS...")
    minified = rcssmin.cssmin(css_content)
    print(f"    Original: {len(css_content)} bytes")
    print(f"    Minified: {len(minified)} bytes")
    print(f"    Saved: {len(css_content) - len(minified)} bytes ({100 * (1 - len(minified)/len(css_content)):.1f}%)")
    return minified

def minify_js(js_content):
    """Minify JavaScript content"""
    print("  Minifying JavaScript...")
    minified = rjsmin.jsmin(js_content)
    print(f"    Original: {len(js_content)} bytes")
    print(f"    Minified: {len(minified)} bytes")
    print(f"    Saved: {len(js_content) - len(minified)} bytes ({100 * (1 - len(minified)/len(js_content)):.1f}%)")
    return minified

def escape_for_cpp_string(text):
    """Escape text for C++ string literal"""
    # Replace backslash first
    text = text.replace('\\', '\\\\')
    # Replace quotes
    text = text.replace('"', '\\"')
    # Replace newlines
    text = text.replace('\n', '\\n')
    text = text.replace('\r', '')
    return text

def extract_css_from_header(header_content):
    """Extract CSS section from web_interface.h"""
    # Find the CSS section in generateHTML()
    pattern = r'(html \+= "<style>";)(.*?)(html \+= "</style>";)'
    match = re.search(pattern, header_content, re.DOTALL)
    if match:
        return match.group(2)
    return None

def extract_js_from_header(header_content, is_lite=False):
    """Extract JavaScript constant from web_interface.h"""
    if is_lite:
        pattern = r'(static const char PROGMEM DIAGNOSTIC_JS_STATIC_LITE\[\] = R"JS\()(.*?)(\)JS";)'
    else:
        pattern = r'(static const char PROGMEM DIAGNOSTIC_JS_STATIC\[\] = R"JS\()(.*?)(\)JS";)'

    match = re.search(pattern, header_content, re.DOTALL)
    if match:
        return match.group(2)
    return None

def inject_css_into_header(header_content, minified_css):
    """Inject minified CSS into web_interface.h"""
    # Escape for C++ string
    escaped_css = escape_for_cpp_string(minified_css)

    # Split into manageable chunks (to avoid too long lines)
    max_line_length = 200
    css_lines = []
    current_line = ""

    for char in escaped_css:
        current_line += char
        if len(current_line) >= max_line_length and char in [';', '}']:
            css_lines.append('  html += "' + current_line + '";')
            current_line = ""

    if current_line:
        css_lines.append('  html += "' + current_line + '";')

    css_block = '\n'.join(css_lines)

    # Replace CSS section - ensure </style> tag is preserved
    pattern = r'(html \+= "<style>";)(.*?)(html \+= "</style>";)'

    # Build replacement with explicit </style> tag
    replacement = r'\1' + '\n' + css_block + '\n  html += "</style>";'

    new_content = re.sub(pattern, replacement, header_content, flags=re.DOTALL)
    return new_content

def inject_js_into_header(header_content, minified_js, is_lite=False):
    """Inject minified JavaScript into web_interface.h"""
    if is_lite:
        pattern = r'(static const char PROGMEM DIAGNOSTIC_JS_STATIC_LITE\[\] = R"JS\()(.*?)(\)JS";)'
    else:
        pattern = r'(static const char PROGMEM DIAGNOSTIC_JS_STATIC\[\] = R"JS\()(.*?)(\)JS";)'

    replacement = r'\1' + '\n' + minified_js + '\n' + r'\3'
    new_content = re.sub(pattern, replacement, header_content, flags=re.DOTALL)
    return new_content

def update_web_interface_header():
    """Main function to update web_interface.h"""
    print("=" * 60)
    print("Web Assets Minifier")
    print("=" * 60)

    # Check if source files exist
    if not CSS_FILE.exists():
        print(f"ERROR: {CSS_FILE} not found!")
        print("Please create readable CSS source in web_src/styles.css")
        return False

    if not WEB_INTERFACE_H.exists():
        print(f"ERROR: {WEB_INTERFACE_H} not found!")
        return False

    print(f"\n1. Reading source files...")
    print(f"   - CSS: {CSS_FILE}")
    if JS_FILE_FULL.exists():
        print(f"   - JS (Full): {JS_FILE_FULL}")
    if JS_FILE_LITE.exists():
        print(f"   - JS (Lite): {JS_FILE_LITE}")

    # Read source files
    css_content = read_file(CSS_FILE)
    header_content = read_file(WEB_INTERFACE_H)

    # Minify CSS
    print("\n2. Minifying CSS...")
    minified_css = minify_css(css_content)

    # Update header with minified CSS
    print("\n3. Injecting CSS into web_interface.h...")
    header_content = inject_css_into_header(header_content, minified_css)

    # Process JavaScript if available
    if JS_FILE_FULL.exists():
        print("\n4. Processing JavaScript (Full)...")
        js_content = read_file(JS_FILE_FULL)
        minified_js = minify_js(js_content)
        header_content = inject_js_into_header(header_content, minified_js, is_lite=False)

    if JS_FILE_LITE.exists():
        print("\n5. Processing JavaScript (Lite)...")
        js_lite_content = read_file(JS_FILE_LITE)
        minified_js_lite = minify_js(js_lite_content)
        header_content = inject_js_into_header(header_content, minified_js_lite, is_lite=True)

    # Write updated header
    print("\n6. Writing updated web_interface.h...")
    write_file(WEB_INTERFACE_H, header_content)

    print("\n" + "=" * 60)
    print("✅ Web interface header updated successfully!")
    print("=" * 60)
    print("\nNext steps:")
    print("  1. Compile your project with PlatformIO")
    print("  2. Upload to ESP32")
    print("  3. Verify web interface works correctly")
    print("\n")

    return True

if __name__ == "__main__":
    success = update_web_interface_header()
    sys.exit(0 if success else 1)
