#pragma once
#include <stdint.h>
#include "../config.h"

class DisplayManager {
public:
    void begin();
    void wake();   // re-init after deep sleep without clearing the panel
    void sleep();

    // call after every page change — handles full/partial decision automatically
    void showPage(const uint8_t *buf, bool force_full = false);

    // force a full refresh on the next showPage call (e.g. on chapter boundary)
    void invalidate() { _partial_count = FULL_REFRESH_EVERY; }

private:
    int  _partial_count   = 0;
    bool _last_was_partial = false;

    void _full_refresh(const uint8_t *buf);
    void _partial_refresh(const uint8_t *buf);
};
