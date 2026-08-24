# FAC LPR Engine

Production-grade, reusable native **license plate recognition (LPR)** engine written in C++20.

FAC LPR Engine turns an image/frame into a technical plate-recognition decision through a deterministic, bounded and testable pipeline. It is designed to be embedded into other products through C++, a stable C ABI, C#, Python or another FFI-capable runtime.

> The engine recognizes plates. It does **not** decide whether a vehicle should be allowed through a gate, own customer/business rules, manage cameras, or persist application state.

## What it does

```text
image / frame
    ↓
plate detection
    ↓
geometry validation + perspective alignment
    ↓
crop generation + enhancement
    ↓
OCR ensemble
    ↓
confidence calibration + layout evidence
    ↓
candidate fusion
    ↓
technical decision
    ↓
ACCEPTED / REVIEW / REJECTED
```

`ACCEPTED` means the recognition evidence is technically strong enough. It never means “grant access”.

## Production status

The implementation roadmap is complete and the `dev` branch is the current release candidate.

Current validated release-candidate gates include:

- Linux x64 full Debug + Release validation in Docker
- production-readiness
- release-readiness
- ABI compatibility
- CMake package smoke
- resource-budget validation
- evaluation-tool validation
- release metadata validation

The active promotion PR is `dev -> main`. A production release is considered complete only after the exact release candidate is promoted and tagged under the repository release policy.

For the canonical product definition, architecture, runtime contracts and release rules, read [`PRODUCT.md`](PRODUCT.md).

## Core design goals

- **Native and embeddable:** C++20 implementation with stable C ABI v1.
- **Vendor-neutral core:** ONNX Runtime/OpenCV stay at infrastructure boundaries.
- **Fail-closed:** invalid models, malformed inputs, unsafe resource requests and incomplete release evidence are rejected.
- **Bounded resources:** queues, workspaces, buffers and external dimensions are explicitly limited.
- **Deterministic contracts:** model tensor shapes, preprocessing, charset and decoder behavior are regression locked.
- **Observable without leaking data:** stage timings and technical diagnostics are available without logging sensitive images or full plate data by default.
- **Release-gated:** ABI, packaging, memory, performance, security and real-model behavior are treated as release requirements, not optional cleanup.

## Technology

| Area | Choice |
| --- | --- |
| Language | C++20 |
| Public ABI | C11-compatible C ABI v1 |
| Build | CMake 3.25+ / Ninja |
| Inference | ONNX Runtime |
| Image processing | OpenCV |
| Tests | GoogleTest / CTest |
| Logging | spdlog |
| Platforms | Windows x64, Linux x64, macOS ARM64 validation |

## Repository layout

```text
fac-lpr-engine/
├── include/                 # Public C++ and C ABI headers
├── src/                     # Domain, application and infrastructure implementation
├── tests/                   # Unit, integration, real-model and regression tests
├── tools/
│   ├── lpr-cli/             # Offline real-pipeline CLI
│   ├── memory-stress/       # Memory/resource stress validation
│   └── ...                  # Evaluation/inspection utilities
├── samples/                 # Consumer examples, including Python ctypes
├── cmake/                   # CMake package/export helpers
├── scripts/                 # Dependency/bootstrap/release validation scripts
├── docs/                    # Architecture, operations and release documentation
├── models/                  # Runtime/release model artifacts and model documentation
├── CMakeLists.txt
├── CMakePresets.json
└── PRODUCT.md               # Canonical product and architecture specification
```

## Build requirements

Typical development environment:

- CMake 3.25+
- C++20-capable compiler
- Ninja
- Git
- Python 3 for supporting scripts/samples
- platform toolchain (`MSVC`, `GCC` or `Clang`)

Dependencies are bootstrapped through the repository scripts/vcpkg configuration. Generated build output belongs under `build/`.

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

All normal builds are out-of-source. Warnings are treated as errors in validation configurations.

## Linux x64 validation through Docker

The repository includes a self-hosted `linux-x64-docker-validation` workflow that validates Linux/amd64 from the macOS ARM64 runner through Docker.

