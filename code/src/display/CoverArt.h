#pragma once
#include <stdint.h>

class Epub;

// Thumbnail is packed 1bpp, MSB-first, row-padded to a whole byte (both
// dimensions are already byte-aligned so no padding is actually needed).
#define COVER_THUMB_W         160
#define COVER_THUMB_H         224
#define COVER_THUMB_ROW_BYTES (COVER_THUMB_W / 8)
#define COVER_THUMB_BYTES     (COVER_THUMB_ROW_BYTES * COVER_THUMB_H)

// Fills `out` (a caller-owned COVER_THUMB_BYTES buffer) with `epub`'s cover,
// dithered to 1bpp at COVER_THUMB_W x COVER_THUMB_H. Decoded thumbnails are
// cached to SD keyed by epub path, so this only actually decodes a JPEG once
// per book. Returns false (leaving `out` untouched) if the epub has no
// declared cover, or its cover isn't a JPEG — callers should just skip
// drawing a thumbnail in that case rather than treating it as an error.
bool get_cover_thumbnail(Epub *epub, uint8_t *out);
