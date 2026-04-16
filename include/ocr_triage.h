/*
 * ocr-triage-c — Sub-millisecond "does this image contain text?" classifier.
 *
 * C99 public API. Companion to the pure-Rust `ocr-triage` crate.
 *
 * Copyright (c) 2026 Hasan Salihoglu and ocr-triage contributors.
 * Licensed under MIT OR Apache-2.0, at your option.
 */

#ifndef OCR_TRIAGE_H
#define OCR_TRIAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCR_TRIAGE_VERSION_MAJOR 0
#define OCR_TRIAGE_VERSION_MINOR 1
#define OCR_TRIAGE_VERSION_PATCH 0

/* ================================================================
 * Types
 * ================================================================ */

typedef enum {
    OCR_TRIAGE_CONSERVATIVE = 0,  /* FN=0 hedef, text kaçırma riski az */
    OCR_TRIAGE_AGGRESSIVE   = 1   /* FP minimize, CPU tavanda */
} ocr_triage_mode_t;

typedef struct {
    int      has_text;     /* 0 or 1 */
    float    score;        /* 0.0 .. ~1.0 */
    uint32_t elapsed_us;   /* wall-clock microseconds */
} ocr_triage_verdict_t;

typedef struct {
    float    threshold;            /* 0.0-1.0, conservative default 0.27 */
    uint32_t thumbnail_short_edge; /* target short-edge px, default 256 */
} ocr_triage_config_t;

/* Owned decoded image returned by ocr_triage_has_text_with_image.
 * Release with ocr_triage_image_free. */
typedef struct {
    uint8_t *gray;    /* 8-bit grayscale, width*height bytes */
    uint32_t width;
    uint32_t height;
    uint32_t stride;  /* usually == width */
} ocr_triage_image_t;

/* ================================================================
 * Config
 * ================================================================ */

ocr_triage_config_t ocr_triage_config_default(ocr_triage_mode_t mode);

/* ================================================================
 * Encoded bytes path (JPEG/PNG/WebP/TIFF/BMP)
 *
 * Malformed input returns verdict with has_text=0, no crash.
 * Pass cfg=NULL for conservative default.
 * ================================================================ */

ocr_triage_verdict_t ocr_triage_has_text(
    const uint8_t *bytes,
    size_t         len,
    const ocr_triage_config_t *cfg
);

/* ================================================================
 * Raw pixel path — caller already has decoded pixels.
 * Fast path for PDF renderers, image pipelines.
 * ================================================================ */

/* Gray buffer must be width*height bytes. */
ocr_triage_verdict_t ocr_triage_has_text_gray(
    const uint8_t *gray,
    uint32_t       width,
    uint32_t       height,
    const ocr_triage_config_t *cfg
);

/* RGB8 buffer must be width*height*3 bytes. */
ocr_triage_verdict_t ocr_triage_has_text_rgb(
    const uint8_t *rgb,
    uint32_t       width,
    uint32_t       height,
    const ocr_triage_config_t *cfg
);

/* ================================================================
 * OCR handoff — decode once, pass decoded image to OCR engine.
 *
 * On has_text=1 the caller can feed `out_image` directly to Tesseract /
 * Leptonica / Paddle without re-decoding the original bytes.
 *
 * Returns 0 on success, -1 on decode failure (verdict.has_text set to 0).
 * ================================================================ */

int ocr_triage_has_text_with_image(
    const uint8_t *bytes,
    size_t         len,
    const ocr_triage_config_t *cfg,
    ocr_triage_verdict_t *out_verdict,
    ocr_triage_image_t   *out_image
);

void ocr_triage_image_free(ocr_triage_image_t *image);

/* Encode decoded gray image into PGM (Netpbm P5) bytes. Any OCR engine
 * accepts PGM with near-zero decode cost.
 *
 * Caller must free returned buffer with free().
 * Returns NULL on allocation failure. */
uint8_t *ocr_triage_image_to_pgm(
    const ocr_triage_image_t *image,
    size_t *out_len
);

/* ================================================================
 * Introspection
 * ================================================================ */

const char *ocr_triage_version(void);
const char *ocr_triage_cpu_features(void);  /* "scalar" / "sse2" / "avx2" / "neon" */

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* OCR_TRIAGE_H */
