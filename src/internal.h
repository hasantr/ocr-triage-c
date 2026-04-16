/*
 * ocr-triage-c internal helpers. Not part of public API.
 */

#ifndef OCR_TRIAGE_INTERNAL_H
#define OCR_TRIAGE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ocr_triage.h"

/* -------- thumbnail (area-average integer subsample) -------- */

/* Aspect-preserving area-average downsample. Quality-biased.
 * Used for encoded-bytes path where decoded pixels may be soft.
 * Returns 0 on success, writes actual dims to `out_w`, `out_h`.
 * Returns -1 if input invalid. */
int ocr_triage_downsample_box(
    const uint8_t *src, uint32_t src_w, uint32_t src_h,
    uint32_t target_short_edge,
    uint8_t *out, size_t max_out_size,
    uint32_t *out_w, uint32_t *out_h
);

/* Integer stride subsample (nearest-style, every Nth pixel). Speed-biased.
 * Used for raw-pixel path where caller feeds crisp decoded pixels
 * (PDF renderer, image pipeline). Much faster than box filter.
 * Output dims: (w/stride, h/stride) where stride = short_side / target.
 * Returns 0 on success, -1 on invalid input.
 * `max_out_size` must be ≥ (w/stride) * (h/stride). */
int ocr_triage_downsample_stride(
    const uint8_t *src, uint32_t src_w, uint32_t src_h,
    uint32_t target_short_edge,
    uint8_t *out, size_t max_out_size,
    uint32_t *out_w, uint32_t *out_h
);

/* -------- RGB → gray (BT.601 approximation) -------- */
void ocr_triage_rgb_to_gray(
    const uint8_t *rgb, size_t pixel_count,
    uint8_t *gray
);

/* -------- CPU feature detection + SIMD dispatch -------- */

typedef struct {
    int has_sse2;
    int has_avx2;
    int has_neon;
} ocr_triage_cpu_t;

/* Cached singleton; detected on first call. Safe across threads after init. */
const ocr_triage_cpu_t *ocr_triage_cpu_detect(void);

/* -------- SIMD primitives -------- */

/* Binarize 8-bit grayscale into 0/1 mask.
 *   `below_is_fg != 0` → pixel <= threshold maps to 1 else 0.
 *   `below_is_fg == 0` → pixel >  threshold maps to 1 else 0.
 * Writes n bytes to `dst`. */
void ocr_triage_binarize(
    const uint8_t *gray, uint8_t *dst, size_t n,
    uint8_t threshold, int below_is_fg
);

/* Sum all bytes of `n`-length buffer — used for foreground coverage count.
 * Returns uint32 sum. */
uint32_t ocr_triage_sum_u8(const uint8_t *buf, size_t n);

/* -------- score pipeline -------- */

/* Compute final triage score in [0,1]. Input: downsampled grayscale.
 * Pipeline: Otsu binarize → edge density + projection variance +
 * 2×2 regional TOP-K → coverage filter. */
float ocr_triage_score(
    const uint8_t *gray,
    uint32_t w, uint32_t h
);

/* -------- decode -------- */

/* Detected image format (used internally). */
typedef enum {
    OCR_TRIAGE_FORMAT_UNKNOWN = 0,
    OCR_TRIAGE_FORMAT_JPEG,
    OCR_TRIAGE_FORMAT_PNG
    /* WebP/TIFF/BMP can join in a later sprint */
} ocr_triage_format_t;

ocr_triage_format_t ocr_triage_sniff(const uint8_t *bytes, size_t len);

/* Decode encoded bytes → full-resolution 8-bit grayscale buffer.
 *
 * Returns 0 on success. Writes malloc'd gray buffer to *out_gray (caller
 * frees with free()), dimensions to *out_w / *out_h.
 * Returns -1 on decode failure. *out_gray set to NULL.
 *
 * For JPEG: uses libjpeg-turbo with DCT-level 1/8 scaled decode when
 * `scaled_preview=1` (short side ~target/8), else full resolution.
 * For PNG: libspng, full resolution (no DCT-scale equivalent).
 */
int ocr_triage_decode_gray(
    const uint8_t *bytes, size_t len,
    int scaled_preview,
    uint8_t **out_gray,
    uint32_t *out_w,
    uint32_t *out_h
);

#endif /* OCR_TRIAGE_INTERNAL_H */
