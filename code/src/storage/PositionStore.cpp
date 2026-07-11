#include "PositionStore.h"
#include <stdio.h>
#include <string.h>
#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(tag, ...) printf(__VA_ARGS__); printf("\n")
#define ESP_LOGE(tag, ...) printf(__VA_ARGS__); printf("\n")
#define ESP_LOGW(tag, ...) printf(__VA_ARGS__); printf("\n")
#endif

static const char *TAG = "POS";

const char    *PositionStore::FILE_PATH = "/sd/katze.pos";
const uint32_t PositionStore::MAGIC     = 0x4B415451; // 'KATQ'
const uint8_t  PositionStore::VERSION   = 0x01;

// ---------------------------------------------------------------------------
// On-disk layout (little-endian, field-by-field — no struct padding issues):
//
//   4B  magic
//   1B  version
//   1B  num_epubs
//   For each epub entry:
//     1B  path_len  + path_len bytes  (no null)
//     1B  title_len + title_len bytes (no null)
//     2B  current_section
//     2B  current_page
//     2B  pages_in_current_section
// ---------------------------------------------------------------------------

static bool w8 (FILE *f, uint8_t  v) { return fwrite(&v, 1, 1, f) == 1; }
static bool w16(FILE *f, uint16_t v) { return fwrite(&v, 2, 1, f) == 1; }
static bool w32(FILE *f, uint32_t v) { return fwrite(&v, 4, 1, f) == 1; }

static bool r8 (FILE *f, uint8_t  *v) { return fread(v, 1, 1, f) == 1; }
static bool r16(FILE *f, uint16_t *v) { return fread(v, 2, 1, f) == 1; }
static bool r32(FILE *f, uint32_t *v) { return fread(v, 4, 1, f) == 1; }

bool PositionStore::load(EpubListState *state) {
    memset(state, 0, sizeof(*state));

    FILE *f = fopen(FILE_PATH, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No state file");
        return false;
    }

    uint32_t magic = 0;
    uint8_t  ver   = 0;
    uint8_t  num   = 0;

    if (!r32(f, &magic) || magic != MAGIC) {
        ESP_LOGE(TAG, "Bad magic");
        goto fail;
    }
    if (!r8(f, &ver) || ver != VERSION) {
        ESP_LOGE(TAG, "Bad version %d", ver);
        goto fail;
    }
    if (!r8(f, &num)) goto fail;
    if (num > MAX_EPUB_LIST_SIZE) num = MAX_EPUB_LIST_SIZE;

    for (int i = 0; i < (int)num; i++) {
        EpubListItem *it = &state->epub_list[i];

        uint8_t plen = 0, tlen = 0;
        if (!r8(f, &plen)) goto fail;
        if (plen > 0 && fread(it->path, 1, plen, f) != plen) goto fail;
        it->path[plen] = '\0';

        if (!r8(f, &tlen)) goto fail;
        if (tlen > 0 && fread(it->title, 1, tlen, f) != tlen) goto fail;
        it->title[tlen] = '\0';

        uint16_t sec = 0, pg = 0, pgs = 0;
        if (!r16(f, &sec) || !r16(f, &pg) || !r16(f, &pgs)) goto fail;
        it->current_section          = sec;
        it->current_page             = pg;
        it->pages_in_current_section = pgs;
    }

    fclose(f);
    state->num_epubs = num;
    state->is_loaded = true;
    ESP_LOGI(TAG, "Loaded %d entries", num);
    return true;

fail:
    fclose(f);
    ESP_LOGE(TAG, "State file corrupt — resetting");
    memset(state, 0, sizeof(*state));
    return false;
}

bool PositionStore::save(const EpubListState &state) {
    FILE *f = fopen(FILE_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for write", FILE_PATH);
        return false;
    }

    int num = state.num_epubs;
    if (num > MAX_EPUB_LIST_SIZE) num = MAX_EPUB_LIST_SIZE;

    bool ok = w32(f, MAGIC) && w8(f, VERSION) && w8(f, (uint8_t)num);

    for (int i = 0; i < num && ok; i++) {
        const EpubListItem &it = state.epub_list[i];

        uint8_t plen = (uint8_t)strnlen(it.path,  MAX_PATH_SIZE  - 1);
        uint8_t tlen = (uint8_t)strnlen(it.title, MAX_TITLE_SIZE - 1);

        ok = ok && w8(f, plen) && fwrite(it.path,  1, plen, f) == plen;
        ok = ok && w8(f, tlen) && fwrite(it.title, 1, tlen, f) == tlen;
        ok = ok && w16(f, it.current_section)
                && w16(f, it.current_page)
                && w16(f, it.pages_in_current_section);
    }

    ok = ok && (fflush(f) == 0);
    fclose(f);

    if (ok) ESP_LOGI(TAG, "Saved %d entries", num);
    else    ESP_LOGE(TAG, "Write error");
    return ok;
}

bool PositionStore::get_epub_position(const EpubListState &state, const char *path,
                                      uint16_t *section, uint16_t *page) {
    for (int i = 0; i < state.num_epubs; i++) {
        if (strncmp(state.epub_list[i].path, path, MAX_PATH_SIZE) == 0) {
            *section = state.epub_list[i].current_section;
            *page    = state.epub_list[i].current_page;
            ESP_LOGI(TAG, "Restored %s → section=%d page=%d", path, *section, *page);
            return true;
        }
    }
    return false;
}

void PositionStore::set_epub_position(EpubListState &state,
                                      const char *path, const char *title,
                                      uint16_t section, uint16_t page,
                                      uint16_t pages_in_section) {
    for (int i = 0; i < state.num_epubs; i++) {
        if (strncmp(state.epub_list[i].path, path, MAX_PATH_SIZE) == 0) {
            state.epub_list[i].current_section          = section;
            state.epub_list[i].current_page             = page;
            state.epub_list[i].pages_in_current_section = pages_in_section;
            return;
        }
    }
    if (state.num_epubs >= MAX_EPUB_LIST_SIZE) {
        ESP_LOGW(TAG, "Epub list full, discarding position for %s", path);
        return;
    }
    EpubListItem &it = state.epub_list[state.num_epubs++];
    strncpy(it.path,  path,  MAX_PATH_SIZE  - 1); it.path[MAX_PATH_SIZE  - 1]  = '\0';
    strncpy(it.title, title, MAX_TITLE_SIZE - 1); it.title[MAX_TITLE_SIZE - 1] = '\0';
    it.current_section          = section;
    it.current_page             = page;
    it.pages_in_current_section = pages_in_section;
}
