/* Triage score pipeline — scalar C port of the Rust `score::compute_raw`.
 *
 * Pipeline:
 *   1. Otsu threshold → binary (foreground = minority class, invariant polarity)
 *   2. Global score = √(horizontal_edge_density × row_projection_variance)
 *   3. 2×2 regional TOP-K score (dampens uniform background dilution)
 *   4. Coverage filter based on foreground ratio
 *
 * All integer + a few float ops. Single-pass per phase. */

#include "internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Otsu ---------- */

/* Returns foreground mask (w*h bytes, 0 or 1). Caller owns the buffer.
 * Returns NULL on alloc failure. */
static uint8_t *otsu_binarize(const uint8_t *gray, uint32_t w, uint32_t h) {
    size_t n = (size_t)w * (size_t)h;
    uint32_t hist[256];
    memset(hist, 0, sizeof(hist));
    for (size_t i = 0; i < n; ++i) hist[gray[i]]++;

    double total = (double)n;
    double sum_all = 0.0;
    for (int i = 0; i < 256; ++i) sum_all += (double)i * hist[i];

    int    best_thr = 128;
    double best_var = 0.0;
    double w_bg = 0.0, sum_bg = 0.0;
    for (int t = 0; t <= 255; ++t) {
        w_bg += hist[t];
        if (w_bg == 0.0) continue;
        double w_fg = total - w_bg;
        if (w_fg <= 0.0) break;
        sum_bg += (double)t * hist[t];
        double mean_bg = sum_bg / w_bg;
        double mean_fg = (sum_all - sum_bg) / w_fg;
        double var = w_bg * w_fg * (mean_bg - mean_fg) * (mean_bg - mean_fg);
        if (var > best_var) {
            best_var = var;
            best_thr = t;
        }
    }

    /* Foreground = minority class. */
    uint32_t below = 0;
    for (int i = 0; i <= best_thr; ++i) below += hist[i];
    uint32_t above = (uint32_t)n - below;
    int fg_is_below = (below <= above);

    uint8_t *binary = (uint8_t *)malloc(n);
    if (!binary) return NULL;
    for (size_t i = 0; i < n; ++i) {
        int below_thr = gray[i] <= (uint8_t)best_thr;
        int is_fg = fg_is_below ? below_thr : !below_thr;
        binary[i] = is_fg ? 1u : 0u;
    }
    return binary;
}

/* ---------- Edge density on a block of the binary image ---------- */
static float horizontal_edge_density_block(
    const uint8_t *bin, uint32_t stride,
    uint32_t x0, uint32_t y0,
    uint32_t w, uint32_t h
) {
    if (w < 2 || h < 1) return 0.0f;
    uint32_t edges = 0;
    for (uint32_t yy = 0; yy < h; ++yy) {
        const uint8_t *row = bin + (size_t)(y0 + yy) * stride + x0;
        for (uint32_t x = 0; x + 1 < w; ++x) {
            if (row[x] != row[x + 1]) edges++;
        }
    }
    uint32_t total = (w - 1) * h;
    float raw = (float)edges / (float)(total ? total : 1);
    /* typical text binary 0.05-0.20 → normalize to ~1 */
    float norm = raw / 0.16f;
    if (norm > 1.0f) norm = 1.0f;
    return norm;
}

