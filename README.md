# FAC LPR Engine

Reusable, production-grade license plate recognition engine.

Development happens on the `dev` branch. Runtime model artifacts are not committed to this repository.

## Foundation

- C++20
- CMake 3.25+
- Windows x64 / MSVC
- Linux x64 / GCC and Clang
- Out-of-source builds only
- Warnings treated as errors by default

## Repository layout

```text
fac-lpr-engine/
├── include/      # public headers
├── src/          # engine implementation
├── tests/        # unit/integration/performance tests
├── tools/        # CLI and model inspection utilities
├── cmake/        # reusable CMake modules
├── docs/         # architecture and operations documentation
└── models/       # documentation only; model binaries are external runtime artifacts
```

## Build presets

Linux GCC:

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug

cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
```

Linux Clang:

```bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug

cmake --preset linux-clang-release
cmake --build --preset linux-clang-release
```

Windows MSVC:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
cmake --build --preset windows-msvc-release
```

All generated build output is written below `build/` and ignored by Git.
