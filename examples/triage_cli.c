/* triage_cli — minimal demo using raw gray path.
 *
 * Usage:
 *   triage_cli gray WIDTH HEIGHT FILE    # raw 8-bit grayscale bytes
 *   triage_cli rgb  WIDTH HEIGHT FILE    # raw RGB888 bytes
 *
 * Encoded bytes (JPEG/PNG) arrive in Sprint 2.
 */

#include "ocr_triage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, uint8_t **out_buf, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return -1; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    size_t read = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (read != (size_t)sz) { free(buf); return -1; }
    *out_buf = buf;
    *out_len = (size_t)sz;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s {gray|rgb} WIDTH HEIGHT FILE\n", argv[0]);
        return 2;
    }
    const char *mode = argv[1];
    uint32_t w = (uint32_t)atoi(argv[2]);
    uint32_t h = (uint32_t)atoi(argv[3]);
    const char *path = argv[4];

    uint8_t *buf = NULL;
    size_t   len = 0;
    if (read_file(path, &buf, &len) != 0) return 1;

    ocr_triage_verdict_t v;
    if (strcmp(mode, "gray") == 0) {
        if (len != (size_t)w * (size_t)h) {
            fprintf(stderr, "size mismatch: expected %zu, got %zu\n",
                (size_t)w * (size_t)h, len);
            free(buf); return 1;
        }
        v = ocr_triage_has_text_gray(buf, w, h, NULL);
    } else if (strcmp(mode, "rgb") == 0) {
        if (len != (size_t)w * (size_t)h * 3) {
            fprintf(stderr, "size mismatch: expected %zu, got %zu\n",
                (size_t)w * (size_t)h * 3, len);
            free(buf); return 1;
        }
        v = ocr_triage_has_text_rgb(buf, w, h, NULL);
    } else {
        fprintf(stderr, "mode must be 'gray' or 'rgb'\n");
        free(buf); return 2;
    }
    free(buf);

    printf("has_text=%d score=%.4f elapsed=%uus (ocr-triage-c v%s/%s)\n",
        v.has_text, v.score, v.elapsed_us,
        ocr_triage_version(), ocr_triage_cpu_features());
    return v.has_text ? 0 : 1;
}
