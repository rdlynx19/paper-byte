#include "DisplayManager.h"
#include "EPD_5in0.h"
#include "../DEV_Config.h"

void DisplayManager::begin() {
    DEV_Module_Init();
    EPD_5in0_Init();
    EPD_5in0_Clear();
    _has_shown_full = false;
}

void DisplayManager::wake() {
    DEV_Module_Init();
    EPD_5in0_Init();
    // No Clear — e-ink panel retains its image; next showPage() restores controller RAM.
    _has_shown_full = false;
}

void DisplayManager::sleep(bool clear_first) {
    EPD_5in0_Init();
    if (clear_first) EPD_5in0_Clear();
    EPD_5in0_Sleep();
    DEV_Module_Exit();
}

void DisplayManager::showPage(const uint8_t *buf, bool force_full) {
    bool do_full = force_full || !_has_shown_full || _partial_count >= FULL_REFRESH_EVERY;

    if (do_full) {
        _full_refresh(buf);
        _partial_count   = 0;
        _has_shown_full  = true;
    } else {
        _partial_refresh(buf);
        _partial_count++;
    }
}

void DisplayManager::_full_refresh(const uint8_t *buf) {
    EPD_5in0_Init();
    EPD_5in0_Display((UBYTE *)buf);
}

void DisplayManager::_partial_refresh(const uint8_t *buf) {
    // EPD_5in0_Display_Partial() does its own hardware reset internally,
    // which wipes the controller's booster/gate-count/data-entry-mode
    // registers back to power-on defaults — but it never re-sends those
    // three commands itself, only EPD_5in0_Init() does. Skipping this call
    // leaves the controller unconfigured and nothing displays correctly.
    EPD_5in0_Init();
    EPD_5in0_Display_Partial((UBYTE *)buf, 0, 0, EPD_W, EPD_H);
}
