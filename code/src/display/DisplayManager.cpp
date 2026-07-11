#include "DisplayManager.h"
#include "EPD_5in0.h"
#include "../DEV_Config.h"

void DisplayManager::begin() {
    DEV_Module_Init();
    EPD_5in0_Init();
    EPD_5in0_Clear();
}

void DisplayManager::wake() {
    DEV_Module_Init();
    EPD_5in0_Init();
    // No Clear — e-ink panel retains its image; next showPage() restores controller RAM.
}

void DisplayManager::sleep() {
    EPD_5in0_Init();
    EPD_5in0_Clear();
    EPD_5in0_Sleep();
    DEV_Module_Exit();
}

void DisplayManager::showPage(const uint8_t *buf, bool force_full) {
    bool do_full = force_full
                   || !_last_was_partial
                   || _partial_count >= FULL_REFRESH_EVERY;

    if (do_full) {
        _full_refresh(buf);
        _partial_count   = 0;
        _last_was_partial = false;
    } else {
        _partial_refresh(buf);
        _partial_count++;
        _last_was_partial = true;
    }
}

void DisplayManager::_full_refresh(const uint8_t *buf) {
    EPD_5in0_Init();
    EPD_5in0_Display((UBYTE *)buf);
}

void DisplayManager::_partial_refresh(const uint8_t *buf) {
    EPD_5in0_Init();
    EPD_5in0_Display_Partial((UBYTE *)buf, 0, 0, EPD_W, EPD_H);
}
