#pragma once
#include <string>

#define MAX_WORD_LENGTH 100

class Epub;

class Renderer {
protected:
    int margin_top    = 40;
    int margin_bottom = 40;
    int margin_left   = 30;
    int margin_right  = 30;

public:
    virtual ~Renderer() {}

    // layout metrics
    virtual int get_page_width()  = 0;
    virtual int get_page_height() = 0;
    virtual int get_line_height() = 0;
    virtual int get_space_width() = 0;
    virtual int get_text_width(const char *text, bool bold = false, bool italic = false) = 0;

    // drawing
    virtual void draw_pixel(int x, int y, uint8_t color) = 0;
    virtual void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false) = 0;
    virtual void draw_rect(int x, int y, int width, int height, uint8_t color = 0) = 0;
    virtual void fill_rect(int x, int y, int width, int height, uint8_t color = 0) = 0;
    virtual void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) = 0;
    virtual void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) = 0;
    virtual void draw_circle(int x, int y, int r, uint8_t color = 0) = 0;
    virtual void fill_circle(int x, int y, int r, uint8_t color = 0) = 0;
    virtual void clear_screen() = 0;

    // image support (stub is fine — draws a placeholder rectangle)
    virtual void draw_image(const std::string &filename, const uint8_t *data, size_t data_size,
                            int x, int y, int width, int height) {
        draw_rect(x, y, width, height, 0);
    }
    virtual bool get_image_size(const std::string &filename, const uint8_t *data, size_t data_size,
                                int *width, int *height) {
        *width = get_page_width() / 2;
        *height = get_page_height() / 4;
        return false;
    }

    // optional display operations
    virtual void flush_display() {}
    virtual void flush_area(int x, int y, int width, int height) {}
    virtual void needs_gray(uint8_t color) {}
    virtual bool has_gray() { return false; }
    virtual void show_busy() {}
    virtual void show_img(int x, int y, int width, int height, const uint8_t *img_buffer) {}
    virtual void reset() {}
    virtual bool dehydrate() { return false; }
    virtual bool hydrate()   { return false; }

    // non-pure: default word-wrap implementation
    virtual void draw_text_box(const std::string &text, int x, int y, int width, int height,
                               bool bold = false, bool italic = false) {
        int length = text.length();
        int start = 0, end = 1, ypos = 0;
        while (start < length && ypos + get_line_height() < height) {
            while (end < length &&
                   get_text_width(text.substr(start, end - start).c_str(), bold, italic) < width)
                end++;
            if (get_text_width(text.substr(start, end - start).c_str(), bold, italic) > width)
                end--;
            draw_text(x, y + ypos, text.substr(start, end - start).c_str(), bold, italic);
            ypos += get_line_height();
            start = end;
            end = start + 1;
        }
    }

    void set_margin_top(int v)    { margin_top    = v; }
    void set_margin_bottom(int v) { margin_bottom = v; }
    void set_margin_left(int v)   { margin_left   = v; }
    void set_margin_right(int v)  { margin_right  = v; }

    uint8_t temperature = 20;
};
