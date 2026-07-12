# ESP32 E-ink Reader — Feature Checklist

Select the items you want implemented and hand this list to your coding agent.

## Core Reading Experience

- [ ] Partial refresh for page turns (avoid full black-flash refresh every page) — **deferred, revisit last**: worth retrying, but only after everything else on this list — last attempt hung the display on this specific panel, and next time should start from Waveshare's actual reference example for this panel rather than guessing at the command sequence again.
- [ ] Ghosting management (periodic full refresh every N pages to clear artifacts) — deferred alongside partial refresh (same underlying plumbing, `DisplayManager`'s `FULL_REFRESH_EVERY` counter).
- [x] Crisp font rendering tuned for e-ink (proper hinting, no reliance on LCD-style anti-aliasing) — fixed-size bitmap fonts, no anti-aliasing anywhere in the pipeline.
- [x] Reflowable text layout (word-wrap, adjustable font size, line spacing, margins) — word-wrap (`TextBlock`), margins, and a Regular/Large font-size toggle (Settings screen) all exist.
- [x] Persistent page position per book (resumes exactly where you left off, survives power loss) — `PositionStore` (SD-backed).

## File / Library Management

- [x] Book library screen (grid or list view with cached cover thumbnails)
- [x] EPUB support (unzip + parse XHTML)
- [ ] ~~Plain text (.txt) support~~ — not needed: EPUB-only by choice.
- [ ] ~~Basic PDF support~~ — not needed: EPUB-only by choice.
- [x] Metadata parsing (title/author extraction, not just filenames) — `dc:title` and `dc:creator` both parsed and shown in the library.
- [ ] ~~SD card storage with cached index~~ — not needed.

## Navigation & Input

- [ ] Low-latency page-forward/back (fastest path in the whole UI) — deferred alongside partial refresh; page turns within a chapter already don't re-parse HTML, the remaining latency is the full e-ink refresh itself.
- [x] Hierarchical settings menu (font size, sleep timeout, battery info) — no orientation or Wi-Fi option, since neither applies to this project (see below).
- [x] Table of contents / chapter jump — full screen, reachable from the reader menu, jumps to the chapter's first page.
- [ ] **Bookmarks (simple, persistent, low-friction add/remove) — wanted, next up.**

## System-Level UI

- [x] Accurate battery percentage indicator (not just voltage-guessing) — MAX17048 fuel gauge.
- [ ] **Sleep/wake static cover screen — wanted.** Currently clears to blank white before sleeping instead of showing something (e.g. the current book's cover).
- [ ] **Low battery / charging screen — wanted** (a real warning state, beyond the plain percentage already shown everywhere).
- [ ] ~~Wi-Fi/status icons~~ — not needed: no Wi-Fi hardware/use case anywhere in this project (purely offline, SD-card-based reader).
- [ ] ~~Boot screen shown immediately at power-on~~ — not needed.

## Performance-Driven Design (ESP32 + E-ink specific)

- [ ] Minimize full-screen redraws (design for small partial-refresh regions) — deferred alongside partial refresh.
- [x] Pre-render/cache current chapter pages to framebuffer (avoid re-parsing on page-back) — a chapter is fully laid out once per spine load; paging within it just re-renders from that cached layout.
- [ ] ~~Use PSRAM for framebuffer / double buffering~~ — not needed.
