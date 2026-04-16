/* bench_encoded — measures full encoded-bytes path for a single file,
 * 20 iterations, reports mean/p50/p95. Used to validate Sprint 2 gains. */

#include "ocr_triage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static uint64_t now_us(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e6 / (double)f.QuadPart);
}
#else
#include <time.h>
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}
#endif

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static int read_file(const char *path, uint8_t **out, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return -1; }
    *out = buf; *len = (size_t)sz;
    return 0;
}

static void bench_file(const char *path) {
    uint8_t *bytes = NULL;
    size_t   len = 0;
    if (read_file(path, &bytes, &len) != 0) return;

    /* Warmup */
    for (int i = 0; i < 5; ++i) (void)ocr_triage_has_text(bytes, len, NULL);

    int iters = 20;
    uint64_t samples[64];
    if (iters > 64) iters = 64;
    float score = 0.0f;
    int   has_text = -1;

    for (int i = 0; i < iters; ++i) {
        uint64_t t0 = now_us();
        ocr_triage_verdict_t v = ocr_triage_has_text(bytes, len, NULL);
        uint64_t dt = now_us() - t0;
        samples[i] = dt;
        score = v.score;
        has_text = v.has_text;
    }
    free(bytes);

    qsort(samples, iters, sizeof(uint64_t), cmp_u64);
    uint64_t sum = 0;
    for (int i = 0; i < iters; ++i) sum += samples[i];
    uint64_t mean = sum / (uint64_t)iters;
    uint64_t p50 = samples[iters / 2];
    uint64_t p95 = samples[(int)(iters * 0.95)];
    uint64_t max = samples[iters - 1];

    printf("%-60s  has_text=%d score=%.3f   %5lu/%5lu/%5lu/%5lu us (mean/p50/p95/max)\n",
        path, has_text, score,
        (unsigned long)mean, (unsigned long)p50,
        (unsigned long)p95, (unsigned long)max);
}

int main(int argc, char **argv) {
    printf("ocr-triage-c v%s (cpu: %s)\n\n",
        ocr_triage_version(), ocr_triage_cpu_features());

    if (argc < 2) {
        fprintf(stderr, "usage: %s FILE [FILE ...]\n", argv[0]);
        return 2;
    }
    for (int i = 1; i < argc; ++i) {
        bench_file(argv[i]);
    }
    return 0;
}
