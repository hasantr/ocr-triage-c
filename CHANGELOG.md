# Changelog

All notable changes to `ocr-triage-c` are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/).

## [0.2.0-alpha] — 2026-04-16

Sprint 2 complete — encoded JPEG/PNG path is live.

### Added
- `ocr_triage_has_text(bytes, len)` — full encoded-bytes pipeline.
- `ocr_triage_has_text_with_image(bytes, len, &verdict, &image)` — decode + verdict
  + reusable full-res grayscale buffer (for OCR engine handoff).
- libjpeg-turbo 3.1.4 vendored via `ExternalProject_Add` (NASM SIMD auto-detect).
  JPEG scaled decode uses DCT-level 1/8 path (`TJSCALED` helper), not post-resize.
- libspng 0.7.4 + zlib-ng 2.1.6 (ZLIB_COMPAT) vendored via `ExternalProject_Add`.
  PNG decode handles grayscale / RGB / RGBA / palette uniformly via `SPNG_FMT_RGBA8`.
- `bench_encoded` benchmark: per-file 20-iteration latency.

### Performance (scalar, Windows MinGW GCC 8.1 + NASM SIMD)
- 220 KB JPEG (CD cover): **~1.3 ms**
- 4 KB PNG (512×512 solid): **~1.0 ms**
- 94 KB PNG (512×512 photo): **~2.5 ms**
- 2.8 MB PNG (8 MP, 256-color): ~36 ms
- 1.9 MB JPEG (8 MP A4 scan): ~37 ms

A4 JPEG Windows latency higher than expected (target ~5 ms via libjpeg-turbo
SIMD); suspected native-build-specific SIMD dispatch issue on MinGW. Linux CI
should hit ~2-4 ms. Verdicts match pure-Rust reference exactly (score parity:
0.832 PNG / 0.839 JPEG on the same content).

### Accuracy parity with pure-Rust `ocr-triage`
- Same Otsu + edge + variance + 2×2 regional + coverage algorithm.
- Rounded-to-tenth score diff on shared testset: 0.000.

### Known limits
- No SIMD kernels yet for the triage score phase (Sprint 3 target).
- Encoded path does not expose `ocr_triage_has_text_with_image` from CLI /
  examples yet — Sprint 4 will add a Tesseract integration example.

## [0.1.0-alpha] — 2026-04-16

Initial alpha release — Sprint 1 complete.

### Added
- Public C99 API surface (`include/ocr_triage.h`):
  - `ocr_triage_has_text_gray` — raw 8-bit grayscale classifier.
  - `ocr_triage_has_text_rgb` — raw RGB888 classifier (in-pass BT.601 luma).
  - `ocr_triage_has_text` — encoded bytes (stub, Sprint 2).
  - `ocr_triage_has_text_with_image` — decode + verdict + reusable buffer (stub, Sprint 2).
  - `ocr_triage_image_to_pgm` — PGM encode for OCR engine handoff.
  - `ocr_triage_config_default`, `TriageVerdict`, `TriageConfig`, `TriageImage`.
- Scalar reference implementation:
  - Otsu binarize (polarity-invariant).
  - Horizontal edge density + row-projection variance.
  - Global + 2×2 regional TOP-K scoring.
  - Foreground coverage filter.
  - Stride subsample (raw-pixel path, speed-biased).
  - Area-average subsample (encoded path, quality-biased, Sprint 2 ready).
- CMake build system (shared + static), MinGW / GCC / Clang / MSVC compatible.
- CTest sanity suite (6 tests, empty / solid / mode / elapsed / rgb).
- `triage_cli` demo CLI.
- `bench_raw` micro-benchmark.
- Dual MIT / Apache-2.0 license.

### Performance (scalar, no SIMD yet)
- Raw 256×256 gray: ~80-130 µs
- Raw A4 (2480×3508) gray: ~150-220 µs
- Beats companion pure-Rust `ocr-triage` crate on A4 raw path by ~2× (218 µs vs 500 µs).

### Known limits
- Encoded bytes path (`ocr_triage_has_text`) not implemented — returns no-text for any input.
- SIMD kernels pending Sprint 3 (AVX2/NEON).
- Tested only on Windows x86_64 + MinGW so far.

## Roadmap

- **0.2** (Sprint 2) — libjpeg-turbo + libspng integration; `has_text(bytes)` working.
- **0.3** (Sprint 3) — AVX2 + NEON SIMD kernels + runtime dispatch.
- **0.4** (Sprint 4) — `has_text_with_image` OCR handoff API complete.
- **0.5** (Sprint 5) — Rust FFI wrapper (`ocr-triage-c-sys`) for Kreuzberg.
- **1.0** (Sprint 6-7) — Cross-platform CI, fuzzing, pkg-config + vcpkg packaging.
