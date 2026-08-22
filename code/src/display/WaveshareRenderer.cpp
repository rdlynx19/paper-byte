#include "WaveshareRenderer.h"
#include "UnicodeFont.h"

WaveshareRenderer::WaveshareRenderer(UBYTE *framebuf, int epd_w, int epd_h)
    : _epd_w(epd_w), _epd_h(epd_h)
{
    // framebuf is already set up by the caller with Paint_NewImage
    (void)framebuf;
}

int WaveshareRenderer::get_page_width() {
    return _epd_w - margin_left - margin_right;
}

int WaveshareRenderer::get_page_height() {
    return _epd_h - margin_top - margin_bottom;
}

int WaveshareRenderer::get_line_height() {
    // +4px padding. Must track _body_font(false), not a fixed Font20 — once
    // font size is a runtime setting, layout metrics that don't follow it
    // cause TextBlock to paginate against the wrong glyph size.
    return _body_font(false)->Height + 4;
}

int WaveshareRenderer::get_space_width() {
    return _body_font(false)->Width;
}

int WaveshareRenderer::get_text_width(const char *text, bool bold, bool italic) {
    (void)italic;
    return utf8_glyph_count(text) * _body_font(bold)->Width;
}

void WaveshareRenderer::draw_pixel(int x, int y, uint8_t color) {
    Paint_SetPixel(_tx(x), _ty(y), _color(color));
}

void WaveshareRenderer::draw_bitmap_1bpp(int x, int y, const uint8_t *bitmap, int width, int height) {
    // flipColor=1: Paint_DrawBitMap_Paste otherwise treats a set bit as white,
    // the opposite of every other bitmap (fonts, dithered covers) in this app.
    Paint_DrawBitMap_Paste(bitmap, _tx(x), _ty(y), width, height, 1);
}

void WaveshareRenderer::draw_text(int x, int y, const char *text, bool bold, bool italic) {
    (void)italic;
    // Paint_DrawString_UTF8 internally swaps foreground/background when calling
    // Paint_DrawGlyphBitmap, so we pass them pre-swapped here to get black on white.
    Paint_DrawString_UTF8(_tx(x), _ty(y), text, _body_font(bold), WHITE, BLACK);
}

void WaveshareRenderer::draw_text_inverted(int x, int y, const char *text, bool bold) {
    // Exact color swap of draw_text() above — flips which color ends up as
    // glyph ink vs. cell fill, giving white-on-black instead of black-on-white.
    Paint_DrawString_UTF8(_tx(x), _ty(y), text, _body_font(bold), BLACK, WHITE);
}

void WaveshareRenderer::draw_rect(int x, int y, int width, int height, uint8_t color) {
    Paint_DrawRectangle(_tx(x), _ty(y),
                        _tx(x) + width, _ty(y) + height,
                        _color(color), DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
}

void WaveshareRenderer::fill_rect(int x, int y, int width, int height, uint8_t color) {
    Paint_DrawRectangle(_tx(x), _ty(y),
                        _tx(x) + width, _ty(y) + height,
                        _color(color), DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

void WaveshareRenderer::draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) {
    Paint_DrawLine(_tx(x0), _ty(y0), _tx(x1), _ty(y1), _color(color), DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(_tx(x1), _ty(y1), _tx(x2), _ty(y2), _color(color), DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawLine(_tx(x2), _ty(y2), _tx(x0), _ty(y0), _color(color), DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

void WaveshareRenderer::fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) {
    // GUI_Paint has no fill-triangle; approximate by filling bounding box then drawing triangle outline
    int xmin = min(min(x0, x1), x2);
    int ymin = min(min(y0, y1), y2);
    int xmax = max(max(x0, x1), x2);
    int ymax = max(max(y0, y1), y2);
    fill_rect(xmin, ymin, xmax - xmin, ymax - ymin, color);
    // overdraw background around the triangle would require a proper scan-fill;
    // for the e-ink reader use-case (UI arrows) this bounding-box fill is fine.
}

void WaveshareRenderer::draw_circle(int x, int y, int r, uint8_t color) {
    Paint_DrawCircle(_tx(x), _ty(y), r, _color(color), DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
}

void WaveshareRenderer::fill_circle(int x, int y, int r, uint8_t color) {
    Paint_DrawCircle(_tx(x), _ty(y), r, _color(color), DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

void WaveshareRenderer::clear_screen() {
    Paint_Clear(WHITE);
}
