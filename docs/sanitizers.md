# Sanitizer validation

FAC LPR Engine exposes a single CMake cache variable:

```text
FAC_LPR_SANITIZER=none|address|thread
```

`address` enables AddressSanitizer. On GCC/Clang builds leak detection is enabled at test runtime through `ASAN_OPTIONS=detect_leaks=1`; MSVC AddressSanitizer does not provide LeakSanitizer. `thread` enables ThreadSanitizer and is intentionally rejected on MSVC.

## Presets

Linux Clang ASan/LSan:

```bash
cmake --preset linux-clang-asan
cmake --build --preset linux-clang-asan
ctest --preset linux-clang-asan
```

Linux Clang TSan:

```bash
cmake --preset linux-clang-tsan
cmake --build --preset linux-clang-tsan
ctest --preset linux-clang-tsan
```

Windows MSVC ASan:

```powershell
cmake --preset windows-msvc-asan
cmake --build --preset windows-msvc-asan
ctest --preset windows-msvc-asan
```

The presets use `halt_on_error=1`; sanitizer findings therefore produce non-zero test exits and fail CTest/CI.

## Suppression policy

There are currently **no sanitizer suppressions** in the repository. A future third-party false positive may only be suppressed when all of the following are documented in the same change:

1. exact third-party library and version;
2. sanitizer finding/stack proving the report is outside FAC LPR-owned code;
3. the narrowest possible symbol/path-specific suppression;
4. an expiry/removal condition.

Project-owned memory errors, leaks or races must never be suppressed. Broad wildcard suppressions are prohibited.

## CI

`.github/workflows/sanitizers.yml` is manual-only and uses self-hosted runners so it does not consume GitHub-hosted runner minutes. Linux provides ASan/LSan and TSan jobs; Windows provides MSVC ASan. Any configure, build, sanitizer runtime or CTest failure fails the job.
