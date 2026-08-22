#include "CoverArt.h"
#include "../epub/Epub.h"
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(tag, ...) printf(__VA_ARGS__); printf("\n")
#define ESP_LOGE(tag, ...) printf(__VA_ARGS__); printf("\n")
#endif

static const char *TAG = "COVER";

// Decode-time context threaded through JPEGDEC's draw callback via pUser —
// a full-resolution (or scaled-down, see decode_jpeg_cover) grayscale buffer
// that MCU blocks get written into as they're decoded.
struct DecodeCtx {
    uint8_t *gray;
    int      width;
    int      height;
};

static int jpeg_draw_callback(JPEGDRAW *draw) {
    DecodeCtx *ctx = (DecodeCtx *)draw->pUser;
    for (int y = 0; y < draw->iHeight; y++) {
        int dst_y = draw->y + y;
        if (dst_y < 0 || dst_y >= ctx->height) continue;
        uint16_t *src_row = draw->pPixels + y * draw->iWidth;
        uint8_t  *dst_row = ctx->gray + (size_t)dst_y * ctx->width;
        for (int x = 0; x < draw->iWidth; x++) {
            int dst_x = draw->x + x;
            if (dst_x < 0 || dst_x >= ctx->width) continue;
            // Six green bits track perceived brightness closely enough for a
            // dithered thumbnail — same approximation JPEGDEC's own e-paper
            // example uses, and it avoids a full RGB565 decompose.
            dst_row[dst_x] = (uint8_t)(((src_row[x] & 0x7E0) >> 5) << 2);
        }
    }
    return 1;
}

// Box-downsamples `src` (sw x sh, 1 byte/pixel) into `dst` (dw x dh).
static void box_resize(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int sy0 = y * sh / dh, sy1 = (y + 1) * sh / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; x++) {
            int sx0 = x * sw / dw, sx1 = (x + 1) * sw / dw;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t sum = 0;
            int n = 0;
            for (int sy = sy0; sy < sy1 && sy < sh; sy++)
                for (int sx = sx0; sx < sx1 && sx < sw; sx++) { sum += src[(size_t)sy * sw + sx]; n++; }
            dst[y * dw + x] = n ? (uint8_t)(sum / n) : 255;
        }
    }
}

// Floyd-Steinberg dither `gray` (w x h, 1 byte/pixel; modified in place as
// error accumulates) into `out` (packed 1bpp, MSB-first, bit=1 -> black).
static void dither_to_1bpp(uint8_t *gray, int w, int h, uint8_t *out) {
    int row_bytes = w / 8;
    memset(out, 0, (size_t)row_bytes * h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int old_val = gray[y * w + x];
            bool black  = old_val < 128;
            if (black) out[y * row_bytes + x / 8] |= (0x80 >> (x % 8));
            int new_val = black ? 0 : 255;
            int err     = old_val - new_val;

            auto spread = [&](int dx, int dy, int num, int den) {
                int xx = x + dx, yy = y + dy;
                if (xx < 0 || xx >= w || yy < 0 || yy >= h) return;
                int v = gray[yy * w + xx] + err * num / den;
                gray[yy * w + xx] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
            };
            spread(1, 0, 7, 16);
            spread(-1, 1, 3, 16);
            spread(0, 1, 5, 16);
            spread(1, 1, 1, 16);
        }
    }
}