It performs clean Debug and Release builds, runs the CTest suites and consumer/runtime smoke validation. vcpkg packages use a persistent binary cache, so heavy dependencies such as OpenCV do not need to be rebuilt on every run.

The workflow uses concurrency cancellation, so a newer `dev` revision replaces obsolete queued/running validation for the same branch.

## Offline CLI

When built with:

```text
FAC_LPR_BUILD_LPR_CLI=ON
```

`fac-lpr-cli` executes the real production pipeline against JPG/PNG input.

Example shape:

```bash
fac-lpr-cli plate.jpg \
  --model-dir ./models \
  --config ./path/to/model-contract.conf \
  --json
```

Supported options include:

```text
--json
--debug-evidence
--model-dir <path>
--config <path>
--log-level trace|debug|info|warn|error
```

In JSON mode stdout is kept machine-readable; diagnostics must not corrupt the JSON contract.

## Public C ABI

The stable C interface lives at:

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

Important ABI properties:

- opaque engine handle
- no C++ exception crosses the ABI
- caller-owned recognition result buffer
- two-call required-size pattern
- buffer-relative offsets for nested result data
- explicit struct size/version contract
- ABI compatibility checked by CI

See [`PRODUCT.md`](PRODUCT.md) and the repository docs for the full ownership/wire-format rules.

## Runtime model contract

The current production pipeline expects two active ONNX models:

```text
best.onnx              # plate detector / keypoints
lprnet_turkey.onnx     # Turkish plate OCR
```

Models are not accepted merely because a file with the expected name exists. Activation validates the model contract, file containment, size and SHA-256 metadata and fails atomically when the contract is invalid.

The current OCR model contract is intentionally strict:

```text
input:  float32 [1,3,40,160]
output: float32 [1,34,24]
layout: BCT
blank index: 33
charset: 0123456789ABCDEFGHIJKLMNOPRSTUVYZ
```

Do not infer or casually change tensor names, dimensions, preprocessing, class order, keypoint interpretation or CTC blank semantics. Those values are part of the production contract and are regression tested.

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

- **Domain** contains vendor-independent recognition concepts/value types.
- **Application** owns orchestration, policies and ports/interfaces.
- **Infrastructure** owns ONNX Runtime, OpenCV, model loading, native image and concrete runtime adapters.
- **Public API / composition** exposes stable integration surfaces and assembles the concrete pipeline.

Vendor types must not leak into Domain, Application or the public C ABI.

## Testing and release gates

The repository contains validation for areas including:

- unit and integration behavior
- real-model inference
- golden regression
- memory stress and resource budgets
- sanitizer/static-analysis/fuzz paths
- performance regression
- ABI compatibility
- C / C# / Python consumers
- package export/install smoke
- dependency security and SBOM
- release metadata
- production/release readiness

A skipped workflow is not automatically equivalent to a passed workflow. Release policy is **fail-closed**: the exact candidate must have the evidence required by the applicable release gate.

## Security, memory and privacy rules

- RAII ownership throughout native code.
- No scattered raw `new`/`delete` ownership.
- Input image memory remains caller-owned.
- External dimensions/stride/offset arithmetic is checked.
- Workspaces, queues and result buffers are bounded.
- Model path traversal/root escape is rejected.
- Model files are checksum validated before activation.
- C++ exceptions never cross the C ABI.
- Sensitive images/crops/full plate text/secrets are not logged by default.

## Development workflow

- `main`: stable/release branch
- `dev`: active release-candidate branch

Changes should land on `dev`, pass the relevant validation, then be promoted to `main` through the release PR. Do not weaken gates to make a build green; fix the cause or document a genuinely non-applicable condition in the appropriate security/release policy.

## Documentation map

Start here:

1. [`README.md`](README.md) — build, integration and repository entry point.
2. [`PRODUCT.md`](PRODUCT.md) — canonical product definition, architecture and runtime/release contracts.
3. [`docs/`](docs/) — detailed architecture, operational, packaging and release documentation.

## License / distribution

Distribution terms and release artifacts must follow the repository's release metadata and packaging policy. Runtime model artifacts are versioned/validated independently from the engine binary through explicit model metadata and checksums.
