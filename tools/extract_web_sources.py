#!/usr/bin/env python3
"""
Web Sources Extractor

This script extracts minified CSS and JavaScript from web_interface.h
and creates beautified source files for easier maintenance.

Usage:
    python tools/extract_web_sources.py

The script will:
1. Read include/web_interface.h
2. Extract minified CSS and JavaScript
3. Beautify the extracted code
4. Create readable source files in web_src/

Requirements:
    pip install jsbeautifier cssbeautifier
"""

import os
import sys
import re
from pathlib import Path

try:
    import jsbeautifier
    import cssbeautifier
except ImportError:
    print("ERROR: Required modules not installed.")
    print("Please install: pip install jsbeautifier cssbeautifier")
    sys.exit(1)

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
WEB_SRC_DIR = PROJECT_ROOT / "web_src"
INCLUDE_DIR = PROJECT_ROOT / "include"
WEB_INTERFACE_H = INCLUDE_DIR / "web_interface.h"

def read_file(filepath):
    """Read file content"""
    with open(filepath, 'r', encoding='utf-8') as f:
        return f.read()

def write_file(filepath, content):
    """Write content to file"""
    filepath.parent.mkdir(parents=True, exist_ok=True)
    with open(filepath, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)

def extract_css_from_header(header_content):
    """Extract CSS from generateHTML() function in web_interface.h"""
    # Find lines between <style> and </style>
    lines = header_content.split('\n')
    css_lines = []
    in_css = False

    for line in lines:
        if 'html += "<style>";' in line:
            in_css = True
            continue
        elif 'html += "</style>";' in line:
            in_css = False
            break
        elif in_css and 'html +=' in line:
            # Extract the CSS content from the line
            match = re.search(r'html \+= "(.*?)";', line)
            if match:
                css_fragment = match.group(1)
                # Unescape
                css_fragment = css_fragment.replace('\\n', '\n')
                css_fragment = css_fragment.replace('\\"', '"')
                css_fragment = css_fragment.replace('\\\\', '\\')
                css_lines.append(css_fragment)

    return ''.join(css_lines)

def extract_js_from_header(header_content, is_lite=False):
    """Extract JavaScript from PROGMEM constant"""
    if is_lite:
        pattern = r'static const char PROGMEM DIAGNOSTIC_JS_STATIC_LITE\[\] = R"JS\((.*?)\)JS";'
    else:
        pattern = r'static const char PROGMEM DIAGNOSTIC_JS_STATIC\[\] = R"JS\((.*?)\)JS";'

    match = re.search(pattern, header_content, re.DOTALL)
    if match:
        return match.group(1).strip()
    return None

def beautify_css(css_content):
    """Beautify CSS"""
    options = cssbeautifier.default_options()
    options.indent_size = 4
    options.indent_char = ' '
    options.selector_separator_newline = True
    options.end_with_newline = True
    options.newline_between_rules = True

    beautified = cssbeautifier.beautify(css_content, options)
    return beautified

def beautify_js(js_content):
    """Beautify JavaScript"""
    options = jsbeautifier.default_options()
    options.indent_size = 4
    options.indent_char = ' '
    options.max_preserve_newlines = 2
    options.preserve_newlines = True
    options.keep_array_indentation = False
    options.break_chained_methods = False
    options.indent_scripts = 'normal'
    options.brace_style = 'collapse'
    options.space_before_conditional = True
    options.unescape_strings = False
    options.jslint_happy = False
    options.end_with_newline = True
    options.wrap_line_length = 0
    options.indent_inner_html = False
    options.comma_first = False
    options.e4x = False
    options.indent_empty_lines = False

    beautified = jsbeautifier.beautify(js_content, options)
    return beautified

