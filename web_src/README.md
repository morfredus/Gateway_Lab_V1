# Web Interface Source Files

This directory contains the **readable, maintainable source code** for web interface.

## 👋 For Beginners

If you're new to this project or web development for embedded systems, **don't worry!** This guide will walk you through everything step-by-step.

**Key Concept:** The ESP32 has limited memory, so we store a compressed ("minified") version of the web interface in the firmware. However, minified code is hard to read and edit. This system lets you:
1. **Edit** clean, readable code (in this directory)
2. **Run a script** to automatically compress it
3. **Compile** and upload to ESP32 as usual

**What You Can Modify:**
- `styles.css` - All visual styling (colors, fonts, layout, spacing, etc.)
- `app.js` - Full-featured JavaScript for ESP32-S3 (all interactive features)
- `app-lite.js` - Lightweight JavaScript for ESP32 Classic (basic features only)
- `template.html` - HTML structure reference (documentation only - actual HTML is generated in C++)

**What You CANNOT Directly Modify:**
- `include/web_interface.h` - This is auto-generated. Never edit it manually!

## 📁 Directory Structure

```
web_src/
├── README.md           # This file
├── template.html       # HTML template (reference/documentation)
├── styles.css          # Readable CSS (13 KB)
├── app.js              # Readable JavaScript - Full version (115 KB)
└── app-lite.js         # Readable JavaScript - Lite version for ESP32 Classic (3.8 KB)
```

## 🎯 Purpose

The web interface code (HTML, CSS, JavaScript) is embedded directly into the ESP32 firmware. To minimize memory usage and maximize performance, this code **must be minified** before compilation.

However, minified code is extremely difficult to read and maintain. This directory solves that problem by providing:

1. **Readable source files** - Easy to edit and understand
2. **Automatic minification** - Scripts to convert readable code to minified code
3. **Version control friendly** - Track changes in human-readable format

### 📝 Note on HTML

The HTML structure is **generated dynamically** in C++ code (`generateHTML()` function in `web_interface.h`) because it includes:
- Dynamic language selection
- Real-time system information
- IP addresses and network data
- Device-specific configuration

The `template.html` file serves as:
- **Documentation** of the HTML structure
- **Reference** for developers
- **Validation** of HTML correctness

CSS and JavaScript are fully extracted and minified, while HTML remains dynamically generated but documented.

## 🔄 Workflow

### Initial Setup

1. **Install Python dependencies** (one-time):
   ```bash
   pip install rcssmin rjsmin jsbeautifier cssbeautifier
   ```

### Making Changes to the Web Interface

**IMPORTANT:** Never edit `include/web_interface.h` directly! Always edit the source files in `web_src/`.

#### Step 1: Extract Current Code (optional)

If you need to extract the latest minified code from `web_interface.h`:

```bash
python tools/extract_web_sources.py
```

This will:
- Extract minified CSS and JavaScript from `include/web_interface.h`
- Beautify the code
- Save readable versions to `web_src/`

#### Step 2: Edit Source Files

Edit the readable source files in `web_src/`:

- `styles.css` - Modify CSS styles
- `app.js` - Modify JavaScript (full version)
- `app-lite.js` - Modify JavaScript (lite version for ESP32 Classic)

#### Step 3: Minify and Update Header

After making changes, run:

```bash
python tools/minify_web.py
```

This will:
- Read source files from `web_src/`
- Minify CSS and JavaScript
- Update `include/web_interface.h` with minified content
- Show compression statistics

Example output:
```
======================================================================
ESP32 Diagnostic - Web Assets Minifier
======================================================================

1. Reading source files...
   - CSS: web_src/styles.css
   - JS (Full): web_src/app.js
   - JS (Lite): web_src/app-lite.js

2. Minifying CSS...
    Original: 13144 bytes
    Minified: 9715 bytes
    Saved: 3429 bytes (26.1%)

3. Injecting CSS into web_interface.h...

4. Processing JavaScript (Full)...
    Original: 115331 bytes
    Minified: 94279 bytes
    Saved: 21052 bytes (18.3%)

5. Processing JavaScript (Lite)...
    Original: 3819 bytes
    Minified: 2768 bytes
    Saved: 1051 bytes (27.5%)

6. Writing updated web_interface.h...

======================================================================
✅ Web interface header updated successfully!
======================================================================
```

