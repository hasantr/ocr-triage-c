# Changelog

All notable changes to `ocr-triage-c` are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/).

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
