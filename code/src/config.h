#pragma once

// EPD (FSPI bus)
#define EPD_SCK   36
#define EPD_MOSI  35
#define EPD_CS    10
#define EPD_DC     9
#define EPD_RST    6
#define EPD_BUSY   5
#define EPD_PWR   13

// SD card (HSPI bus)
#define SD_SCK    14
#define SD_MISO   11
#define SD_MOSI   12
#define SD_CS      8

// Buttons (active low, internal pull-up via RTC GPIO)
#define BTN_PREV   16
#define BTN_NEXT   17
#define BTN_SELECT 18

// STEMMA QT / I2C (battery fuel gauge)
#define I2C_SDA    3
#define I2C_SCL    4
#define I2C_POWER  7   // must be driven HIGH to power the STEMMA QT connector

// Display geometry
#define EPD_W     960
#define EPD_H     552

// Paths on SD card
#define EPUB_DIR      "/epub"
#define POSITION_FILE "/katze/position.dat"

// Behaviour
#define SLEEP_TIMEOUT_MS    300000   // 5 min idle before deep sleep
#define FULL_REFRESH_EVERY  8        // partial refreshes between forced full refresh
