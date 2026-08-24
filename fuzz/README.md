# Fuzzing

FAC LPR Engine fuzz targets are opt-in and require Clang/AppleClang with libFuzzer support.

Configure and build:

```bash
cmake -S . -B build/fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DFAC_LPR_BUILD_FUZZERS=ON
cmake --build build/fuzz --parallel
```

Targets:

- `fac_lpr_fuzz_image`: image dimension/stride/pixel-format/data-size boundary
- `fac_lpr_fuzz_config`: engine config and C ABI config boundary
- `fac_lpr_fuzz_abi`: public C ABI image/output-buffer boundary
- `fac_lpr_fuzz_grammar`: Turkish plate normalization/validation/prefix/province grammar
- `fac_lpr_fuzz_ctc_decoder`: bounded CTC logits/classes/timesteps decoder input

Example:

```bash
mkdir -p build/fuzz/artifacts/fuzz-crashes/image
build/fuzz/artifacts/fuzz/fac_lpr_fuzz_image \
  fuzz/corpus/image \
  -artifact_prefix=build/fuzz/artifacts/fuzz-crashes/image/ \
  -max_total_time=60 \
  -rss_limit_mb=2048
```

All fuzz targets compile with libFuzzer + AddressSanitizer + UndefinedBehaviorSanitizer and `-fno-sanitize-recover=all`. A sanitizer finding therefore fails the run.

## Allocation limits

Fuzz targets must never allocate directly from unconstrained fuzz dimensions. Image/ABI fuzzing reuses the caller input/fixed output storage, grammar input is capped at 256 bytes, and CTC tensors are capped at 64×64 float elements. New targets must define an explicit allocation/input ceiling before they are accepted.

## Crash reproducers

CI writes libFuzzer artifacts below `artifacts/fuzz-crashes/<target>/` and uploads that directory even when the fuzz step fails. A discovered reproducer should be retained until fixed; after a fix it should be promoted into the corresponding seed/regression corpus when practical.
