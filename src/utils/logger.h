#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Logger — wrapper Serial minimaliste, header-only, portable
// Désactiver avec : -D LOG_LEVEL=0 dans platformio.ini
// ---------------------------------------------------------------------------

#ifndef LOG_LEVEL
#define LOG_LEVEL 3   // 0=off 1=error 2=warn 3=info 4=debug
#endif

namespace Log {

namespace detail {
    inline void _print(const char* level, const char* tag, const char* msg) {
        Serial.printf("[%s][%s] %s\n", level, tag, msg);
    }
    inline void _printf(const char* level, const char* tag, const char* fmt, va_list args) {
        char buf[256];
        vsnprintf(buf, sizeof(buf), fmt, args);
        _print(level, tag, buf);
    }
}

__attribute__((format(printf, 2, 3)))
inline void d(const char* tag, const char* fmt, ...) {
#if LOG_LEVEL >= 4
    va_list a; va_start(a, fmt); detail::_printf("DBG", tag, fmt, a); va_end(a);
#endif
}

__attribute__((format(printf, 2, 3)))
inline void i(const char* tag, const char* fmt, ...) {
#if LOG_LEVEL >= 3
    va_list a; va_start(a, fmt); detail::_printf("INF", tag, fmt, a); va_end(a);
#endif
}

__attribute__((format(printf, 2, 3)))
inline void w(const char* tag, const char* fmt, ...) {
#if LOG_LEVEL >= 2
    va_list a; va_start(a, fmt); detail::_printf("WRN", tag, fmt, a); va_end(a);
#endif
}

__attribute__((format(printf, 2, 3)))
inline void e(const char* tag, const char* fmt, ...) {
#if LOG_LEVEL >= 1
    va_list a; va_start(a, fmt); detail::_printf("ERR", tag, fmt, a); va_end(a);
#endif
}

} // namespace Log
