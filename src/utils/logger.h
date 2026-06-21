/**
 * Logger — Journalisation série avec niveaux de criticité
 *
 * Utilisation :
 *   Log::i("MonModule", "Valeur = %d", x);   // Information
 *   Log::w("MonModule", "Attention !");        // Avertissement
 *   Log::e("MonModule", "Erreur critique");    // Erreur
 *   Log::d("MonModule", "x=%d y=%d", x, y);   // Debug (verbose)
 *
 * Désactivation complète (build de production) :
 *   Ajouter dans platformio.ini build_flags : -D LOG_LEVEL=0
 *
 * Niveaux : 0=off  1=error  2=warn  3=info (défaut)  4=debug
 */

#pragma once
#include <Arduino.h>
#include "../../include/app_config.h"   // BOOT_LOG_ENABLED

#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#ifdef BOOT_LOG_ENABLED
#include "../modules/boot_log.h"   // [DEBOGAGE TEMPORAIRE] capture les logs avant reboot — voir boot_log.h
#endif

namespace Log {

namespace detail {
    // Formatage et impression d'un message horodaté sur le port série
    inline void _printf(const char* level, const char* tag, const char* fmt, va_list args) {
        char buf[256];
        vsnprintf(buf, sizeof(buf), fmt, args);
        Serial.printf("[%s][%s] %s\n", level, tag, buf);
#ifdef BOOT_LOG_ENABLED
        bootLog.capture(level, tag, buf);
#endif
    }
}

// Niveau DEBUG — informations très détaillées pour le développement
__attribute__((format(printf, 2, 3)))
inline void d(const char* tag, const char* fmt, ...) {
#if LOG_LEVEL >= 4
    va_list a; va_start(a, fmt); detail::_printf("DBG", tag, fmt, a); va_end(a);
#endif
}

// Niveau INFO — déroulement normal de l'application
__attribute__((format(printf, 2, 3)))
inline void i(const char* tag, const char* fmt, ...) {
#if LOG_LEVEL >= 3
    va_list a; va_start(a, fmt); detail::_printf("INF", tag, fmt, a); va_end(a);
#endif
}

// Niveau WARN — situation anormale mais non bloquante
__attribute__((format(printf, 2, 3)))
inline void w(const char* tag, const char* fmt, ...) {
#if LOG_LEVEL >= 2
    va_list a; va_start(a, fmt); detail::_printf("WRN", tag, fmt, a); va_end(a);
#endif
}

// Niveau ERROR — erreur critique, fonctionnalité impactée
__attribute__((format(printf, 2, 3)))
inline void e(const char* tag, const char* fmt, ...) {
#if LOG_LEVEL >= 1
    va_list a; va_start(a, fmt); detail::_printf("ERR", tag, fmt, a); va_end(a);
#endif
}

} // namespace Log
