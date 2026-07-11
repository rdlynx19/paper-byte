#pragma once
#include <stdint.h>
#include "fonts.h"
#include "GUI_Paint.h"

// Sparse codepoint -> glyph-index map for the characters beyond plain ASCII
// (0x20-0x7E) that a font's extension table has bitmaps for. Entries are
// sorted ascending by codepoint so lookup can binary-search.
struct UnicodeGlyphEntry {
    uint16_t codepoint;
    uint16_t glyph_index;
};

// Decodes one UTF-8 codepoint starting at `text`. Always returns >=1 so a
// caller advancing by the return value can't get stuck on malformed input;
// an invalid/truncated sequence decodes as its raw lead byte.
int utf8_decode(const char *text, int *codepoint);

// True for codepoints that must never occupy a glyph cell — soft hyphen,
// zero-width (non-)joiners, direction marks. These come out of
// replace_html_entities() but have no visual representation in a fixed-width
// bitmap font, so they're skipped rather than drawn as a fallback glyph.
bool codepoint_is_zero_width(int codepoint);

// Number of glyph cells `text` occupies once rendered (UTF-8 aware; each
// codepoint is one cell in this fixed-width font, zero-width ones are 0).
// Multiply by a font's Width for pixel width — this is the UTF-8-safe
// replacement for strlen(text) in width calculations.
int utf8_glyph_count(const char *text);

// UTF-8-aware replacement for Paint_DrawString_EN. Any codepoint with no
// bitmap in `font`'s extension table (see UnicodeFontData.cpp) falls back to
// '?' instead of reading garbage out of the font table.
void Paint_DrawString_UTF8(UWORD Xstart, UWORD Ystart, const char *text,
                           sFONT *font, UWORD Color_Foreground, UWORD Color_Background);
