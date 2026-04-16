/* Quick micro-benchmark on synthetic gray buffers.
 * Goal: confirm raw-gray scalar path latency before SIMD sprint. */

#include "ocr_triage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void bench(const char *label, uint32_t w, uint32_t h,
                  void (*fill)(uint8_t *, uint32_t, uint32_t)) {
    uint8_t *buf = (uint8_t *)malloc((size_t)w * (size_t)h);
    fill(buf, w, h);

    /* warmup */
    for (int i = 0; i < 32; ++i) (void)ocr_triage_has_text_gray(buf, w, h, NULL);

    int iters = 200;
    uint64_t t0 = now_us();
    float score_sum = 0.0f;
    for (int i = 0; i < iters; ++i) {
        ocr_triage_verdict_t v = ocr_triage_has_text_gray(buf, w, h, NULL);
        score_sum += v.score;
    }
    uint64_t elapsed = now_us() - t0;
    double per_iter = (double)elapsed / (double)iters;
    printf("%-32s %4ux%-4u  %8.1f us/call   (score avg %.3f)\n",
        label, w, h, per_iter, score_sum / (float)iters);
    free(buf);
}

static void fill_white(uint8_t *b, uint32_t w, uint32_t h) { memset(b, 255, (size_t)w * h); }
static void fill_gray(uint8_t *b, uint32_t w, uint32_t h)  { memset(b, 128, (size_t)w * h); }
static void fill_gradient(uint8_t *b, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            b[(size_t)y * w + x] = (uint8_t)((x * 255) / (w ? w : 1));
}
static void fill_checker(uint8_t *b, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            b[(size_t)y * w + x] = ((x / 8 + y / 8) % 2) ? 240 : 20;
}

int main(void) {
    printf("ocr-triage-c v%s (cpu: %s)\n\n",
        ocr_triage_version(), ocr_triage_cpu_features());

    printf("=== Raw gray path ===\n");
    bench("solid white", 256, 256, fill_white);
    bench("solid gray",  256, 256, fill_gray);
    bench("gradient",    256, 256, fill_gradient);
    bench("checker",     256, 256, fill_checker);
    bench("solid white (A4)", 2480, 3508, fill_white);
    bench("checker (A4)",     2480, 3508, fill_checker);
    return 0;
}
