# FAC LPR Engine

FAC LPR Engine is a reusable native **license plate recognition engine** written in C++20.

Give it an image or frame and it returns a technical recognition result with plate text, confidence/evidence and a clear recognition decision:

```text
ACCEPTED
REVIEW
REJECTED
```

The engine is designed to be embedded into other products through C++, a stable C ABI, C# P/Invoke, Python `ctypes` or another FFI-capable runtime.

> FAC LPR Engine recognizes plates. It does **not** decide whether a vehicle is allowed to enter, manage cameras, control barriers or own customer/business rules.

## What it does

```text
image / frame
    ↓
plate detection
    ↓
geometry validation + perspective correction
    ↓
crop generation / enhancement
    ↓
OCR
    ↓
confidence + layout evidence
    ↓
candidate fusion
    ↓
technical decision
    ↓
ACCEPTED / REVIEW / REJECTED
```

`ACCEPTED` means the recognition evidence is technically strong enough. It never means “grant access”.

## Why it exists as a separate engine

The engine is intentionally independent from FAC Access or any other host application.

This separation keeps responsibilities clean:

```text
FAC LPR Engine
"What plate is visible, and how trustworthy is the recognition?"

Host application
"What should I do with that plate?"
```

For example, FAC Access may receive an `ACCEPTED` recognition and still return `DENIED` because the vehicle does not have permission to enter.

## Main capabilities

- native C++20 recognition pipeline
- Turkish plate recognition
- plate detection and geometry correction
- OCR and candidate fusion
- confidence / evidence-based decisions
- explicit `ACCEPTED`, `REVIEW`, `REJECTED` semantics
- bounded native memory/resource behavior
- verified runtime model contracts
- SHA-256 model integrity checks
- stable C ABI v1
- C++, C#, Python and CMake consumer paths
- offline CLI for real-pipeline testing
- stage timing and technical diagnostics
- ABI, packaging, memory, performance, security and release validation

## Technology

| Area | Technology |
| --- | --- |
| Language | C++20 |
| Public ABI | C11-compatible C ABI v1 |
| Build | CMake 3.25+ / Ninja |
| Inference | ONNX Runtime |
| Image processing | OpenCV |
| Tests | GoogleTest / CTest |
| Logging | spdlog |
| Main targets | Windows x64, Linux x64 |
| Additional validation | macOS ARM64 |

## Repository layout

```text
fac-lpr-engine/
├── include/                 # public C++ / C ABI headers
├── src/                     # Domain, Application and Infrastructure implementation
├── tests/                   # unit, integration, regression and real-model tests
├── tools/                   # CLI, evaluation and resource tools
├── samples/                 # consumer examples
├── cmake/                   # CMake package/export helpers
├── scripts/                 # bootstrap/validation/release scripts
├── docs/                    # detailed technical and release documentation
├── models/                  # model artifacts/documentation
├── PRODUCT.md               # canonical product + architecture specification
├── AGENTS.md                # AI-agent / maintainer project guide
└── README.md
```

## Active production models

The production pipeline currently expects:

```text
best.onnx              # detector / keypoints
lprnet_turkey.onnx     # Turkish plate OCR
```

These models are governed by explicit runtime contracts. A model is not accepted simply because an ONNX file loads successfully.

The current OCR contract includes:

```text
input:  float32 [1,3,40,160]
output: float32 [1,34,24]
layout: BCT
blank index: 33
charset: 0123456789ABCDEFGHIJKLMNOPRSTUVYZ
```

Model tensor names, dimensions, preprocessing, class order, charset, blank semantics and detector keypoint behavior are production contracts and are regression tested.

## Public C ABI

The stable C interface is defined in:

```text
include/fac_lpr/fac_lpr_engine.h
```

Primary lifecycle functions:

```c
fac_lpr_engine_create_v1(...);
fac_lpr_engine_recognize_v1(...);
fac_lpr_engine_destroy_v1(...);
fac_lpr_get_last_error_v1(...);
```

Important ABI rules:

- opaque engine handle
- no C++ exception crosses the ABI
- caller-owned result buffer
- two-call required-size pattern
- explicit version/size contracts
- nested data represented with buffer-relative offsets/counts
- ABI compatibility checked by CI

## Quick build

### Linux / GCC

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure
```

Release:

```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release --output-on-failure
```

### Linux / Clang

```bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug --output-on-failure
```

### Windows / MSVC

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
cmake --build --preset windows-msvc-release
```

Normal builds are out-of-source. Validation configurations treat warnings as errors where configured.

## Offline CLI

When built with:

```text
FAC_LPR_BUILD_LPR_CLI=ON
```

`fac-lpr-cli` runs the real production pipeline against JPG/PNG input.

Example:

```bash
fac-lpr-cli plate.jpg \
  --model-dir ./models \
  --config ./path/to/model-contract.conf \
  --json
```

Useful options include:

```text
--json
--debug-evidence
--model-dir <path>
--config <path>
--log-level trace|debug|info|warn|error
```

In JSON mode stdout remains machine-readable.

## Architecture

Dependency direction is inward:

```text
Public API / Composition Root
            ↓
      Infrastructure
            ↓
       Application
            ↓
          Domain
```

- **Domain** contains vendor-independent recognition concepts.
- **Application** owns orchestration and recognition policies/ports.
- **Infrastructure** owns ONNX Runtime, OpenCV, model loading and concrete runtime adapters.
- **Public API / composition root** exposes stable integration surfaces and assembles the production pipeline.

Vendor/runtime types must not leak into Domain, Application or the public C ABI.

## Reliability and safety principles

The engine is built to fail closed rather than return optimistic garbage.

Core rules include:

- malformed input is rejected safely
- model checksum/contract mismatch blocks activation
- external dimensions and allocation arithmetic are validated
- queues/workspaces/buffers remain bounded
- native ownership uses RAII
- C++ exceptions never cross the C ABI
- degraded execution is surfaced explicitly
- sensitive images/crops/full plate text are not routine diagnostic logs

## Validation and release gates

Production readiness includes more than unit tests.

The repository contains validation for areas such as:

- unit and integration tests
- real-model inference
- golden regression
- Linux x64 Debug/Release validation
- macOS ARM64 validation
- sanitizer/static-analysis/fuzz paths
- memory/resource stress
- performance regression
- ABI compatibility
- C / C# / Python consumers
- CMake package consumption
- security/dependency checks
- SBOM/release metadata
- production-readiness
- release-readiness

A skipped workflow is not automatically a passed workflow. Release policy is fail-closed and must be satisfied for the exact candidate being promoted.

## Development branches

```text
main  → stable / release
 dev  → active development / release candidate
```

Changes normally land on `dev`, pass applicable validation and are then promoted to `main` through the release process.

## Documentation

Start with the document that matches your role:

- [`README.md`](README.md) — understandable product/build/integration overview
- [`AGENTS.md`](AGENTS.md) — required starting context for AI coding agents and new maintainers
- [`PRODUCT.md`](PRODUCT.md) — canonical product, architecture, model, ABI and release contracts
- [`docs/`](docs/) — detailed technical, operational and release documentation

If you are an AI coding agent, read `AGENTS.md` before making changes.

## Current status

The initial production-hardening roadmap is complete. The engine already has the native recognition pipeline, model-contract enforcement, stable C ABI, consumer coverage, resource controls and release/readiness gates needed to operate as an independent embeddable product.

It is currently consumed by FAC Access through its public C ABI, while remaining independent from FAC Access business/access-control rules.
