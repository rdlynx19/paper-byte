# ESP32 E-ink Reader — Feature Checklist

Select the items you want implemented and hand this list to your coding agent.

## Core Reading Experience

- [ ] Partial refresh for page turns (avoid full black-flash refresh every page) — *the driver-level plumbing exists (`DisplayManager::_partial_refresh`, `EPD_5in0_Display_Partial`), but every call site in `code.ino` passes `force_full=true`, so in practice every page turn still does a full refresh.*
- [ ] Ghosting management (periodic full refresh every N pages to clear artifacts) — *same story: `FULL_REFRESH_EVERY` and the partial-refresh counter are implemented in `DisplayManager`, just unused since partial refresh is never actually triggered.*
- [x] Crisp font rendering tuned for e-ink (proper hinting, no reliance on LCD-style anti-aliasing) — fixed-size bitmap fonts, no anti-aliasing anywhere in the pipeline.
- [ ] Reflowable text layout (word-wrap, adjustable font size, line spacing, margins) — *word-wrap (`TextBlock`'s line-breaking) and margins exist; there's no user-facing font-size setting.*
- [x] Persistent page position per book (resumes exactly where you left off, survives power loss) — `PositionStore` (SD-backed).

## File / Library Management

- [x] Book library screen (grid or list view with cached cover thumbnails)
- [x] EPUB support (unzip + parse XHTML)
- [ ] Plain text (.txt) support
- [ ] Basic PDF support (or pre-converted format pipeline)
- [ ] Metadata parsing (title/author extraction, not just filenames) — *title extraction from `dc:title` is done; author (`dc:creator`) is never parsed.*
- [ ] SD card storage with cached index (avoid re-scanning on every boot) — *book list/position is cached to SD, but `scan_for_epubs()` still walks the SD directory on every boot to find new files (just skips re-parsing already-known ones).*

## Navigation & Input

- [ ] Low-latency page-forward/back (fastest path in the whole UI) — *page turns within a chapter don't re-parse HTML (see below), but every turn still forces a full e-ink refresh, which dominates perceived latency.*
- [ ] Hierarchical settings menu (font size, orientation, sleep timeout, Wi-Fi, battery info) — only a 3-item menu exists (Continue Reading / Go to Library / Sleep).
- [ ] Table of contents / chapter jump — *`Epub` already parses the full TOC (`m_toc`, `get_toc_item`), but there's no screen to browse/jump via it.*
- [ ] Bookmarks (simple, persistent, low-friction add/remove)

## System-Level UI

- [x] Accurate battery percentage indicator (not just voltage-guessing) — MAX17048 fuel gauge.
- [ ] Sleep/wake static cover screen (leverages e-ink's bistable, zero-power display hold) — *the bistable zero-power hold is used correctly, but the screen is cleared to blank white before sleeping, not a cover/graphic.*
- [ ] Low battery / charging screens
- [ ] Wi-Fi/status icons (if doing OTA updates or content downloads) — no Wi-Fi in the project at all currently.
- [ ] Boot screen shown immediately at power-on — panel just holds whatever was last on it until the library screen finishes rendering.

## Performance-Driven Design (ESP32 + E-ink specific)

- [ ] Minimize full-screen redraws (design for small partial-refresh regions) — *contradicted by current behavior: every redraw is a full-screen refresh (see partial refresh note above).*
- [x] Pre-render/cache current chapter pages to framebuffer (avoid re-parsing on page-back) — a chapter is fully laid out once per spine load; paging within it just re-renders from that cached layout.
- [ ] Use PSRAM for framebuffer / double buffering (on ESP32-S3 or similar variants) — *framebuffer is PSRAM-allocated (`ps_malloc`), but there's a single buffer, not double-buffered.*
