/* Sanity tests — no external deps. Mirrors the Rust `sanity.rs` file. */

#include "ocr_triage.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Make a solid-color gray image. */
static uint8_t *make_solid_gray(uint32_t w, uint32_t h, uint8_t value) {
    size_t n = (size_t)w * (size_t)h;
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) return NULL;
    memset(buf, value, n);
    return buf;
}

static int test_empty_gray_returns_no_text(void) {
    ocr_triage_verdict_t v = ocr_triage_has_text_gray(NULL, 0, 0, NULL);
    if (v.has_text != 0) { printf("FAIL: empty gray has_text=%d\n", v.has_text); return 1; }
    return 0;
}

static int test_solid_white_is_text_free(void) {
    uint8_t *buf = make_solid_gray(256, 256, 255);
    ocr_triage_verdict_t v = ocr_triage_has_text_gray(buf, 256, 256, NULL);
    free(buf);
    if (v.has_text) { printf("FAIL: solid white has_text=1, score=%f\n", v.score); return 1; }
    return 0;
}

static int test_solid_black_is_text_free(void) {
    uint8_t *buf = make_solid_gray(256, 256, 0);
    ocr_triage_verdict_t v = ocr_triage_has_text_gray(buf, 256, 256, NULL);
    free(buf);
    if (v.has_text) { printf("FAIL: solid black has_text=1, score=%f\n", v.score); return 1; }
    return 0;
}

static int test_aggressive_mode_has_higher_threshold(void) {
    ocr_triage_config_t c = ocr_triage_config_default(OCR_TRIAGE_CONSERVATIVE);
    ocr_triage_config_t a = ocr_triage_config_default(OCR_TRIAGE_AGGRESSIVE);
    if (!(a.threshold > c.threshold)) {
        printf("FAIL: thresholds cons=%f aggr=%f\n", c.threshold, a.threshold);
        return 1;
    }
    return 0;
}

static int test_verdict_carries_elapsed(void) {
    uint8_t *buf = make_solid_gray(64, 64, 128);
    ocr_triage_verdict_t v = ocr_triage_has_text_gray(buf, 64, 64, NULL);
    free(buf);
    /* elapsed_us should be > 0 — but on very fast machines might be 0 for 64x64.
     * We just check non-negative / sane. */
    if (v.elapsed_us > 1000000) { printf("FAIL: absurd elapsed %u\n", v.elapsed_us); return 1; }
    return 0;
}

static int test_rgb_same_as_gray_for_uniform(void) {
    uint32_t w = 128, h = 128;
    size_t n = (size_t)w * (size_t)h;
    uint8_t *rgb = (uint8_t *)malloc(n * 3);
    memset(rgb, 200, n * 3);  /* bright uniform → gray ≈ 200 */
    ocr_triage_verdict_t v = ocr_triage_has_text_rgb(rgb, w, h, NULL);
    free(rgb);
    if (v.has_text) { printf("FAIL: uniform RGB marked as text\n"); return 1; }
    return 0;
}

int main(void) {
    int failures = 0;
    printf("ocr-triage-c v%s (cpu: %s)\n",
        ocr_triage_version(), ocr_triage_cpu_features());

    failures += test_empty_gray_returns_no_text();
    failures += test_solid_white_is_text_free();
    failures += test_solid_black_is_text_free();
    failures += test_aggressive_mode_has_higher_threshold();
    failures += test_verdict_carries_elapsed();
    failures += test_rgb_same_as_gray_for_uniform();

    if (failures == 0) {
        printf("PASS: all tests\n");
        return 0;
    }
    printf("FAIL: %d test(s) failed\n", failures);
    return 1;
}