#### Step 4: Compile and Upload

```bash
# Compile the project
pio run

# Upload to ESP32
pio run --target upload
```

#### Step 5: Test

1. Open the web interface at `http://ESP32-Diagnostic.local` or the ESP32's IP address
2. Test all functionality
3. Verify that changes are visible and working correctly

## 📊 File Sizes

| File | Readable Size | Minified Size | Savings |
|------|---------------|---------------|---------|
| CSS | ~13 KB | ~10 KB | ~26% |
| JavaScript (Full) | ~115 KB | ~94 KB | ~18% |
| JavaScript (Lite) | ~3.8 KB | ~2.8 KB | ~28% |

## 🛠️ Python Tools Documentation

The `tools/` directory contains Python scripts that automate the web interface development workflow. Here's everything you need to know about each tool.

### 📦 Python Dependencies

All tools require Python 3.6 or newer. Install dependencies once:

```bash
pip install rcssmin rjsmin jsbeautifier cssbeautifier
```

**Package Details:**
- `rcssmin` - Fast CSS minification library
- `rjsmin` - Fast JavaScript minification library
- `jsbeautifier` - JavaScript beautification/formatting
- `cssbeautifier` - CSS beautification/formatting

### 🔽 extract_web_sources.py

**Purpose:** Extracts minified code from `web_interface.h` and converts it back to readable format.

**When to use:**
- You want to recover the latest code from firmware
- Starting fresh after someone else made changes
- Synchronizing local files with firmware version

**Usage:**
```bash
python tools/extract_web_sources.py
```

**What it does:**
1. Reads `include/web_interface.h`
2. Extracts minified CSS from the `<style>` section
3. Extracts minified JavaScript from `DIAGNOSTIC_JS_STATIC` constant
4. Extracts minified JavaScript from `DIAGNOSTIC_JS_STATIC_LITE` constant
5. Beautifies (formats) each extracted code block
6. Writes to `web_src/styles.css`, `web_src/app.js`, `web_src/app-lite.js`

**Output Location:**
- `web_src/styles.css` - Beautified CSS
- `web_src/app.js` - Beautified JavaScript (full version)
- `web_src/app-lite.js` - Beautified JavaScript (lite version)

**Python Dependencies:**
- `jsbeautifier` - For formatting JavaScript
- `cssbeautifier` - For formatting CSS

**Note:** This script creates backups before overwriting existing files.

### 🔼 minify_web.py

**Purpose:** Minifies readable source code and updates the firmware header file.

**When to use:**
- **Every time** you modify any file in `web_src/`
- Before compiling and uploading to ESP32

**Usage:**
```bash
python tools/minify_web.py
```

**What it does:**
1. Reads source files from `web_src/`:
   - `styles.css`
   - `app.js`
   - `app-lite.js`
2. Minifies each file (removes whitespace, comments, etc.)
3. Escapes special characters for C++ strings
4. Injects minified code into `include/web_interface.h`
5. Shows compression statistics

**Output Location:**
- `include/web_interface.h` - Updated with minified code

**How Firmware Integration Works:**
- **CSS**: Injected between `html += "<style>";` and `html += "</style>";` tags
- **JavaScript (Full)**: Injected into `DIAGNOSTIC_JS_STATIC` R"JS()JS" constant
- **JavaScript (Lite)**: Injected into `DIAGNOSTIC_JS_STATIC_LITE` R"JS()JS" constant

**Python Dependencies:**
- `rcssmin` - For minifying CSS
- `rjsmin` - For minifying JavaScript

