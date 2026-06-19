"""
custom_hooks.py

Hook PlatformIO optionnel permettant d'automatiser la génération
des ressources web avant la compilation du firmware.

Fonctionnement :
- Exécute tools/minify_web.py avant le build.
- Génère les fichiers include/web_interface*.h.
- Génère include/oui_table.h à partir de data/oui.json.
- Interrompt la compilation en cas d'erreur.

Ce hook n'est pas activé pendant la phase de développement active.
L'exécution manuelle reste privilégiée :

    python tools/minify_web.py

Activation :

    extra_scripts = pre:tools/custom_hooks.py

dans platformio.ini.
"""

import os
import subprocess
from SCons.Script import Import, ARGUMENTS  # type: ignore

# Récupération de l'environnement SCons
env = Import("env")

def run_minify_script(source, target, env):
    print("\n[⚡ PlatformIO Hook] Lancement STRICT avant compilation...")
    
    # Chemin vers ton script de minification
    script_path = os.path.join(env['PROJECT_DIR'], "tools", "minify_web.py")
    
    if os.path.exists(script_path):
        # Exécute minify_web.py avant la compilation.
        # Le build est interrompu si une erreur est détectée.
        result = subprocess.run(["python", script_path], capture_output=True, text=True)
        
        if result.returncode == 0:
            print("[✅ Success] Minification terminée.\n")
            if result.stdout:
                print(result.stdout)
        else:
            print("[❌ Error] Erreur de minification :")
            print(result.stderr)
            env.Exit(1) # On stoppe TOUT si la minification échoue
    else:
        print(f"[❌ Error] Script introuvable : {script_path}\n")

# Exécute automatiquement la génération des fichiers web avant
# la compilation du firmware.
#
# Cette étape produit les headers C++ à partir des fichiers
# présents dans web_src/ et data/.
#
# Le hook est déclenché suffisamment tôt pour garantir que le
# firmware utilise toujours les dernières versions générées.
env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", run_minify_script)

# Variante possible :
# Déclenchement à partir d'un fichier source précis.
# Conservée ici uniquement comme référence.
# env.AddPreAction("src/main.cpp", run_minify_script)