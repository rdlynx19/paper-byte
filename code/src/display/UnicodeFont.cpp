#include "UnicodeFont.h"

extern const uint8_t Font20_Ext_Table[];
extern const UnicodeGlyphEntry Font20_Ext_Index[];
extern const uint16_t Font20_Ext_Count;

extern const uint8_t Font24_Ext_Table[];
extern const UnicodeGlyphEntry Font24_Ext_Index[];
extern const uint16_t Font24_Ext_Count;

int utf8_decode(const char *text, int *codepoint) {
    const unsigned char *s = (const unsigned char *)text;
    unsigned char lead = s[0];

    if (lead < 0x80) { *codepoint = lead; return 1; }

    int len, cp;
    if      ((lead & 0xE0) == 0xC0) { len = 2; cp = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { len = 3; cp = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { len = 4; cp = lead & 0x07; }
    else { *codepoint = lead; return 1; } // not a valid UTF-8 lead byte

    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) { *codepoint = lead; return 1; } // truncated sequence
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *codepoint = cp;
    return len;
}

bool codepoint_is_zero_width(int codepoint) {
    switch (codepoint) {
        case 0x00AD: // soft hyphen
        case 0x200C: // zero width non-joiner
        case 0x200D: // zero width joiner
        case 0x200E: // left-to-right mark
        case 0x200F: // right-to-left mark
            return true;
        default:
            return false;
    }
}

static uint32_t row_bytes_of(sFONT *font) {
    return font->Width / 8 + (font->Width % 8 ? 1 : 0);
}

static bool find_ext_glyph(const UnicodeGlyphEntry *entries, uint16_t count,
                           int codepoint, uint16_t *out_index) {
    int lo = 0, hi = (int)count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (entries[mid].codepoint == codepoint) { *out_index = entries[mid].glyph_index; return true; }
        if (entries[mid].codepoint < (uint16_t)codepoint) lo = mid + 1; else hi = mid - 1;
    }
    return false;
}

// Returns the bitmap for `codepoint` in `font`, or nullptr if there is no
// glyph for it anywhere (caller falls back to '?').
static const uint8_t *lookup_glyph(sFONT *font, int codepoint) {
    if (codepoint >= ' ' && codepoint <= '~')
        return &font->table[(codepoint - ' ') * font->Height * row_bytes_of(font)];

    const uint8_t *ext_table;
    const UnicodeGlyphEntry *ext_index;
    uint16_t ext_count;
    if (font == &Font20)      { ext_table = Font20_Ext_Table; ext_index = Font20_Ext_Index; ext_count = Font20_Ext_Count; }
    else if (font == &Font24) { ext_table = Font24_Ext_Table; ext_index = Font24_Ext_Index; ext_count = Font24_Ext_Count; }
    else return nullptr;

    uint16_t glyph_index;
    if (!find_ext_glyph(ext_index, ext_count, codepoint, &glyph_index)) return nullptr;
    return &ext_table[glyph_index * font->Height * row_bytes_of(font)];
}

int utf8_glyph_count(const char *text) {
    int count = 0;
    while (*text) {
        int cp;
        text += utf8_decode(text, &cp);
        if (!codepoint_is_zero_width(cp)) count++;
    }
    return count;
}

void Paint_DrawString_UTF8(UWORD Xstart, UWORD Ystart, const char *text,
                           sFONT *font, UWORD Color_Foreground, UWORD Color_Background) {
    UWORD Xpoint = Xstart;
    UWORD Ypoint = Ystart;

    if (Xstart > Paint.Width || Ystart > Paint.Height) {
        Debug("Paint_DrawString_UTF8 Input exceeds the normal display range\r\n");
        return;
    }

    while (*text) {
        int codepoint;
        text += utf8_decode(text, &codepoint);
        if (codepoint_is_zero_width(codepoint)) continue;

        if ((Xpoint + font->Width) > Paint.Width) {
            Xpoint = Xstart;
            Ypoint += font->Height;
        }
        if ((Ypoint + font->Height) > Paint.Height) {
            Xpoint = Xstart;
            Ypoint = Ystart;
        }

        const uint8_t *bitmap = lookup_glyph(font, codepoint);
        if (!bitmap) bitmap = lookup_glyph(font, '?');

        // Paint_DrawChar swaps foreground/background before drawing a glyph;
        // mirror that here so callers see identical colors either way.
        Paint_DrawGlyphBitmap(Xpoint, Ypoint, bitmap, font->Width, font->Height,
                              Color_Background, Color_Foreground);

        Xpoint += font->Width;
    }
}
