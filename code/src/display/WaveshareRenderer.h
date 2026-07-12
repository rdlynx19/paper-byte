#pragma once
#include "Renderer.h"
#include "GUI_Paint.h"
#include "fonts.h"

class WaveshareRenderer : public Renderer {
public:
    WaveshareRenderer(UBYTE *framebuf, int epd_w, int epd_h);

    int get_page_width()  override;
    int get_page_height() override;
    int get_line_height() override;
    int get_space_width() override;
    int get_text_width(const char *text, bool bold = false, bool italic = false) override;

    void draw_pixel(int x, int y, uint8_t color) override;
    void draw_bitmap_1bpp(int x, int y, const uint8_t *bitmap, int width, int height) override;
    void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false) override;
    void draw_rect(int x, int y, int width, int height, uint8_t color = 0) override;
    void fill_rect(int x, int y, int width, int height, uint8_t color = 0) override;
    void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) override;
    void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) override;
    void draw_circle(int x, int y, int r, uint8_t color = 0) override;
    void fill_circle(int x, int y, int r, uint8_t color = 0) override;
    void clear_screen() override;
    bool has_gray() override { return false; }

    // Reader-facing font size toggle. When on, body text renders at Font24
    // instead of Font20 — reusing the existing bold-heading font rather than
    // needing a third font table. Trade-off: while this is on, real
    // bold/emphasis spans (which also render as Font24) stop being visually
    // distinguishable from regular text.
    void set_large_font(bool large) { _large_font = large; }

private:
    int  _epd_w;
    int  _epd_h;
    bool _large_font = false;
    inline sFONT *_body_font(bool bold) { return (bold || _large_font) ? &Font24 : &Font20; }
    // translate content-area-relative coords to display coords
    inline int _tx(int x) { return margin_left + x; }
    inline int _ty(int y) { return margin_top  + y; }
    inline UWORD _color(uint8_t c) { return c ? BLACK : WHITE; }
};
