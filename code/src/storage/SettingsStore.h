#pragma once
#include <stdint.h>

// Reader settings adjustable from the settings screen. Kept separate from
// PositionStore's file (different lifecycle: settings change rarely and
// don't grow with the library, so there's no reason to share a format/version
// with the per-book position list).
struct ReaderSettings {
    uint32_t sleep_timeout_ms;
    bool     large_font;
};

class SettingsStore {
public:
    // Fills *settings with saved values, or defaults if no file exists yet
    // (or it's corrupt). Always succeeds in the sense that *settings is
    // always left in a usable state.
    static void load(ReaderSettings *settings);

    // Returns true on success.
    static bool save(const ReaderSettings &settings);

    static const char *FILE_PATH;

private:
    static const uint32_t MAGIC;
    static const uint8_t  VERSION;
};
