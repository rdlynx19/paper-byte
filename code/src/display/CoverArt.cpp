#include "CoverArt.h"
#include "../epub/Epub.h"
#include <JPEGDEC.h>
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

static bool decode_jpeg_cover(const uint8_t *jpg, size_t jpg_size, uint8_t *out_1bpp) {
    JPEGDEC jpeg;
    if (!jpeg.openRAM((uint8_t *)jpg, (int)jpg_size, jpeg_draw_callback)) {
        ESP_LOGE(TAG, "Not a JPEG or corrupt cover image");
        return false;
    }

    int src_w = jpeg.getWidth(), src_h = jpeg.getHeight();

    // Pick the coarsest built-in downscale that still leaves us at least
    // thumbnail-sized, so we don't decode (and allocate) a multi-megapixel
    // image just to shrink it back down a moment later.
    int scale = 0; // 0 == full size
    int dw = src_w, dh = src_h;
    while (scale < JPEG_SCALE_EIGHTH && dw / 2 >= COVER_THUMB_W && dh / 2 >= COVER_THUMB_H) {
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
    jpeg.setUserPointer(&ctx);

    bool ok = jpeg.decode(0, 0, scale) != 0;
    jpeg.close();
    if (!ok) {
        ESP_LOGE(TAG, "JPEG decode failed");
        free(ctx.gray);
        return false;
    }

    uint8_t *thumb_gray = (uint8_t *)ps_malloc((size_t)COVER_THUMB_W * COVER_THUMB_H);
    if (!thumb_gray) {
        ESP_LOGE(TAG, "OOM allocating thumbnail buffer");
        free(ctx.gray);
        return false;
    }
    box_resize(ctx.gray, dw, dh, thumb_gray, COVER_THUMB_W, COVER_THUMB_H);
    free(ctx.gray);

    dither_to_1bpp(thumb_gray, COVER_THUMB_W, COVER_THUMB_H, out_1bpp);
    free(thumb_gray);
    return true;
}

// FNV-1a over the epub's own SD path — used to name its cover cache file,
// since the path itself may be long and isn't a valid filename on its own.
static uint32_t hash_path(const std::string &path) {
    uint32_t h = 2166136261u;
    for (unsigned char c : path) { h ^= c; h *= 16777619u; }
    return h;
}

static void cover_cache_path(const std::string &epub_path, char *out, size_t out_size) {
    snprintf(out, out_size, "/sd/cov_%08lX.bin", (unsigned long)hash_path(epub_path));
}

bool get_cover_thumbnail(Epub *epub, uint8_t *out) {
    char cache_path[64];
    cover_cache_path(epub->get_path(), cache_path, sizeof(cache_path));

    FILE *f = fopen(cache_path, "rb");
    if (f) {
        size_t n = fread(out, 1, COVER_THUMB_BYTES, f);
        fclose(f);
        if (n == COVER_THUMB_BYTES) return true;
    }

    const std::string &cover_item = epub->get_cover_image_item();
    if (cover_item.empty()) return false;

    size_t jpg_size = 0;
    uint8_t *jpg = epub->get_item_contents(cover_item, &jpg_size);
    if (!jpg) return false;

    bool is_jpeg = jpg_size > 2 && jpg[0] == 0xFF && jpg[1] == 0xD8;
    bool ok = is_jpeg && decode_jpeg_cover(jpg, jpg_size, out);
    free(jpg);
    if (!ok) return false;

    f = fopen(cache_path, "wb");
    if (f) {
        fwrite(out, 1, COVER_THUMB_BYTES, f);
        fclose(f);
    }
    return true;
}
