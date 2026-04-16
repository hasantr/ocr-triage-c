# ocr-triage-c

**Does this image contain text?** Sub-millisecond binary classifier, written in C99. Companion to the pure-Rust [`ocr-triage`](https://github.com/hasantr/ocr-triage) crate — same algorithm, same verdicts, **aggressively faster** via SIMD.

## Why this lib

OCR engines (Tesseract, PaddleOCR, RapidOCR) take **500-2000 ms per image**. Most images fed to a document pipeline have no text at all — logos, photos, gradients, icons. `ocr-triage` answers one question first:

> Is it worth calling the OCR engine on this image?

If "no", skip. If "yes", pass through. In mixed batches, **~%60 of images are text-free** — this saves hours.

This C library:

- **Sub-millisecond** on raw pixel buffers (target: <500 µs on 8 MP grayscale).
- **Runtime SIMD dispatch** — AVX2 / NEON / SSE2 / scalar fallback.
- **Single header, small library** (~80 KB shared, ~50 KB static).
- **C99 ABI** — callable from Rust, Python, Go, C++, any language with FFI.
- **MIT OR Apache-2.0** dual license.

## Status

**Sprint 1 in progress.** What works right now:

- `ocr_triage_has_text_gray` — raw 8-bit grayscale → verdict
- `ocr_triage_has_text_rgb` — raw RGB888 → verdict
- `ocr_triage_has_text_with_image` → **stub** (Sprint 2)
- `ocr_triage_has_text` (encoded bytes) → **stub** (Sprint 2, needs libjpeg-turbo + libspng)
- Scalar reference implementation, SIMD **not yet** (Sprint 3)

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
ctest --output-on-failure
```

Produces:
- `libocr_triage.so` / `.dll` / `.dylib` (shared) or `.a` / `.lib` (static)
- `test_basic` — unit tests
- `triage_cli` — demo CLI

## API

```c
#include <ocr_triage.h>

uint8_t gray[WIDTH * HEIGHT] = { /* ... */ };
ocr_triage_verdict_t v = ocr_triage_has_text_gray(gray, WIDTH, HEIGHT, NULL);

if (v.has_text) {
    printf("Text detected (score %.3f in %u µs) — send to OCR engine.\n",
        v.score, v.elapsed_us);
} else {
    printf("No text (score %.3f in %u µs) — skip.\n",
        v.score, v.elapsed_us);
}
```

## Algorithm

1. **Otsu binarize** — adaptive threshold from histogram, polarity-invariant.
2. **Horizontal edge density** — 0/1 transitions per row on binary image.
3. **Row-projection variance** — text rows dense, interline sparse → high variance.
4. **2×2 regional TOP-K** — picks the strongest quadrants; uniform background does not dilute.
5. **Coverage filter** — foreground ratio must be in text-typical range (3-30%).

Score is the geometric mean of edge × variance, scaled by the coverage weight.

## Roadmap

- [x] Sprint 1: iskelet, scalar raw-pixel path, tests, CLI
- [ ] Sprint 2: libjpeg-turbo + libspng for encoded bytes (`ocr_triage_has_text`)
- [ ] Sprint 3: SIMD kernels (AVX2, NEON) + runtime dispatch
- [ ] Sprint 4: `ocr_triage_has_text_with_image` for OCR handoff (+ PGM encode)
- [ ] Sprint 5: Rust FFI wrapper (`ocr-triage-c-sys`) for Kreuzberg integration
- [ ] Sprint 6: cross-platform CI (Linux / macOS / Windows / ARM64) + fuzzing

## License

Dual-licensed under either MIT ([LICENSE-MIT](LICENSE-MIT)) or Apache-2.0 ([LICENSE-APACHE](LICENSE-APACHE)), at your option.

## Credits

Design by **Hasan Salihoğlu**. Implementation co-authored with **Claude (Opus 4.6, Anthropic)**.