static bool decode_jpeg_cover(const uint8_t *jpg, size_t jpg_size, uint8_t *out_1bpp, int out_w, int out_h) {
    // JPEGDEC's internal JPEGIMAGE struct holds its Huffman/quantization
    // tables and MCU buffers inline (tens of KB) — as a plain stack local
    // that blows the loop task's stack. static keeps it out of the stack
    // entirely; decode calls are never concurrent/re-entrant here.
    static JPEGDEC jpeg;
    if (!jpeg.openRAM((uint8_t *)jpg, (int)jpg_size, jpeg_draw_callback)) {
        ESP_LOGE(TAG, "Not a JPEG or corrupt cover image");
        return false;
    }

    int src_w = jpeg.getWidth(), src_h = jpeg.getHeight();

    // Pick the coarsest built-in downscale that still leaves us at least
    // target-sized, so we don't decode (and allocate) a multi-megapixel
    // image just to shrink it back down a moment later.
    int scale = 0; // 0 == full size
    int dw = src_w, dh = src_h;
    while (scale < JPEG_SCALE_EIGHTH && dw / 2 >= out_w && dh / 2 >= out_h) {
        dw /= 2; dh /= 2;
        scale = scale == 0 ? JPEG_SCALE_HALF
              : scale == JPEG_SCALE_HALF ? JPEG_SCALE_QUARTER
              : JPEG_SCALE_EIGHTH;
    }

    DecodeCtx ctx;
    ctx.width  = dw;
    ctx.height = dh;
    ctx.gray   = (uint8_t *)ps_malloc((size_t)dw * dh);
    if (!ctx.gray) {
        ESP_LOGE(TAG, "OOM allocating %dx%d decode buffer", dw, dh);
        jpeg.close();
        return false;
    }
    // ps_malloc doesn't zero memory, and MCU block rounding at odd image
    // sizes/scale factors can leave a sliver of this buffer undrawn by the
    // callback below — without this, that sliver is leftover PSRAM garbage,
    // which dithers into visible noise. White is a neutral fill for it.
    memset(ctx.gray, 255, (size_t)dw * dh);
    jpeg.setUserPointer(&ctx);

    bool ok = jpeg.decode(0, 0, scale) != 0;
    jpeg.close();
    if (!ok) {
        ESP_LOGE(TAG, "JPEG decode failed");
        free(ctx.gray);
        return false;
    }

    uint8_t *out_gray = (uint8_t *)ps_malloc((size_t)out_w * out_h);
    if (!out_gray) {
        ESP_LOGE(TAG, "OOM allocating %dx%d output buffer", out_w, out_h);
        free(ctx.gray);
        return false;
    }
    box_resize(ctx.gray, dw, dh, out_gray, out_w, out_h);
    free(ctx.gray);

    dither_to_1bpp(out_gray, out_w, out_h, out_1bpp);
    free(out_gray);
    return true;
}

// PNGdec has no built-in downscale (unlike JPEGDEC's SCALE_HALF/QUARTER/
// EIGHTH), and covers can be a few thousand pixels tall, so decoding a full
// row buffer per scanline and holding the *whole* image in memory isn't an
// option. Instead this streams: each incoming source scanline is box-
// downsampled horizontally into `row_sum`/`row_count` (the accumulator for
// whichever destination row it maps to), and the accumulator is finalized
// into `out_gray` the moment a scanline belonging to the next destination
// row arrives — so peak memory is one source row, not the whole source image.
struct PngDecodeCtx {
    PNG      *png;
    uint16_t *rgb565_row; // scratch, [src_w]
    uint32_t *row_sum;    // accumulator for `current_dy`, [out_w]
    int       row_count;
    int       current_dy;
    int       src_w, src_h;
    int       out_w, out_h;
    uint8_t  *out_gray;   // [out_w * out_h], output
};

static void finalize_png_row(PngDecodeCtx *ctx) {
    if (ctx->current_dy < 0 || ctx->current_dy >= ctx->out_h) return;
    uint8_t *dst = ctx->out_gray + (size_t)ctx->current_dy * ctx->out_w;
    for (int x = 0; x < ctx->out_w; x++)
        dst[x] = ctx->row_count ? (uint8_t)(ctx->row_sum[x] / ctx->row_count) : 255;
}

