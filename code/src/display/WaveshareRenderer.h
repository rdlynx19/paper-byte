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
    void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false) override;
    void draw_rect(int x, int y, int width, int height, uint8_t color = 0) override;
    void fill_rect(int x, int y, int width, int height, uint8_t color = 0) override;
    void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) override;
    void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) override;
    void draw_circle(int x, int y, int r, uint8_t color = 0) override;
    void fill_circle(int x, int y, int r, uint8_t color = 0) override;
    void clear_screen() override;
    bool has_gray() override { return false; }

private:
    int _epd_w;
    int _epd_h;
    // translate content-area-relative coords to display coords
    inline int _tx(int x) { return margin_left + x; }
    inline int _ty(int y) { return margin_top  + y; }
    inline UWORD _color(uint8_t c) { return c ? BLACK : WHITE; }
};
