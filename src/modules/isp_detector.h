/**
 * IspDetector — Identification des boxes des principaux FAI français
 *
 * Détecte et enrichit les champs manufacturer, model et category pour :
 *   - Free      : Freebox Ultra / Pop / Révolution / Delta / Mini 4K
 *   - Orange    : Livebox 4 / 5 / 6
 *   - SFR       : Box Plus / Box 8
 *   - Bouygues  : Bbox / Bbox Miami / Bbox Ultym
 *
 * Sources de détection par priorité décroissante :
 *   1. hostname (mDNS .local ou PTR DNS) — le plus précis
 *   2. manufacturer OUI (déjà renseigné par la table OUI)
 *
 * Règles :
 *   - Détection non bloquante, 100 % locale (pas de réseau requis)
 *   - N'écrase jamais un hostname déjà renseigné
 *   - category = "Router" pour toutes les boxes détectées
 *   - model vide si la génération ne peut pas être identifiée
 */

#pragma once
#include <Arduino.h>
#include "network_scanner.h"

// Test de sous-chaîne insensible à la casse (helper interne)
static inline bool _iContains(const String& s, const char* pattern) {
    String lower = s;
    lower.toLowerCase();
    return lower.indexOf(pattern) >= 0;
}

/**
 * Applique la détection FAI sur un NetworkDevice.
 * Modifie manufacturer, model, category si une box FAI est identifiée.
 * Appelée après la résolution de hostname pour bénéficier du nom complet.
 */
inline void applyIspDetection(NetworkDevice& dev) {
    const String& h   = dev.hostname;
    const String& mfr = dev.manufacturer;

    // ----------------------------------------------------------------
    // Free / Freebox
    // Signatures hostname : freebox-server, freebox-ultra, freebox-pop…
    // Signatures OUI      : "Freebox SAS", "Free SAS"
    // ----------------------------------------------------------------
    if (_iContains(h, "freebox") || _iContains(mfr, "freebox") || _iContains(mfr, "free sas")) {
        dev.manufacturer = "Free";
        dev.category     = "Router";
        if      (_iContains(h, "ultra"))                                    dev.model = "Freebox Ultra";
        else if (_iContains(h, "pop"))                                      dev.model = "Freebox Pop";
        else if (_iContains(h, "revolution") || _iContains(h, "r\xe9volution")) dev.model = "Freebox R\xe9volution";
        else if (_iContains(h, "delta"))                                    dev.model = "Freebox Delta";
        else if (_iContains(h, "mini"))                                     dev.model = "Freebox Mini 4K";
        else                                                                 dev.model = "Freebox";
        return;
    }

    // ----------------------------------------------------------------
    // Orange / Livebox
    // Signatures hostname : livebox, livebox4, livebox-5…
    // Signatures OUI      : "Orange SA", "Orange"
    // ----------------------------------------------------------------
    if (_iContains(h, "livebox") || _iContains(mfr, "orange")) {
        dev.manufacturer = "Orange";
        dev.category     = "Router";
        if      (_iContains(h, "livebox6") || _iContains(h, "livebox-6") ||
                 _iContains(h, "livebox 6"))                                dev.model = "Livebox 6";
        else if (_iContains(h, "livebox5") || _iContains(h, "livebox-5") ||
                 _iContains(h, "livebox 5"))                                dev.model = "Livebox 5";
        else if (_iContains(h, "livebox4") || _iContains(h, "livebox-4") ||
                 _iContains(h, "livebox 4"))                                dev.model = "Livebox 4";
        else                                                                 dev.model = "Livebox";
        return;
    }

    // ----------------------------------------------------------------
    // SFR / SFR Box
    // Signatures hostname : sfrbox, sfr-box, sfr-…
    // Signatures OUI      : "SFR"
    // ----------------------------------------------------------------
    if (_iContains(h, "sfrbox") || _iContains(h, "sfr-box") ||
        _iContains(h, "sfr-")   || _iContains(mfr, "sfr")) {
        dev.manufacturer = "SFR";
        dev.category     = "Router";
        if      (_iContains(h, "box-8")  || _iContains(h, "box8"))  dev.model = "Box 8";
        else if (_iContains(h, "plus"))                               dev.model = "Box Plus";
        else                                                           dev.model = "SFR Box";
        return;
    }

    // ----------------------------------------------------------------
    // Bouygues Telecom / Bbox
    // Signatures hostname : bbox, bbox-miami, bbox-ultym…
    // Signatures OUI      : "Bouygues Telecom", "Bouygues"
    // ----------------------------------------------------------------
    if (_iContains(h, "bbox") || _iContains(mfr, "bouygues")) {
        dev.manufacturer = "Bouygues Telecom";
        dev.category     = "Router";
        if      (_iContains(h, "ultym"))  dev.model = "Bbox Ultym";
        else if (_iContains(h, "miami"))  dev.model = "Bbox Miami";
        else                               dev.model = "Bbox";
        return;
    }
}
