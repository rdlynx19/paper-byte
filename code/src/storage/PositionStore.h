#pragma once
#include <stdint.h>
#include "../model/State.h"

class PositionStore {
public:
    // Read persisted state from SD. Fills *state on success.
    // Returns false (and zero-initialises *state) if file missing or corrupt.
    static bool load(EpubListState *state);

    // Write state to SD. Returns true on success.
    static bool save(const EpubListState &state);

    // Find the saved position for epub at `path`.
    // Returns false if no entry exists; sets *section and *page on success.
    static bool get_epub_position(const EpubListState &state, const char *path,
                                  uint16_t *section, uint16_t *page);

    // Insert or update the position entry for `path` / `title`.
    static void set_epub_position(EpubListState &state,
                                  const char *path, const char *title,
                                  uint16_t section, uint16_t page,
                                  uint16_t pages_in_section);

    static const char *FILE_PATH;

private:
    static const uint32_t MAGIC;
    static const uint8_t  VERSION;
};
