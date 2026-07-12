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
    int  _partial_count  = 0;
    // A partial refresh is a diff against whatever the controller's RAM
    // already holds from a previous full refresh. Right after begin()/wake()
    // there is no such baseline yet, so the first showPage() of a session
    // must always be full — this flag guarantees that regardless of what
    // callers pass, without relying on assumptions about the vendor driver's
    // internal RAM-buffer semantics.
    bool _has_shown_full = false;

    void _full_refresh(const uint8_t *buf);
    void _partial_refresh(const uint8_t *buf);
};
