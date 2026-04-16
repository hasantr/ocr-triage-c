/* Public API surface + config + introspection. */

#include "ocr_triage.h"
#include "internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Thumbnail buffer size cap (4 MP downsampled thumbnail is overkill — we use 256). */
#define OCR_TRIAGE_MAX_THUMB ((size_t)512 * 512)

/* ---------- Time helpers ---------- */
#if defined(_WIN32)
#include <windows.h>
static uint64_t now_us(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e6 / (double)f.QuadPart);
}
#else
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}
#endif

/* ---------- Config ---------- */

ocr_triage_config_t ocr_triage_config_default(ocr_triage_mode_t mode) {
    ocr_triage_config_t cfg;
    cfg.thumbnail_short_edge = 256;
    cfg.threshold = (mode == OCR_TRIAGE_AGGRESSIVE) ? 0.40f : 0.27f;
    return cfg;
}

static ocr_triage_config_t resolve_cfg(const ocr_triage_config_t *cfg) {
    if (cfg) return *cfg;
    return ocr_triage_config_default(OCR_TRIAGE_CONSERVATIVE);
}

static ocr_triage_verdict_t empty_verdict(uint32_t us) {
    ocr_triage_verdict_t v = {0, 0.0f, us};
    return v;
}

/* ---------- Raw gray path ---------- */

ocr_triage_verdict_t ocr_triage_has_text_gray(
    const uint8_t *gray, uint32_t width, uint32_t height,
    const ocr_triage_config_t *cfg_in
) {
    uint64_t t0 = now_us();
    ocr_triage_config_t cfg = resolve_cfg(cfg_in);

    if (!gray || width == 0 || height == 0) {
        return empty_verdict((uint32_t)(now_us() - t0));
    }

    /* subsample to thumbnail buffer on stack-ish (static? — just heap) */
    uint8_t *thumb = (uint8_t *)malloc(OCR_TRIAGE_MAX_THUMB);
    if (!thumb) return empty_verdict((uint32_t)(now_us() - t0));

    /* Raw-pixel path: speed-biased stride subsample (caller's pixels are crisp).
     * Encoded path (Sprint 2) will use ocr_triage_downsample_box instead. */
    uint32_t tw = 0, th = 0;
    int rc = ocr_triage_downsample_stride(
        gray, width, height, cfg.thumbnail_short_edge,
        thumb, OCR_TRIAGE_MAX_THUMB, &tw, &th
    );
    if (rc != 0) {
        free(thumb);
        return empty_verdict((uint32_t)(now_us() - t0));
    }

    float score = ocr_triage_score(thumb, tw, th);
    free(thumb);

    ocr_triage_verdict_t v;
    v.has_text = (score >= cfg.threshold) ? 1 : 0;
    v.score = score;
    v.elapsed_us = (uint32_t)(now_us() - t0);
    return v;
}

/* ---------- Raw RGB path ---------- */

ocr_triage_verdict_t ocr_triage_has_text_rgb(
    const uint8_t *rgb, uint32_t width, uint32_t height,
    const ocr_triage_config_t *cfg_in
) {
    uint64_t t0 = now_us();
    if (!rgb || width == 0 || height == 0) {
        return empty_verdict((uint32_t)(now_us() - t0));
    }

    size_t n = (size_t)width * (size_t)height;
    uint8_t *gray = (uint8_t *)malloc(n);
    if (!gray) return empty_verdict((uint32_t)(now_us() - t0));

    ocr_triage_rgb_to_gray(rgb, n, gray);
    ocr_triage_verdict_t v = ocr_triage_has_text_gray(gray, width, height, cfg_in);
    free(gray);
    /* keep elapsed from the gray call but overwrite with outer timing */
    v.elapsed_us = (uint32_t)(now_us() - t0);
    return v;
}

/* ---------- Encoded bytes path (JPEG/PNG) ---------- */

