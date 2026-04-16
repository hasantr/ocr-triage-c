/* CPU feature detection — runtime cpuid / ARM query.
 *
 * Thread-safety: first call performs detection; after that the struct is
 * const. Multiple concurrent first calls are benign (repeat idempotent work).
 */

#include "internal.h"

#include <stdint.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  define OCR_TRIAGE_X86 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#  define OCR_TRIAGE_AARCH64 1
#endif

#ifdef OCR_TRIAGE_X86
#  if defined(_MSC_VER)
#    include <intrin.h>
static void cpuid_ex(int leaf, int subleaf, int out[4]) {
    __cpuidex(out, leaf, subleaf);
}
#  else
#    include <cpuid.h>
static void cpuid_ex(int leaf, int subleaf, int out[4]) {
    unsigned a, b, c, d;
    __cpuid_count((unsigned)leaf, (unsigned)subleaf, a, b, c, d);
    out[0] = (int)a; out[1] = (int)b; out[2] = (int)c; out[3] = (int)d;
}
#  endif
#endif

static ocr_triage_cpu_t g_cpu;
static int g_detected = 0;

static void detect(ocr_triage_cpu_t *out) {
    memset(out, 0, sizeof(*out));
#ifdef OCR_TRIAGE_X86
    int regs[4] = {0};
    cpuid_ex(1, 0, regs);
    /* EDX bit 26 = SSE2 */
    out->has_sse2 = (regs[3] >> 26) & 1;

    /* AVX2 requires cpuid leaf 7 subleaf 0 EBX bit 5,
     * plus OSXSAVE + XMM/YMM enabled. */
    cpuid_ex(0, 0, regs);
    int max_leaf = regs[0];
    if (max_leaf >= 7) {
        cpuid_ex(7, 0, regs);
        int avx2_cpu = (regs[1] >> 5) & 1;

        /* Check OSXSAVE (cpuid 1 ECX bit 27) to ensure OS supports saving YMM. */
        int regs1[4];
        cpuid_ex(1, 0, regs1);
        int osxsave = (regs1[2] >> 27) & 1;
        int avx_cpu = (regs1[2] >> 28) & 1;

        if (avx2_cpu && avx_cpu && osxsave) {
            /* Read XCR0 to confirm YMM enabled. */
#  if defined(_MSC_VER)
            unsigned long long xcr = _xgetbv(0);
#  else
            unsigned int eax, edx;
            __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
            unsigned long long xcr = ((unsigned long long)edx << 32) | eax;
#  endif
            if ((xcr & 0x6) == 0x6) {  /* XMM (bit 1) + YMM (bit 2) */
                out->has_avx2 = 1;
            }
        }
    }
#endif

#ifdef OCR_TRIAGE_AARCH64
    /* ARMv8 baseline mandates NEON — assume available. */
    out->has_neon = 1;
#endif
}

const ocr_triage_cpu_t *ocr_triage_cpu_detect(void) {
    if (!g_detected) {
        detect(&g_cpu);
        g_detected = 1;
    }
    return &g_cpu;
}

const char *ocr_triage_cpu_features(void) {
    const ocr_triage_cpu_t *c = ocr_triage_cpu_detect();
    if (c->has_avx2) return "avx2";
    if (c->has_sse2) return "sse2";
    if (c->has_neon) return "neon";
    return "scalar";
}
