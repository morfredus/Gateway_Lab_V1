# Contributing

Thank you for your interest in Gateway Lab V1.

This project is developed primarily as a learning and experimentation platform around:

* ESP32
* Embedded software architecture
* Network discovery
* Service discovery
* Local web interfaces

## Reporting issues

If you find a bug or unexpected behavior:

* Check existing issues first
* Open a new issue with enough details to reproduce the problem
* Include logs when possible

## Pull Requests

Pull requests are welcome.

Before submitting a PR:

* Keep changes focused on a single topic
* Ensure the project still compiles with PlatformIO
* Update documentation if necessary
* Preserve the modular architecture of the project

## Generated files

Some files are generated and intentionally versioned:

* include/oui_table.h
* include/web_interface.h
* include/web_interface_scan.h
* include/web_interface_ota.h

After modifying:

* data/oui.json
* web_src/*.html

run:

```bash
python tools/minify_web.py
```

before committing.

## Philosophy

Gateway Lab prioritizes:

* readability
* maintainability
* documentation
* learning

Clear and understandable code is preferred over clever code.