def add_header_comment(content, file_type, version="3.31.0"):
    """Add header comment to source file"""
    if file_type == "css":
        header = f"""/**
 * Web Interface - Main Stylesheet
 * Version: {version}
 *
 * This file contains the readable, maintainable CSS for the web interface.
 * It is automatically minified during build and embedded into the firmware.
 *
 * DO NOT EDIT web_interface.h directly - edit this file instead!
 *
 * To rebuild web_interface.h after making changes:
 *   python tools/minify_web.py
 */

"""
    elif file_type == "js":
        header = f"""/**
 * ESP32 Diagnostic - Main Application JavaScript
 * Version: {version}
 *
 * This file contains the readable, maintainable JavaScript for the web interface.
 * It is automatically minified during build and embedded into the firmware.
 *
 * DO NOT EDIT web_interface.h directly - edit this file instead!
 *
 * To rebuild web_interface.h after making changes:
 *   python tools/minify_web.py
 */

"""
    elif file_type == "js-lite":
        header = f"""/**
 * ESP32 Diagnostic - Lite Application JavaScript
 * Version: {version}
 *
 * This is a simplified version for ESP32 Classic (limited memory).
 * It is automatically minified during build and embedded into the firmware.
 *
 * DO NOT EDIT web_interface.h directly - edit this file instead!
 *
 * To rebuild web_interface.h after making changes:
 *   python tools/minify_web.py
 */

"""
    else:
        header = ""

    return header + content

def extract_all_sources():
    """Main extraction function"""
    print("=" * 70)
    print("ESP32 Diagnostic - Web Sources Extractor")
    print("=" * 70)

    if not WEB_INTERFACE_H.exists():
        print(f"\nERROR: {WEB_INTERFACE_H} not found!")
        return False

    print(f"\n1. Reading web_interface.h...")
    header_content = read_file(WEB_INTERFACE_H)

    # Extract and beautify CSS
    print("\n2. Extracting CSS...")
    css_content = extract_css_from_header(header_content)
    if css_content:
        print(f"   ✓ Found CSS content ({len(css_content)} bytes minified)")
        print("   Beautifying CSS...")
        beautified_css = beautify_css(css_content)
        beautified_css = add_header_comment(beautified_css, "css")
        css_output = WEB_SRC_DIR / "styles.css"
        write_file(css_output, beautified_css)
        print(f"   ✓ Saved to {css_output}")
        print(f"   Result: {len(beautified_css)} bytes (readable)")
    else:
        print("   ✗ No CSS content found")

    # Extract and beautify JavaScript (Full)
    print("\n3. Extracting JavaScript (Full)...")
    js_content = extract_js_from_header(header_content, is_lite=False)
    if js_content:
        print(f"   ✓ Found JavaScript ({len(js_content)} bytes minified)")
        print("   Beautifying JavaScript...")
        beautified_js = beautify_js(js_content)
        beautified_js = add_header_comment(beautified_js, "js")
        js_output = WEB_SRC_DIR / "app.js"
        write_file(js_output, beautified_js)
        print(f"   ✓ Saved to {js_output}")
        print(f"   Result: {len(beautified_js)} bytes (readable)")
    else:
        print("   ✗ No JavaScript content found")

    # Extract and beautify JavaScript (Lite)
    print("\n4. Extracting JavaScript (Lite)...")
    js_lite_content = extract_js_from_header(header_content, is_lite=True)
    if js_lite_content:
        print(f"   ✓ Found JavaScript Lite ({len(js_lite_content)} bytes minified)")
        print("   Beautifying JavaScript...")
        beautified_js_lite = beautify_js(js_lite_content)
        beautified_js_lite = add_header_comment(beautified_js_lite, "js-lite")
        js_lite_output = WEB_SRC_DIR / "app-lite.js"
        write_file(js_lite_output, beautified_js_lite)
        print(f"   ✓ Saved to {js_lite_output}")
        print(f"   Result: {len(beautified_js_lite)} bytes (readable)")
    else:
        print("   ✗ No JavaScript Lite content found")

    print("\n" + "=" * 70)
    print("✅ Extraction complete!")
    print("=" * 70)
    print("\nSource files created in web_src/:")
    if css_content:
        print("  • styles.css - Beautified CSS")
    if js_content:
        print("  • app.js - Beautified JavaScript (Full)")
    if js_lite_content:
        print("  • app-lite.js - Beautified JavaScript (Lite)")

    print("\nYou can now edit these files.")
    print("After editing, run: python tools/minify_web.py")
    print()

    return True

if __name__ == "__main__":
    success = extract_all_sources()
    sys.exit(0 if success else 1)
