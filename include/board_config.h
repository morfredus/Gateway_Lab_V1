#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// =========================================================
//         CONFIGURATION ESP32-S3 DevKitC-1 (N16R8)
// =========================================================

#define BOARD_NAME "ESP32-S3 DevKitC-1"

// ============================================================
// Paramètres écran TFT
// ============================================================
#define DISPLAY_WIDTH      240   // Largeur écran pixels.
#define DISPLAY_HEIGHT     240   // Hauteur écran pixels.
#define DISPLAY_SPI_FREQ   40000000UL // 40 MHz : stable et rapide sur S3.

// ============================================================
// RAPPELS DE SÉCURITÉ ESP32-S3
// ============================================================
// - GPIO 3.3V uniquement (aucune broche 5V tolérante).
// - GPIO0 : strapping BOOT — ne rien connecter qui force LOW.
// - GPIO46 : entrée uniquement, attention au boot/JTAG.
// - GPS TX (vers RXD 18) et HC-SR04 ECHO (vers GPIO 35) : diviseur obligatoire si capteurs 5V.
// - I2C : pull-up 4.7 kΩ obligatoire.
// - LED : résistance série 220–470 Ω.
// - Buzzer : transistor obligatoire si >12 mA.
// ============================================================


// ============================================================
// SPI TFT + SD — SPI Natif ESP32-S3 (HSPI)
// ============================================================
#define DISPLAY_SCK_PIN     12   // SPI SCLK — natif, haute vitesse
#define DISPLAY_MOSI_PIN    11   // SPI MOSI — natif
#define DISPLAY_MISO_PIN    13   // SPI MISO — natif (lecture SD)
#define DISPLAY_CS_PIN      10   // CS TFT — GPIO libre
#define DISPLAY_DC_PIN       9   // Data/Command — GPIO libre
#define DISPLAY_RST_PIN      8   // Reset matériel TFT
#define DISPLAY_BL_PIN      18   // Backlight — PWM possible

// Alias TFT (compatibilité librairies)
#define TFT_MISO_PIN DISPLAY_MISO_PIN
#define TFT_MOSI_PIN DISPLAY_MOSI_PIN
#define TFT_SCLK_PIN DISPLAY_SCK_PIN
#define TFT_CS_PIN   DISPLAY_CS_PIN
#define TFT_DC_PIN   DISPLAY_DC_PIN
#define TFT_RST_PIN  DISPLAY_RST_PIN
#define TFT_BL_PIN   DISPLAY_BL_PIN

// SD Card (SPI partagé)
#define SD_MISO_PIN   DISPLAY_MISO_PIN
#define SD_MOSI_PIN   DISPLAY_MOSI_PIN
#define SD_SCLK_PIN   DISPLAY_SCK_PIN
#define SD_CS_PIN     1   // CS SD — GPIO sûr

// ============================================================
// I2C — Broches recommandées ESP32-S3
// ============================================================
#define I2C_SDA_PIN  15  // SDA — open-drain, pull-up 4.7 kΩ
#define I2C_SCL_PIN  16  // SCL — open-drain, pull-up 4.7 kΩ


// ============================================================
// NeoPixel
// ============================================================
#define NEOPIXEL_PIN   48  // NeoPixel — sortie 3.3V


// ============================================================
// Boutons
// ============================================================
#define BUTTON_BOOT_PIN 0   // BOOT — strapping
#define BUTTON_1_PIN   38   // Bouton user — entrée, pull-up interne
#define BUTTON_2_PIN   39   // Bouton user — entrée, pull-up interne

// ============================================================
// Capteurs & Sorties
// ============================================================
#define BUZZER_PIN          6 // Buzzer — sortie (transistor recommandé)

// ============================================================
// HC-SR04
// ============================================================
#define DISTANCE_TRIG_PIN 2   // TRIG — sortie 3.3V
#define DISTANCE_ECHO_PIN 35  // ECHO — entrée, diviseur obligatoire


#endif // BOARD_CONFIG_H