/* ---------- Row-projection variance on a block ---------- */
static float projection_variance_block(
    const uint8_t *bin, uint32_t stride,
    uint32_t x0, uint32_t y0,
    uint32_t w, uint32_t h
) {
    if (w == 0 || h == 0) return 0.0f;
    /* row coverage per row (fraction of fg pixels). */
    float *rc = (float *)malloc(sizeof(float) * h);
    if (!rc) return 0.0f;
    for (uint32_t yy = 0; yy < h; ++yy) {
        const uint8_t *row = bin + (size_t)(y0 + yy) * stride + x0;
        uint32_t fg = 0;
        for (uint32_t x = 0; x < w; ++x) fg += row[x];
        rc[yy] = (float)fg / (float)w;
    }
    float mean = 0.0f;
    for (uint32_t i = 0; i < h; ++i) mean += rc[i];
    mean /= (float)h;
    float var = 0.0f;
    for (uint32_t i = 0; i < h; ++i) {
        float d = rc[i] - mean;
        var += d * d;
    }
    var /= (float)h;
    free(rc);
    float std = sqrtf(var);
    float norm = std / 0.18f;
    if (norm > 1.0f) norm = 1.0f;
    return norm;
}

static float score_block(
    const uint8_t *bin, uint32_t stride,
    uint32_t x0, uint32_t y0,
    uint32_t w, uint32_t h
) {
    float edge = horizontal_edge_density_block(bin, stride, x0, y0, w, h);
    float var  = projection_variance_block(bin, stride, x0, y0, w, h);
    return sqrtf(edge * var);
}

static int f32_cmp_desc(const void *a, const void *b) {
    float fa = *(const float *)a, fb = *(const float *)b;
    if (fa < fb) return 1;
    if (fa > fb) return -1;
    return 0;
}

static float regional_top_k(
    const uint8_t *bin, uint32_t w, uint32_t h,
    uint32_t grid, int top_k
) {
    uint32_t cw = w / grid, ch = h / grid;
    if (cw < 16 || ch < 16) return 0.0f;
    size_t cells = (size_t)grid * grid;
    float *scores = (float *)malloc(sizeof(float) * cells);
    if (!scores) return 0.0f;

    for (uint32_t gy = 0; gy < grid; ++gy) {
        for (uint32_t gx = 0; gx < grid; ++gx) {
            uint32_t x0 = gx * cw;
            uint32_t y0 = gy * ch;
            uint32_t cell_w = (gx + 1 == grid) ? (w - x0) : cw;
            uint32_t cell_h = (gy + 1 == grid) ? (h - y0) : ch;
            scores[gy * grid + gx] = score_block(bin, w, x0, y0, cell_w, cell_h);
        }
    }
    qsort(scores, cells, sizeof(float), f32_cmp_desc);
    int k = (top_k < (int)cells) ? top_k : (int)cells;
    if (k == 0) { free(scores); return 0.0f; }
    float sum = 0.0f;
    for (int i = 0; i < k; ++i) sum += scores[i];
    free(scores);
    return sum / (float)k;
}

/* ---------- Coverage filter ---------- */
static float coverage_weight(float fg_ratio) {
    if (fg_ratio < 0.01f) return 0.20f;
    if (fg_ratio < 0.03f) return 0.50f;
    if (fg_ratio <= 0.30f) return 1.00f;
    if (fg_ratio <= 0.38f) return 0.70f;
    if (fg_ratio <= 0.45f) return 0.40f;
    if (fg_ratio <= 0.55f) return 0.15f;
    if (fg_ratio <= 0.70f) return 0.30f;
    return 0.20f;
}

/* ---------- Public entry ---------- */

float ocr_triage_score(const uint8_t *gray, uint32_t w, uint32_t h) {
    if (!gray || w == 0 || h == 0) return 0.0f;
    uint8_t *bin = otsu_binarize(gray, w, h);
    if (!bin) return 0.0f;

    float global = score_block(bin, w, 0, 0, w, h);
    float regional = regional_top_k(bin, w, h, 2, 2);
    float raw = (global > regional * 0.9f) ? global : regional * 0.9f;

    /* coverage */
    size_t n = (size_t)w * (size_t)h;
    uint32_t fg_count = 0;
    for (size_t i = 0; i < n; ++i) fg_count += bin[i];
    float fg_ratio = (float)fg_count / (float)(n ? n : 1);
    float cov = coverage_weight(fg_ratio);

    free(bin);
    return raw * cov;
}