**Example Output:**
```
============================================================
ESP32 Diagnostic - Web Assets Minifier
============================================================

1. Reading source files...
   - CSS: web_src/styles.css
   - JS (Full): web_src/app.js
   - JS (Lite): web_src/app-lite.js

2. Minifying CSS...
    Original: 13325 bytes
    Minified: 9843 bytes
    Saved: 3482 bytes (26.1%)

3. Injecting CSS into web_interface.h...

4. Processing JavaScript (Full)...
    Original: 115415 bytes
    Minified: 94462 bytes
    Saved: 20953 bytes (18.2%)

5. Processing JavaScript (Lite)...
    Original: 3819 bytes
    Minified: 2753 bytes
    Saved: 1066 bytes (27.9%)

6. Writing updated web_interface.h...

============================================================
✅ Web interface header updated successfully!
============================================================
```

### ✅ validate_html.py

**Purpose:** Validates the HTML template structure for correctness.

**When to use:**
- After updating `template.html`
- To check for HTML errors before implementation
- To verify translation attributes are correct

**Usage:**
```bash
python tools/validate_html.py
```

**What it does:**
1. Reads `web_src/template.html`
2. Validates HTML structure and tag matching
3. Checks for duplicate element IDs
4. Reports missing required elements
5. Analyzes translation attributes (data-i18n)
6. Shows HTML statistics

**Python Dependencies:**
- Built-in `html.parser` module (no extra installation needed)

**Note:** This validates the documentation template only. The actual HTML is generated dynamically in C++ code (`generateHTML()` function in `web_interface.h`).

**Example Output:**
```
======================================================================
ESP32 Diagnostic - HTML Template Validator
======================================================================

Reading template: web_src/template.html
Validating HTML structure...
Checking required elements...
Checking translation attributes...
  Found 150 translation keys
  Unique keys: 145

HTML Statistics:
  Total size: 7317 bytes
  Lines: 202
  Total tags: 85
  Unique tags: 15

======================================================================
Validation Results
======================================================================

✅ HTML template is valid!
======================================================================
```

## 📝 Best Practices

1. **Always edit source files** - Never edit `web_interface.h` directly
2. **Test after minifying** - Always test the web interface after running minification
3. **Version control** - Commit both source files and generated `web_interface.h`
4. **Document changes** - Add comments to source files explaining complex logic
5. **Keep it clean** - Remove unused CSS/JavaScript to minimize firmware size

## 🚨 Important Notes

- The HTML structure is generated dynamically in C++ code (`generateHTML()` function)
- CSS is embedded in the `<style>` tag
- JavaScript is stored in `PROGMEM` constants
- Two JavaScript versions:
  - **Full** (`DIAGNOSTIC_JS_STATIC`) - Complete interface for ESP32-S3
  - **Lite** (`DIAGNOSTIC_JS_STATIC_LITE`) - Simplified interface for ESP32 Classic

## 🔍 Troubleshooting

### "Module not found" errors

Install Python dependencies:
```bash
pip install rcssmin rjsmin jsbeautifier cssbeautifier
```

### Minification produces broken code

1. Check for syntax errors in source files
2. Ensure JavaScript is valid (no ES6+ features not supported by minifier)
3. Test source files in a browser before minifying

### Web interface doesn't update after compilation

1. Clear browser cache (Ctrl+Shift+R or Cmd+Shift+R)
2. Verify `web_interface.h` was updated (check file modification time)
3. Ensure compilation completed successfully

## 📚 Additional Resources

- [Project README](../README.md)
- [Architecture Documentation](../docs/ARCHITECTURE.md)
- [Web Interface Documentation](../docs/WEB_INTERFACE.md)
- [API Reference](../docs/API_REFERENCE.md)

## 💡 Tips

- **Use comments** - Add comments in source files; they'll be removed during minification
- **Format code** - Use proper indentation; it helps readability and doesn't affect minified size
- **Test incrementally** - Test changes frequently rather than making many changes at once
- **Check diff** - Review the diff of `web_interface.h` before committing

---

**Version:** 1.0.0
**Last Updated:** 2026-06-15
