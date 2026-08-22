#pragma once
#include <stddef.h>
#include <stdlib.h>

// Routes the memory-heavy parts of a chapter's parsed/laid-out data
// (TextBlock/ImageBlock, Page, PageLine/PageImage, and each paragraph's
// copied span text) onto PSRAM instead of the small internal-DRAM heap
// that ESP.getFreeHeap() reports. A single dense chapter's worth of these
// — one heap object per paragraph, per line, per page, plus every
// paragraph's actual text copied into its own buffer — can add up to
// several hundred KB, which internal DRAM alone (shared with the WiFi/BT
// stack, FreeRTOS, and everything else) doesn't have room for. The 2MB of
// PSRAM this board has sits almost entirely unused otherwise; only the
// framebuffer and a couple of cover-art bitmaps (code.ino, CoverArt.cpp)
// currently use it via ps_malloc().
//
// Falls back to regular malloc() if PSRAM allocation fails (fragmented or
// genuinely exhausted) rather than returning null outright — internal RAM
// might still have room even when PSRAM doesn't.
#ifndef UNIT_TEST
#include <esp32-hal-psram.h>
inline void *model_alloc(size_t sz) {
    void *p = ps_malloc(sz);
    return p ? p : malloc(sz);
}
#else
// Host-side/unit-test build: no PSRAM, no ESP32 SDK — plain malloc.
inline void *model_alloc(size_t sz) {
    return malloc(sz);
}
#endif
