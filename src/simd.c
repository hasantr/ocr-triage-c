/* SIMD primitives — binarize + sum_u8.
 *
 * Strategy: compile all variants (scalar + SSE2 + AVX2 + NEON) into the
 * same TU with per-function target attributes where the compiler supports
 * them, then runtime-dispatch based on cpu.c detection.
 *
 * On MSVC we rely on AVX2 intrinsics being available unconditionally at
 * /arch:AVX2 or default x64 with AVX2 intrinsics headers; the scalar
 * fallback covers everything else.
 */

#include "internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  define OCR_TRIAGE_X86 1
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
#  define OCR_TRIAGE_AARCH64 1
#endif

#ifdef OCR_TRIAGE_X86
#  include <immintrin.h>
#endif
#ifdef OCR_TRIAGE_AARCH64
#  include <arm_neon.h>
#endif

/* ============================================================ *
 * BINARIZE
 * ============================================================ */

static void binarize_scalar(const uint8_t *gray, uint8_t *dst, size_t n,
                            uint8_t threshold, int below_is_fg) {
    if (below_is_fg) {
        for (size_t i = 0; i < n; ++i) {
            dst[i] = (gray[i] <= threshold) ? 1u : 0u;
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            dst[i] = (gray[i] > threshold) ? 1u : 0u;
        }
    }
}

#ifdef OCR_TRIAGE_X86
#  if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#  endif
static void binarize_avx2(const uint8_t *gray, uint8_t *dst, size_t n,
                          uint8_t threshold, int below_is_fg) {
    /* (a > b) signed on 8-bit is asymmetric — bias by 0x80 to get unsigned
     * compare via signed _mm256_cmpgt_epi8. */
    size_t i = 0;
    const __m256i bias = _mm256_set1_epi8((char)0x80);
    const __m256i one  = _mm256_set1_epi8(1);

    if (below_is_fg) {
        /* fg = (gray <= threshold) = !(gray > threshold)
         * Using signed cmpgt with bias: gray' > thr' iff unsigned gray > thr */
        __m256i thr_v = _mm256_set1_epi8((char)(threshold ^ 0x80));
        for (; i + 32 <= n; i += 32) {
            __m256i g  = _mm256_loadu_si256((const __m256i *)(gray + i));
            __m256i gb = _mm256_xor_si256(g, bias);
            __m256i gt = _mm256_cmpgt_epi8(gb, thr_v);       /* 0xFF if gray > thr */
            __m256i le = _mm256_andnot_si256(gt, one);       /* 1 if gray <= thr */
            _mm256_storeu_si256((__m256i *)(dst + i), le);
        }
    } else {
        __m256i thr_v = _mm256_set1_epi8((char)(threshold ^ 0x80));
        for (; i + 32 <= n; i += 32) {
            __m256i g  = _mm256_loadu_si256((const __m256i *)(gray + i));
            __m256i gb = _mm256_xor_si256(g, bias);
            __m256i gt = _mm256_cmpgt_epi8(gb, thr_v);
            __m256i m  = _mm256_and_si256(gt, one);
            _mm256_storeu_si256((__m256i *)(dst + i), m);
        }
    }
    if (i < n) binarize_scalar(gray + i, dst + i, n - i, threshold, below_is_fg);
}

#  if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sse2")))
#  endif
static void binarize_sse2(const uint8_t *gray, uint8_t *dst, size_t n,
                          uint8_t threshold, int below_is_fg) {
    size_t i = 0;
    const __m128i bias = _mm_set1_epi8((char)0x80);
    const __m128i one  = _mm_set1_epi8(1);
    __m128i thr_v = _mm_set1_epi8((char)(threshold ^ 0x80));

    if (below_is_fg) {
        for (; i + 16 <= n; i += 16) {
            __m128i g  = _mm_loadu_si128((const __m128i *)(gray + i));
            __m128i gb = _mm_xor_si128(g, bias);
            __m128i gt = _mm_cmpgt_epi8(gb, thr_v);
            __m128i le = _mm_andnot_si128(gt, one);
            _mm_storeu_si128((__m128i *)(dst + i), le);
        }
    } else {
        for (; i + 16 <= n; i += 16) {
            __m128i g  = _mm_loadu_si128((const __m128i *)(gray + i));
            __m128i gb = _mm_xor_si128(g, bias);
            __m128i gt = _mm_cmpgt_epi8(gb, thr_v);
            __m128i m  = _mm_and_si128(gt, one);
            _mm_storeu_si128((__m128i *)(dst + i), m);
        }
    }
    if (i < n) binarize_scalar(gray + i, dst + i, n - i, threshold, below_is_fg);
}
#endif /* OCR_TRIAGE_X86 */

