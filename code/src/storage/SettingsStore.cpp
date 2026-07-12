#include "SettingsStore.h"
#include <stdio.h>
#include <string.h>
#include "../config.h"
#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(tag, ...) printf(__VA_ARGS__); printf("\n")
#define ESP_LOGE(tag, ...) printf(__VA_ARGS__); printf("\n")
#endif

static const char *TAG = "SETTINGS";

const char    *SettingsStore::FILE_PATH = "/sd/katze.cfg";
const uint32_t SettingsStore::MAGIC     = 0x4B415453; // 'KATS'
const uint8_t  SettingsStore::VERSION   = 0x01;

// ---------------------------------------------------------------------------
// On-disk layout (little-endian):
//   4B  magic
//   1B  version
//   4B  sleep_timeout_ms
//   1B  large_font
// ---------------------------------------------------------------------------

static void defaults(ReaderSettings *settings) {
    settings->sleep_timeout_ms = SLEEP_TIMEOUT_MS;
    settings->large_font       = false;
}

void SettingsStore::load(ReaderSettings *settings) {
    defaults(settings);

    FILE *f = fopen(FILE_PATH, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No settings file, using defaults");
        return;
    }

    uint32_t magic = 0;
    uint8_t  ver   = 0;
    uint32_t timeout = 0;
    uint8_t  large   = 0;
    bool ok = fread(&magic, 4, 1, f) == 1 && magic == MAGIC
           && fread(&ver, 1, 1, f) == 1 && ver == VERSION
           && fread(&timeout, 4, 1, f) == 1
           && fread(&large, 1, 1, f) == 1;
    fclose(f);

    if (!ok) {
        ESP_LOGE(TAG, "Settings file corrupt — using defaults");
        defaults(settings);
        return;
    }

    settings->sleep_timeout_ms = timeout;
    settings->large_font       = large != 0;
    ESP_LOGI(TAG, "Loaded settings: timeout=%lums large_font=%d",
             (unsigned long)timeout, (int)settings->large_font);
}

bool SettingsStore::save(const ReaderSettings &settings) {
    FILE *f = fopen(FILE_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for write", FILE_PATH);
        return false;
    }

    uint8_t large = settings.large_font ? 1 : 0;
    bool ok = fwrite(&MAGIC, 4, 1, f) == 1
           && fwrite(&VERSION, 1, 1, f) == 1
           && fwrite(&settings.sleep_timeout_ms, 4, 1, f) == 1
           && fwrite(&large, 1, 1, f) == 1
           && fflush(f) == 0;
    fclose(f);

    if (!ok) ESP_LOGE(TAG, "Write error");
    return ok;
}
