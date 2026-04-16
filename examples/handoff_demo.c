/* handoff_demo — show the OCR-handoff pipeline end-to-end.
 *
 * Reads an encoded image (JPEG or PNG), runs triage, and if "text"
 * is detected writes out a PGM file that any OCR engine (Tesseract,
 * PaddleOCR, RapidOCR, Leptonica-based code, OpenCV) can consume
 * with effectively zero decode cost on its side.
 *
 * Usage:
 *   handoff_demo INPUT.jpg [OUTPUT.pgm]
 *
 * If OUTPUT.pgm is omitted the file is written next to the input,
 * replacing the extension with .pgm, only when has_text=1.
 *
 * Pipeline (single decode):
 *   encoded bytes → libjpeg-turbo / libspng decode (once)
 *                → triage verdict
 *                → if positive: full-res grayscale already in memory
 *                → serialize to PGM header + raw bytes
 *                → ready for OCR engine's SetImage / pixReadMemPnm / etc.
 */

#include "ocr_triage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, uint8_t **out, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return -1; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return -1; }
    *out = buf;
    *len = (size_t)sz;
    return 0;
}

static int write_file(const char *path, const uint8_t *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    size_t put = fwrite(data, 1, len, f);
    fclose(f);
    return (put == len) ? 0 : -1;
}

static void derive_pgm_path(const char *input, char *out, size_t cap) {
    /* Replace last '.' with .pgm (or append if no dot). */
    size_t n = strlen(input);
    if (n + 5 > cap) { out[0] = '\0'; return; }
    size_t last_dot = n;
    for (size_t i = 0; i < n; ++i) {
        if (input[i] == '.') last_dot = i;
        if (input[i] == '/' || input[i] == '\\') last_dot = n;  /* reset on sep */
    }
    if (last_dot < n) {
        memcpy(out, input, last_dot);
        memcpy(out + last_dot, ".pgm", 5);
    } else {
        memcpy(out, input, n);
        memcpy(out + n, ".pgm", 5);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: %s INPUT.jpg|.png [OUTPUT.pgm]\n"
            "  triage an encoded image; if text is detected, dump a\n"
            "  PGM thumbnail suitable for any OCR engine.\n",
            argv[0]);
        return 2;
    }
    const char *input = argv[1];

    uint8_t *bytes = NULL;
    size_t   len   = 0;
    if (read_file(input, &bytes, &len) != 0) return 1;

    ocr_triage_verdict_t verdict;
    ocr_triage_image_t   image;
    int rc = ocr_triage_has_text_with_image(bytes, len, NULL, &verdict, &image);
    free(bytes);
    if (rc != 0) {
        fprintf(stderr, "decode failed (verdict set to no-text)\n");
        return 1;
    }

    printf("ocr-triage-c v%s (cpu: %s)\n",
        ocr_triage_version(), ocr_triage_cpu_features());
    printf("input       : %s\n", input);
    printf("dimensions  : %u x %u\n", image.width, image.height);
    printf("has_text    : %s\n", verdict.has_text ? "YES" : "no");
    printf("score       : %.4f  (threshold 0.27)\n", verdict.score);
    printf("elapsed     : %u us\n", verdict.elapsed_us);

    if (!verdict.has_text) {
        printf("\nNo text detected — skipping OCR (saved ~500-2000 ms).\n");
        ocr_triage_image_free(&image);
        return 0;
    }

    /* Serialize to PGM. Caller-selected path or derived beside the input. */
    size_t pgm_len = 0;
    uint8_t *pgm = ocr_triage_image_to_pgm(&image, &pgm_len);
    if (!pgm) {
        fprintf(stderr, "PGM encode failed\n");
        ocr_triage_image_free(&image);
        return 1;
    }

    char derived[2048];
    const char *output = (argc >= 3) ? argv[2] : (derive_pgm_path(input, derived, sizeof(derived)), derived);
    if (write_file(output, pgm, pgm_len) != 0) {
        free(pgm);
        ocr_triage_image_free(&image);
        return 1;
    }

    printf("\nPGM written : %s  (%zu bytes)\n", output, pgm_len);
    printf("Feed this directly to your OCR engine — no re-decode needed.\n");
    printf("  Tesseract  : TessBaseAPI::SetImage(pix) with pixReadMemPnm(pgm, ...)\n");
    printf("  PaddleOCR  : cv::imdecode(pgm, cv::IMREAD_UNCHANGED)\n");
    printf("  Leptonica  : pixReadMemPnm(pgm_bytes, pgm_len)\n");

    free(pgm);
    ocr_triage_image_free(&image);
    return 0;
}
