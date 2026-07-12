#pragma once
#include <stdint.h>

class Epub;

// Bitmaps are packed 1bpp, MSB-first, row-padded to a whole byte. All sizes
// used here have byte-aligned widths so no padding math is actually needed.

// Library grid tile size (2 columns x 4 rows).
#define COVER_THUMB_W         120
#define COVER_THUMB_H         168
#define COVER_THUMB_ROW_BYTES (COVER_THUMB_W / 8)
#define COVER_THUMB_BYTES     (COVER_THUMB_ROW_BYTES * COVER_THUMB_H)

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

// Convenience wrapper for the library grid's tile size.
inline bool get_cover_thumbnail(Epub *epub, uint8_t *out) {
    return get_cover_bitmap(epub, out, COVER_THUMB_W, COVER_THUMB_H);
}

// Convenience wrapper for the sleep/wake cover screen size.
inline bool get_cover_sleep_bitmap(Epub *epub, uint8_t *out) {
    return get_cover_bitmap(epub, out, COVER_SLEEP_W, COVER_SLEEP_H);
}
