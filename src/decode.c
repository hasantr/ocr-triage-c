/* Encoded bytes → full-resolution 8-bit grayscale.
 *
 * JPEG  via libjpeg-turbo (DCT-level 1/8 scaled decode when requested).
 * PNG   via libspng (SSE2/NEON accelerated inflate + filter).
 *
 * Both decoders normalize to grayscale via BT.601 luma if the source is
 * RGB/RGBA. Palette PNGs are expanded. 16-bit PNGs are narrowed to 8-bit.
 */

#include "internal.h"
#include "ocr_triage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef OCR_TRIAGE_WITH_JPEG
#include "turbojpeg.h"
#endif

#ifdef OCR_TRIAGE_WITH_PNG
#include "spng.h"
#endif

/* ---------- Format sniff ---------- */

ocr_triage_format_t ocr_triage_sniff(const uint8_t *bytes, size_t len) {
    if (!bytes || len < 8) return OCR_TRIAGE_FORMAT_UNKNOWN;
    /* JPEG: FF D8 FF */
    if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        return OCR_TRIAGE_FORMAT_JPEG;
    }
    /* PNG: 89 50 4E 47 0D 0A 1A 0A */
    if (bytes[0] == 0x89 && bytes[1] == 0x50 &&
        bytes[2] == 0x4E && bytes[3] == 0x47) {
        return OCR_TRIAGE_FORMAT_PNG;
    }
    return OCR_TRIAGE_FORMAT_UNKNOWN;
}

/* ---------- JPEG (libjpeg-turbo) ---------- */

#ifdef OCR_TRIAGE_WITH_JPEG
static int decode_jpeg_impl(
    const uint8_t *bytes, size_t len,
    int scaled_preview,
    uint8_t **out_gray,
    uint32_t *out_w, uint32_t *out_h
) {
    tjhandle h = tj3Init(TJINIT_DECOMPRESS);
    if (!h) return -1;

    /* Decide scaling factor. libjpeg-turbo supports 1/8, 1/4, 1/2, 1, 2, 4, 8. */
    if (scaled_preview) {
        tjscalingfactor sf = { 1, 8 };
        if (tj3SetScalingFactor(h, sf) != 0) {
            /* Non-fatal: retry full-size. */
            tjscalingfactor one = { 1, 1 };
            (void)tj3SetScalingFactor(h, one);
        }
    }

    if (tj3DecompressHeader(h, bytes, (size_t)len) != 0) {
        tj3Destroy(h);
        return -1;
    }

    int jpeg_w = tj3Get(h, TJPARAM_JPEGWIDTH);
    int jpeg_h = tj3Get(h, TJPARAM_JPEGHEIGHT);
    if (jpeg_w <= 0 || jpeg_h <= 0) {
        tj3Destroy(h);
        return -1;
    }
    /* Scaled dims via factor set earlier (defaults to 1/1 if scale disabled). */
    tjscalingfactor active_sf = { 1, 1 };
    if (scaled_preview) active_sf = (tjscalingfactor){ 1, 8 };
    int scaled_w = TJSCALED(jpeg_w, active_sf);
    int scaled_h = TJSCALED(jpeg_h, active_sf);
    if (scaled_w <= 0 || scaled_h <= 0) {
        tj3Destroy(h);
        return -1;
    }

    size_t pixels = (size_t)scaled_w * (size_t)scaled_h;
    uint8_t *gray = (uint8_t *)malloc(pixels);
    if (!gray) {
        tj3Destroy(h);
        return -1;
    }

    /* Decompress directly into grayscale — libjpeg-turbo handles
     * YCbCr → Y conversion internally. TJPF_GRAY. */
    int rc = tj3Decompress8(h, bytes, (size_t)len, gray,
                            scaled_w /* pitch=w for grayscale */,
                            TJPF_GRAY);
    tj3Destroy(h);
    if (rc != 0) {
        free(gray);
        return -1;
    }

    *out_gray = gray;
    *out_w = (uint32_t)scaled_w;
    *out_h = (uint32_t)scaled_h;
    return 0;
}
#endif /* OCR_TRIAGE_WITH_JPEG */

/* ---------- PNG (libspng) ---------- */

#ifdef OCR_TRIAGE_WITH_PNG
static int decode_png_impl(
    const uint8_t *bytes, size_t len,
    uint8_t **out_gray,
    uint32_t *out_w, uint32_t *out_h
) {
    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx) return -1;

    /* Let libspng choose a sensible memory cap (10 MP image is fine;
     * explicit large caps mostly matter for user-supplied PNGs). */
    spng_set_image_limits(ctx, 65535, 65535);
    spng_set_chunk_limits(ctx, 16 * 1024 * 1024, 64 * 1024 * 1024);

    if (spng_set_png_buffer(ctx, bytes, len) != 0) {
        spng_ctx_free(ctx);
        return -1;
    }

    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) != 0) {
        spng_ctx_free(ctx);
        return -1;
    }

    /* Ask libspng to deliver 8-bit RGBA — uniform fast path for all
     * color types (gray, RGB, palette, w/wo alpha, 16-bit).
     * We then collapse to gray in one pass. */
    int fmt = SPNG_FMT_RGBA8;
    size_t out_size = 0;
    if (spng_decoded_image_size(ctx, fmt, &out_size) != 0) {
        spng_ctx_free(ctx);
        return -1;
    }

    uint8_t *rgba = (uint8_t *)malloc(out_size);
    if (!rgba) { spng_ctx_free(ctx); return -1; }

    if (spng_decode_image(ctx, rgba, out_size, fmt, 0) != 0) {
        free(rgba);
        spng_ctx_free(ctx);
        return -1;
    }

    size_t pixels = (size_t)ihdr.width * (size_t)ihdr.height;
    uint8_t *gray = (uint8_t *)malloc(pixels);
    if (!gray) { free(rgba); spng_ctx_free(ctx); return -1; }

    /* BT.601: Y = (77R + 150G + 29B) >> 8. Ignore alpha. */
    for (size_t i = 0; i < pixels; ++i) {
        uint32_t r = rgba[i * 4];
        uint32_t g = rgba[i * 4 + 1];
        uint32_t b = rgba[i * 4 + 2];
        gray[i] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
    }

    free(rgba);
    spng_ctx_free(ctx);

    *out_gray = gray;
    *out_w = ihdr.width;
    *out_h = ihdr.height;
    return 0;
}
#endif /* OCR_TRIAGE_WITH_PNG */

/* ---------- Public dispatch ---------- */

int ocr_triage_decode_gray(
    const uint8_t *bytes, size_t len,
    int scaled_preview,
    uint8_t **out_gray,
    uint32_t *out_w, uint32_t *out_h
) {
    if (!bytes || !out_gray || !out_w || !out_h) return -1;
    *out_gray = NULL;
    *out_w = 0;
    *out_h = 0;

    switch (ocr_triage_sniff(bytes, len)) {
#ifdef OCR_TRIAGE_WITH_JPEG
        case OCR_TRIAGE_FORMAT_JPEG:
            return decode_jpeg_impl(bytes, len, scaled_preview, out_gray, out_w, out_h);
#endif
#ifdef OCR_TRIAGE_WITH_PNG
        case OCR_TRIAGE_FORMAT_PNG:
            (void)scaled_preview;  /* PNG has no scaled-decode equivalent. */
            return decode_png_impl(bytes, len, out_gray, out_w, out_h);
#endif
        default:
            return -1;
    }
}