static int png_draw_callback(PNGDRAW *draw) {
    PngDecodeCtx *ctx = (PngDecodeCtx *)draw->pUser;
    ctx->png->getLineAsRGB565(draw, ctx->rgb565_row, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

    int dy = draw->y * ctx->out_h / ctx->src_h;
    if (dy != ctx->current_dy) {
        finalize_png_row(ctx);
        ctx->current_dy = dy;
        ctx->row_count  = 0;
        memset(ctx->row_sum, 0, ctx->out_w * sizeof(uint32_t));
    }

    for (int x = 0; x < ctx->out_w; x++) {
        int sx0 = x * ctx->src_w / ctx->out_w, sx1 = (x + 1) * ctx->src_w / ctx->out_w;
        if (sx1 <= sx0) sx1 = sx0 + 1;
        uint32_t sum = 0;
        int n = 0;
        for (int sx = sx0; sx < sx1 && sx < ctx->src_w; sx++) {
            uint16_t px = ctx->rgb565_row[sx];
            sum += ((px & 0x7E0) >> 5) << 2; // same green-channel luma approximation as the JPEG path
            n++;
        }
        ctx->row_sum[x] += n ? sum / n : 255;
    }
    ctx->row_count++;
    return 1;
}

static bool decode_png_cover(const uint8_t *data, size_t data_size, uint8_t *out_1bpp, int out_w, int out_h) {
    // Same reasoning as decode_jpeg_cover's static JPEGDEC: keep PNG's
    // internal decode state off the stack.
    static PNG png;
    if (png.openRAM((uint8_t *)data, (int)data_size, png_draw_callback) != PNG_SUCCESS) {
        ESP_LOGE(TAG, "Not a PNG or corrupt cover image");
        return false;
    }

    int src_w = png.getWidth(), src_h = png.getHeight();

    uint8_t  *out_gray   = (uint8_t  *)ps_malloc((size_t)out_w * out_h);
    uint16_t *rgb565_row = (uint16_t *)ps_malloc((size_t)src_w * sizeof(uint16_t));
    uint32_t *row_sum    = (uint32_t *)malloc(out_w * sizeof(uint32_t));
    if (!out_gray || !rgb565_row || !row_sum) {
        ESP_LOGE(TAG, "OOM allocating PNG decode buffers");
        free(out_gray); free(rgb565_row); free(row_sum);
        png.close();
        return false;
    }
    memset(out_gray, 255, (size_t)out_w * out_h);

    PngDecodeCtx ctx;
    ctx.png         = &png;
    ctx.rgb565_row  = rgb565_row;
    ctx.row_sum     = row_sum;
    ctx.row_count   = 0;
    ctx.current_dy  = -1;
    ctx.src_w       = src_w;
    ctx.src_h       = src_h;
    ctx.out_w       = out_w;
    ctx.out_h       = out_h;
    ctx.out_gray    = out_gray;

    bool ok = png.decode(&ctx, 0) == PNG_SUCCESS;
    finalize_png_row(&ctx); // the last accumulated row is only flushed on the *next* dy change, which never comes
    png.close();
    free(rgb565_row);
    free(row_sum);

    if (!ok) {
        ESP_LOGE(TAG, "PNG decode failed");
        free(out_gray);
        return false;
    }

    dither_to_1bpp(out_gray, out_w, out_h, out_1bpp);
    free(out_gray);
    return true;
}

// FNV-1a over the epub's own SD path — used to name its cover cache file,
// since the path itself may be long and isn't a valid filename on its own.
static uint32_t hash_path(const std::string &path) {
    uint32_t h = 2166136261u;
    for (unsigned char c : path) { h ^= c; h *= 16777619u; }
    return h;
}

// Cache format tag — bump this (cov2 -> cov3 ...) whenever the decode
// pipeline changes, so stale/corrupt cached files can never be mistaken for
// current ones; a size check alone can't catch a same-size but wrongly-
// decoded cache entry. Width/height are also baked into the filename so
// different requested sizes (grid thumbnail vs. sleep screen) don't collide.
static void cover_cache_path(const std::string &epub_path, int width, int height, char *out, size_t out_size) {
    snprintf(out, out_size, "/sd/cov2_%dx%d_%08lX.bin", width, height, (unsigned long)hash_path(epub_path));
}

bool get_cover_bitmap(Epub *epub, uint8_t *out, int width, int height) {
    size_t out_bytes = (size_t)(width / 8) * height;

    char cache_path[64];
    cover_cache_path(epub->get_path(), width, height, cache_path, sizeof(cache_path));

    FILE *f = fopen(cache_path, "rb");
    if (f) {
        size_t n = fread(out, 1, out_bytes, f);
        fclose(f);
        if (n == out_bytes) return true;
    }

    const std::string &cover_item = epub->get_cover_image_item();
    if (cover_item.empty()) return false;

    size_t img_size = 0;
    uint8_t *img = epub->get_item_contents(cover_item, &img_size);
    if (!img) return false;

    bool is_jpeg = img_size > 2 && img[0] == 0xFF && img[1] == 0xD8;
    bool is_png  = img_size > 8 && img[0] == 0x89 && img[1] == 'P' && img[2] == 'N' && img[3] == 'G';

    bool ok = is_jpeg ? decode_jpeg_cover(img, img_size, out, width, height)
            : is_png  ? decode_png_cover(img, img_size, out, width, height)
            : false;
    free(img);
    if (!ok) return false;

    f = fopen(cache_path, "wb");
    if (f) {
        fwrite(out, 1, out_bytes, f);
        fclose(f);
    }
    return true;
}

void invalidate_cover_cache(const std::string &epub_path) {
    char path[64];
    cover_cache_path(epub_path, COVER_THUMB_W, COVER_THUMB_H, path, sizeof(path));
    remove(path);
    cover_cache_path(epub_path, COVER_FEATURED_W, COVER_FEATURED_H, path, sizeof(path));
    remove(path);
    cover_cache_path(epub_path, COVER_SLEEP_W, COVER_SLEEP_H, path, sizeof(path));
    remove(path);
}