#ifdef OCR_TRIAGE_AARCH64
static void binarize_neon(const uint8_t *gray, uint8_t *dst, size_t n,
                          uint8_t threshold, int below_is_fg) {
    size_t i = 0;
    uint8x16_t thr_v = vdupq_n_u8(threshold);
    uint8x16_t one   = vdupq_n_u8(1);
    if (below_is_fg) {
        for (; i + 16 <= n; i += 16) {
            uint8x16_t g  = vld1q_u8(gray + i);
            uint8x16_t le = vcleq_u8(g, thr_v);    /* 0xFF if <=, else 0 */
            vst1q_u8(dst + i, vandq_u8(le, one));
        }
    } else {
        for (; i + 16 <= n; i += 16) {
            uint8x16_t g  = vld1q_u8(gray + i);
            uint8x16_t gt = vcgtq_u8(g, thr_v);
            vst1q_u8(dst + i, vandq_u8(gt, one));
        }
    }
    if (i < n) binarize_scalar(gray + i, dst + i, n - i, threshold, below_is_fg);
}
#endif

void ocr_triage_binarize(const uint8_t *gray, uint8_t *dst, size_t n,
                         uint8_t threshold, int below_is_fg) {
    const ocr_triage_cpu_t *c = ocr_triage_cpu_detect();
#ifdef OCR_TRIAGE_X86
    if (c->has_avx2) { binarize_avx2(gray, dst, n, threshold, below_is_fg); return; }
    if (c->has_sse2) { binarize_sse2(gray, dst, n, threshold, below_is_fg); return; }
#endif
#ifdef OCR_TRIAGE_AARCH64
    if (c->has_neon) { binarize_neon(gray, dst, n, threshold, below_is_fg); return; }
#endif
    (void)c;
    binarize_scalar(gray, dst, n, threshold, below_is_fg);
}

/* ============================================================ *
 * SUM_U8 — total value of n bytes (used for foreground coverage)
 * ============================================================ */

static uint32_t sum_u8_scalar(const uint8_t *buf, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i < n; ++i) s += buf[i];
    return s;
}

#ifdef OCR_TRIAGE_X86
#  if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#  endif
static uint32_t sum_u8_avx2(const uint8_t *buf, size_t n) {
    size_t i = 0;
    __m256i acc = _mm256_setzero_si256();
    const __m256i zero = _mm256_setzero_si256();
    for (; i + 32 <= n; i += 32) {
        __m256i v  = _mm256_loadu_si256((const __m256i *)(buf + i));
        /* sad_epu8 sums 8 bytes into a 64-bit lane — 4 lanes total per vec */
        __m256i sad = _mm256_sad_epu8(v, zero);
        acc = _mm256_add_epi64(acc, sad);
    }
    /* horizontal reduce of 4×64 accumulators */
    uint64_t tmp[4];
    _mm256_storeu_si256((__m256i *)tmp, acc);
    uint32_t total = (uint32_t)(tmp[0] + tmp[1] + tmp[2] + tmp[3]);
    for (; i < n; ++i) total += buf[i];
    return total;
}
#endif

uint32_t ocr_triage_sum_u8(const uint8_t *buf, size_t n) {
    const ocr_triage_cpu_t *c = ocr_triage_cpu_detect();
#ifdef OCR_TRIAGE_X86
    if (c->has_avx2) return sum_u8_avx2(buf, n);
#endif
    (void)c;
    return sum_u8_scalar(buf, n);
}

/* ============================================================ *
 * COUNT_TRANSITIONS — 0/1 transitions along a binary row
 *
 * edges = count( row[i] != row[i+1] )
 *
 * Since row[i] ∈ {0,1}, XOR gives {0,1} → summing the XORed stream
 * equals the transition count. AVX2: load two unaligned 32-byte
 * windows offset by 1, XOR, SAD to 4×64-bit accumulator.
 * ============================================================ */