ocr_triage_verdict_t ocr_triage_has_text(
    const uint8_t *bytes, size_t len,
    const ocr_triage_config_t *cfg_in
) {
    uint64_t t0 = now_us();
    ocr_triage_config_t cfg = resolve_cfg(cfg_in);

    /* Decode once. JPEG uses DCT 1/8 scaled preview (10-20× speedup),
     * PNG has no equivalent and goes full-res. */
    uint8_t *gray = NULL;
    uint32_t w = 0, h = 0;
    if (ocr_triage_decode_gray(bytes, len, /* scaled_preview */ 1,
                               &gray, &w, &h) != 0 || !gray) {
        return empty_verdict((uint32_t)(now_us() - t0));
    }

    /* Quality-biased box subsample (decoded pixels may be soft from JPEG). */
    uint8_t *thumb = (uint8_t *)malloc(OCR_TRIAGE_MAX_THUMB);
    if (!thumb) {
        free(gray);
        return empty_verdict((uint32_t)(now_us() - t0));
    }

    uint32_t tw = 0, th = 0;
    int rc = ocr_triage_downsample_box(
        gray, w, h, cfg.thumbnail_short_edge,
        thumb, OCR_TRIAGE_MAX_THUMB, &tw, &th
    );
    free(gray);
    if (rc != 0) {
        free(thumb);
        return empty_verdict((uint32_t)(now_us() - t0));
    }

    float score = ocr_triage_score(thumb, tw, th);
    free(thumb);

    ocr_triage_verdict_t v;
    v.has_text    = (score >= cfg.threshold) ? 1 : 0;
    v.score       = score;
    v.elapsed_us  = (uint32_t)(now_us() - t0);
    return v;
}

int ocr_triage_has_text_with_image(
    const uint8_t *bytes, size_t len,
    const ocr_triage_config_t *cfg_in,
    ocr_triage_verdict_t *out_verdict,
    ocr_triage_image_t *out_image
) {
    if (!out_verdict || !out_image) return -1;
    out_image->gray = NULL;
    out_image->width = out_image->height = out_image->stride = 0;

    uint64_t t0 = now_us();
    ocr_triage_config_t cfg = resolve_cfg(cfg_in);

    /* Decode full-resolution — caller needs it for OCR engine handoff. */
    uint8_t *gray = NULL;
    uint32_t w = 0, h = 0;
    if (ocr_triage_decode_gray(bytes, len, /* scaled_preview */ 0,
                               &gray, &w, &h) != 0 || !gray) {
        *out_verdict = empty_verdict((uint32_t)(now_us() - t0));
        return -1;
    }

    /* Thumbnail for scoring (stride subsample — caller's full-res is crisp). */
    uint8_t *thumb = (uint8_t *)malloc(OCR_TRIAGE_MAX_THUMB);
    if (!thumb) {
        free(gray);
        *out_verdict = empty_verdict((uint32_t)(now_us() - t0));
        return -1;
    }

    uint32_t tw = 0, th = 0;
    int rc = ocr_triage_downsample_stride(
        gray, w, h, cfg.thumbnail_short_edge,
        thumb, OCR_TRIAGE_MAX_THUMB, &tw, &th
    );
    if (rc != 0) {
        free(thumb);
        free(gray);
        *out_verdict = empty_verdict((uint32_t)(now_us() - t0));
        return -1;
    }

    float score = ocr_triage_score(thumb, tw, th);
    free(thumb);

    ocr_triage_verdict_t v;
    v.has_text    = (score >= cfg.threshold) ? 1 : 0;
    v.score       = score;
    v.elapsed_us  = (uint32_t)(now_us() - t0);
    *out_verdict = v;

    /* Full-res gray ownership transferred to caller. */
    out_image->gray   = gray;
    out_image->width  = w;
    out_image->height = h;
    out_image->stride = w;
    return 0;
}

void ocr_triage_image_free(ocr_triage_image_t *image) {
    if (!image) return;
    free(image->gray);
    image->gray = NULL;
    image->width = image->height = image->stride = 0;
}

/* ---------- PGM encode ---------- */

uint8_t *ocr_triage_image_to_pgm(const ocr_triage_image_t *image, size_t *out_len) {
    if (!image || !image->gray || image->width == 0 || image->height == 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    char header[64];
    int header_len = snprintf(header, sizeof(header), "P5\n%u %u\n255\n",
                              image->width, image->height);
    if (header_len <= 0) return NULL;

    size_t pixel_bytes = (size_t)image->width * (size_t)image->height;
    size_t total = (size_t)header_len + pixel_bytes;

    uint8_t *out = (uint8_t *)malloc(total);
    if (!out) return NULL;
    memcpy(out, header, header_len);
    memcpy(out + header_len, image->gray, pixel_bytes);

    if (out_len) *out_len = total;
    return out;
}

/* ---------- Introspection ---------- */

const char *ocr_triage_version(void) {
    return "0.1.0";
}

const char *ocr_triage_cpu_features(void) {
    return "scalar";  /* SIMD sprint sonrası: "avx2" / "neon" / ... */
}
