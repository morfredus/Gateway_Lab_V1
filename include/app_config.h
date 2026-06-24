#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// Timeouts
#define WIFI_CONNECT_TIMEOUT 15000
#define NETWORK_SCAN_TIMEOUT 5000

// Network
#define WEB_SERVER_PORT  80
#define MDNS_HOSTNAME    "gateway-lab"

// Mémoire — protections contre l'épuisement du heap
#define MAX_TRACKED_DEVICES   300     // Borne haute du nombre d'équipements suivis/persistés
#define MAX_HISTORY_EVENTS    1000    // Borne haute du journal d'événements (FIFO, /history.json)
#define MAX_NOTES_PER_DEVICE  20      // Borne haute des notes libres par équipement (FIFO)
#define MAX_NOTE_LENGTH       256     // Longueur max d'une note (tronquée au-delà)
#define HEAP_WARNING_BYTES    40000   // Sous ce seuil : log d'avertissement
#define HEAP_CRITICAL_BYTES   20000   // Sous ce seuil : mode dégradé (pas de redémarrage auto)
#define HEAP_RECOVERY_MARGIN  10000   // Hystérésis : sortie du mode dégradé au-delà de CRITICAL+marge

// Surveillance continue (NetworkScanner::serviceMonitor) — v1.0.0
#define MONITOR_INTERVAL_MIN_MINUTES   1     // Borne basse de la frequence configurable
#define MONITOR_INTERVAL_MAX_MINUTES   60    // Borne haute de la frequence configurable
#define MONITOR_INTERVAL_DEFAULT_MINUTES 5   // Frequence par defaut (1er demarrage)
#define MOBILE_AWAY_SHORT_MS  (30UL * 60UL * 1000UL)   // 30 min : absence courte, pas de penalite de stabilite
#define MOBILE_AWAY_LONG_MS   (2UL * 60UL * 60UL * 1000UL)  // 2h : absence longue, evenement "mobile_left"

// Features
#define ENABLE_OTA
#define ENABLE_WEB_SERVER
#define ENABLE_MDNS
// #define ENABLE_MQTT
// #define ENABLE_HUE

// [DEBOGAGE TEMPORAIRE] Journal de redemarrage (src/modules/boot_log.h) —
// capture les derniers logs + la raison du reset avant chaque reboot, sans
// moniteur serie. A retirer (cette ligne + boot_log.h/.cpp + page /debug)
// une fois le debogage termine.
#define BOOT_LOG_ENABLED
#define MAX_BOOT_LOG_ENTRIES   10     // Nombre de boots conserves dans /bootlog.json (FIFO)
#define LOG_BUFFER_SIZE        20     // Lignes du buffer circulaire RTC (logs avant reboot)
#define LOG_LINE_MAX_LEN       160    // Longueur max d'une ligne (JSON compact : t/lvl/tag/heap/blk/msg)
#define BOOT_LOG_STATS_INTERVAL_MS  30000   // Cadence de l'instantane RuntimeStats/WiFi periodique

#endif // APP_CONFIG_H