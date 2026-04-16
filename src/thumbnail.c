/* Aspect-preserving area-average integer downsample.
 *
 * Mirrors the Rust box_downsample in `ocr-triage` — Triangle filter'ın
 * çıkarlarına yakın sonuç, integer-only, tek-pass. */

#include "internal.h"

#include <stdint.h>
#include <string.h>

int ocr_triage_downsample_box(
    const uint8_t *src, uint32_t src_w, uint32_t src_h,
    uint32_t target_short_edge,
    uint8_t *out, size_t max_out_size,
    uint32_t *out_w, uint32_t *out_h
) {
    if (!src || !out || src_w == 0 || src_h == 0 || target_short_edge == 0) {
        return -1;
    }
    uint32_t short_side = (src_w < src_h) ? src_w : src_h;

    /* No downsample needed. */
    if (short_side <= target_short_edge) {
        if ((size_t)src_w * (size_t)src_h > max_out_size) return -1;
        memcpy(out, src, (size_t)src_w * (size_t)src_h);
        *out_w = src_w;
        *out_h = src_h;
        return 0;
    }

    uint32_t tw, th;
    if (src_w <= src_h) {
        tw = target_short_edge;
        th = (uint32_t)((uint64_t)src_h * target_short_edge / src_w);
    } else {
        tw = (uint32_t)((uint64_t)src_w * target_short_edge / src_h);
        th = target_short_edge;
    }
    if (tw == 0 || th == 0) return -1;
    if ((size_t)tw * (size_t)th > max_out_size) return -1;

    for (uint32_t sy = 0; sy < th; ++sy) {
        uint32_t y0 = (uint32_t)((uint64_t)sy * src_h / th);
        uint32_t y1 = (uint32_t)((uint64_t)(sy + 1) * src_h / th);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > src_h) y1 = src_h;

        for (uint32_t sx = 0; sx < tw; ++sx) {
            uint32_t x0 = (uint32_t)((uint64_t)sx * src_w / tw);
            uint32_t x1 = (uint32_t)((uint64_t)(sx + 1) * src_w / tw);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > src_w) x1 = src_w;

            uint32_t sum = 0, count = 0;
            for (uint32_t y = y0; y < y1; ++y) {
                const uint8_t *row = src + (size_t)y * src_w + x0;
                uint32_t n = x1 - x0;
                for (uint32_t i = 0; i < n; ++i) {
                    sum += row[i];
                }
                count += n;
            }
            out[(size_t)sy * tw + sx] = (uint8_t)(sum / (count ? count : 1));
        }
    }
    *out_w = tw;
    *out_h = th;
    return 0;
}

int ocr_triage_downsample_stride(
    const uint8_t *src, uint32_t src_w, uint32_t src_h,
    uint32_t target_short_edge,
    uint8_t *out, size_t max_out_size,
    uint32_t *out_w, uint32_t *out_h
) {
    if (!src || !out || src_w == 0 || src_h == 0 || target_short_edge == 0) {
        return -1;
    }
    uint32_t short_side = (src_w < src_h) ? src_w : src_h;
    uint32_t stride = short_side / target_short_edge;
    if (stride < 1) stride = 1;

    if (stride == 1) {
        /* No downsample — pass-through. */
        if ((size_t)src_w * (size_t)src_h > max_out_size) return -1;
        for (uint32_t y = 0; y < src_h; ++y) {
            const uint8_t *row = src + (size_t)y * src_w;
            uint8_t *dst = out + (size_t)y * src_w;
            for (uint32_t x = 0; x < src_w; ++x) dst[x] = row[x];
        }
        *out_w = src_w;
        *out_h = src_h;
        return 0;
    }

    uint32_t sw = src_w / stride;
    uint32_t sh = src_h / stride;
    if (sw == 0) sw = 1;
    if (sh == 0) sh = 1;
    if ((size_t)sw * (size_t)sh > max_out_size) return -1;

    for (uint32_t sy = 0; sy < sh; ++sy) {
        size_t y = (size_t)sy * stride;
        const uint8_t *row = src + y * src_w;
        uint8_t *dst = out + (size_t)sy * sw;
        for (uint32_t sx = 0; sx < sw; ++sx) {
            dst[sx] = row[(size_t)sx * stride];
        }
    }
    *out_w = sw;
    *out_h = sh;
    return 0;
}

void ocr_triage_rgb_to_gray(
    const uint8_t *rgb, size_t pixel_count,
    uint8_t *gray
) {
    /* BT.601: Y = (77R + 150G + 29B) >> 8 */
    for (size_t i = 0; i < pixel_count; ++i) {
        uint32_t r = rgb[i * 3];
        uint32_t g = rgb[i * 3 + 1];
        uint32_t b = rgb[i * 3 + 2];
        gray[i] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
    }
}