static uint32_t count_transitions_scalar(const uint8_t *row, size_t len) {
    if (len < 2) return 0;
    uint32_t edges = 0;
    for (size_t x = 0; x + 1 < len; ++x) {
        if (row[x] != row[x + 1]) edges++;
    }
    return edges;
}

#ifdef OCR_TRIAGE_X86
#  if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#  endif
static uint32_t count_transitions_avx2(const uint8_t *row, size_t len) {
    if (len < 2) return 0;
    size_t x = 0;
    const size_t pair_count = len - 1;   /* number of (row[i], row[i+1]) pairs */
    __m256i acc = _mm256_setzero_si256();
    const __m256i zero = _mm256_setzero_si256();

    for (; x + 32 <= pair_count; x += 32) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(row + x));
        __m256i b = _mm256_loadu_si256((const __m256i *)(row + x + 1));
        __m256i d = _mm256_xor_si256(a, b);            /* 0/1 per byte */
        __m256i sad = _mm256_sad_epu8(d, zero);        /* 4×u64 partial sums */
        acc = _mm256_add_epi64(acc, sad);
    }
    uint64_t tmp[4];
    _mm256_storeu_si256((__m256i *)tmp, acc);
    uint32_t total = (uint32_t)(tmp[0] + tmp[1] + tmp[2] + tmp[3]);

    /* Scalar tail. */
    for (; x + 1 < len; ++x) {
        if (row[x] != row[x + 1]) total++;
    }
    return total;
}

#  if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sse2")))
#  endif
static uint32_t count_transitions_sse2(const uint8_t *row, size_t len) {
    if (len < 2) return 0;
    size_t x = 0;
    const size_t pair_count = len - 1;
    __m128i acc = _mm_setzero_si128();
    const __m128i zero = _mm_setzero_si128();

    for (; x + 16 <= pair_count; x += 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(row + x));
        __m128i b = _mm_loadu_si128((const __m128i *)(row + x + 1));
        __m128i d = _mm_xor_si128(a, b);
        __m128i sad = _mm_sad_epu8(d, zero);           /* 2×u64 partial sums */
        acc = _mm_add_epi64(acc, sad);
    }
    uint64_t tmp[2];
    _mm_storeu_si128((__m128i *)tmp, acc);
    uint32_t total = (uint32_t)(tmp[0] + tmp[1]);

    for (; x + 1 < len; ++x) {
        if (row[x] != row[x + 1]) total++;
    }
    return total;
}
#endif

#ifdef OCR_TRIAGE_AARCH64
static uint32_t count_transitions_neon(const uint8_t *row, size_t len) {
    if (len < 2) return 0;
    size_t x = 0;
    const size_t pair_count = len - 1;
    uint32x4_t acc = vdupq_n_u32(0);
    for (; x + 16 <= pair_count; x += 16) {
        uint8x16_t a = vld1q_u8(row + x);
        uint8x16_t b = vld1q_u8(row + x + 1);
        uint8x16_t d = veorq_u8(a, b);          /* 0/1 per byte */
        /* vpaddlq_u8: pairwise add u8→u16 ×8, then chain to u16→u32. */
        uint16x8_t s16 = vpaddlq_u8(d);
        uint32x4_t s32 = vpaddlq_u16(s16);
        acc = vaddq_u32(acc, s32);
    }
    uint32_t total = vaddvq_u32(acc);
    for (; x + 1 < len; ++x) {
        if (row[x] != row[x + 1]) total++;
    }
    return total;
}
#endif

uint32_t ocr_triage_count_transitions(const uint8_t *row, size_t len) {
    const ocr_triage_cpu_t *c = ocr_triage_cpu_detect();
#ifdef OCR_TRIAGE_X86
    if (c->has_avx2) return count_transitions_avx2(row, len);
    if (c->has_sse2) return count_transitions_sse2(row, len);
#endif
#ifdef OCR_TRIAGE_AARCH64
    if (c->has_neon) return count_transitions_neon(row, len);
#endif
    (void)c;
    return count_transitions_scalar(row, len);
}
