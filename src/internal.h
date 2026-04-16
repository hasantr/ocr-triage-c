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

/* -------- score pipeline -------- */

/* Compute final triage score in [0,1]. Input: downsampled grayscale.
 * Pipeline: Otsu binarize → edge density + projection variance +
 * 2×2 regional TOP-K → coverage filter. */
float ocr_triage_score(
    const uint8_t *gray,
    uint32_t w, uint32_t h
);

#endif /* OCR_TRIAGE_INTERNAL_H */
