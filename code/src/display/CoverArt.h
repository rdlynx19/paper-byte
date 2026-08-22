#pragma once
#include <stdint.h>
#include <string>

class Epub;

// Bitmaps are packed 1bpp, MSB-first, row-padded to a whole byte. All sizes
// used here have byte-aligned widths so no padding math is actually needed.

// Library carousel's small previous/next preview size. Only consumer is
// the carousel's side previews (render_cover_thumbnail() in code.ino) —
// kept deliberately small so most of the 492px content width goes to
// COVER_FEATURED below.
#define COVER_THUMB_W         88
#define COVER_THUMB_H         124
#define COVER_THUMB_ROW_BYTES (COVER_THUMB_W / 8)
#define COVER_THUMB_BYTES     (COVER_THUMB_ROW_BYTES * COVER_THUMB_H)

// Library carousel's large "current book" cover — same ~1.4 aspect ratio
// as COVER_THUMB, sized to sit between two COVER_THUMB-sized side previews
// within the 492px content width (88 + 18 + 280 + 18 + 88 = 492, an exact
// fit at the default margins).
#define COVER_FEATURED_W         280
#define COVER_FEATURED_H         392
#define COVER_FEATURED_ROW_BYTES (COVER_FEATURED_W / 8)
#define COVER_FEATURED_BYTES     (COVER_FEATURED_ROW_BYTES * COVER_FEATURED_H)

// Sleep/wake cover screen size — fills the panel's portrait content area
// (492 x 880 at the default margins; 488 is the nearest byte-aligned width),
// full-bleed rather than matching the thumbnail's aspect ratio, since the
// cover art itself already carries the title — no separate text needed.
#define COVER_SLEEP_W         488
#define COVER_SLEEP_H         880
#define COVER_SLEEP_ROW_BYTES (COVER_SLEEP_W / 8)
#define COVER_SLEEP_BYTES     (COVER_SLEEP_ROW_BYTES * COVER_SLEEP_H)

// Fills `out` (a caller-owned width/height-sized buffer, row_bytes = width/8)
// with `epub`'s cover, dithered to 1bpp at the given size. Decoded bitmaps
// are cached to SD keyed by epub path *and* size, so repeated calls at the
// same size only decode once. Supports JPEG and PNG covers (the two formats
// epub covers actually show up in in practice). Returns false (leaving `out`
// untouched) if the epub has no declared cover, or its cover is some other
// format — callers should just skip drawing it in that case rather than
// treating it as an error.
bool get_cover_bitmap(Epub *epub, uint8_t *out, int width, int height);

// Convenience wrapper for the library carousel's side-preview size.
inline bool get_cover_thumbnail(Epub *epub, uint8_t *out) {
    return get_cover_bitmap(epub, out, COVER_THUMB_W, COVER_THUMB_H);
}

// Convenience wrapper for the library carousel's featured-cover size.
inline bool get_cover_featured(Epub *epub, uint8_t *out) {
    return get_cover_bitmap(epub, out, COVER_FEATURED_W, COVER_FEATURED_H);
}

// Convenience wrapper for the sleep/wake cover screen size.
inline bool get_cover_sleep_bitmap(Epub *epub, uint8_t *out) {
    return get_cover_bitmap(epub, out, COVER_SLEEP_W, COVER_SLEEP_H);
}

// Removes any cached cover bitmap(s) for `epub_path` (thumbnail, featured,
// and sleep-screen sizes) — call this when the underlying epub file itself
// is deleted, so a future, different book that happens to land on the same
// path can't ever read back a stale cache entry.
void invalidate_cover_cache(const std::string &epub_path);
