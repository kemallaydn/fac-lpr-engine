# FAC LPR Engine — AI / Agent Project Guide

This file is the fastest reliable entry point for AI coding agents and new maintainers working on FAC LPR Engine.

It explains the product boundary, architecture, production contracts, current release state and the rules that must remain true while the engine evolves.

> Important: this is a continuity/navigation guide. `PRODUCT.md`, code, tests, model contracts, release policy and executed CI evidence remain authoritative for their own concerns.

## 1. Read this repository in this order

Before changing code:

1. `AGENTS.md` — orientation and non-negotiable working rules.
2. `README.md` — human-facing product/build/integration overview.
3. `PRODUCT.md` — canonical product, architecture, model, ABI and release specification.
4. Relevant files under `docs/` for the area being changed.
5. Relevant public headers under `include/` if changing integration surfaces.
6. Relevant tests and model-contract fixtures.
7. Relevant GitHub issue/PR and CI evidence.

Do not assume documentation is newer than code. If documentation and implementation disagree, determine which side is stale and update the documentation in the same coherent change.

## 2. What FAC LPR Engine is

FAC LPR Engine is an independent, reusable, production-grade native **license plate recognition engine** written in C++20.

It turns an input image/frame into a technically justified recognition result.

Conceptual pipeline:

```text
image / frame
    ↓
plate detection
    ↓
geometry validation
    ↓
perspective alignment
    ↓
crop generation / enhancement
    ↓
OCR recognition
    ↓
confidence calibration + layout evidence
    ↓
candidate fusion
    ↓
technical decision
    ↓
ACCEPTED / REVIEW / REJECTED
```

The engine is a recognition component, not an access-control application.

## 3. Product boundary

The engine owns:

- plate detection
- plate geometry evaluation
- perspective rectification
- crop generation / enhancement
- OCR inference
- recognition ensemble behavior
- confidence and evidence collection
- plate-layout evidence
- candidate fusion
- technical recognition decision
- model loading / verification
- bounded native execution
- public C++ and C integration surfaces
- technical diagnostics / stage timing

The engine does **not** own:

- vehicle authorization
- barrier control
- registered vehicles
- visitors
- customer/user permissions
- backend/database state
- camera lifecycle / RTSP
- camera discovery
- application UI
- payment/licensing business rules
- model training lifecycle
- consuming-product audit persistence

The most important semantic rule is:

> `ACCEPTED` means recognition evidence is technically strong enough. It never means “grant access”.

FAC Access or another consuming product must make its own business decision.

## 4. Repository map

```text
fac-lpr-engine/
├── include/                  # public C++ / C ABI headers
├── src/                      # Domain / Application / Infrastructure implementation
├── tests/                    # unit, integration, regression and real-model validation
├── tools/                    # CLI, evaluation, memory/resource tools
├── samples/                  # consumer examples (including Python ctypes)
├── cmake/                    # package/export helpers
├── scripts/                  # bootstrap/validation/release scripts
├── docs/                     # detailed technical/release documentation
├── models/                   # runtime model artifacts and model documentation
├── CMakeLists.txt
├── CMakePresets.json
├── PRODUCT.md                # canonical product/architecture/runtime specification
├── AGENTS.md                 # AI/maintainer continuity guide
└── README.md                 # human-facing repository entry point
```

## 5. Architecture

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

### Domain

Contains vendor-independent recognition concepts/value types.

Must not depend on:

- ONNX Runtime
- OpenCV
- filesystem/runtime adapters
- platform SDKs
- public ABI wire-layout concerns

### Application

Owns recognition orchestration and vendor-neutral ports/policies.

Representative contracts include:

```text
IPlateDetector
IPlateGeometryEvaluator
IPlateAligner
ICropGenerator
IConfidenceCalibrator
IPlateLayoutAnalyzer
ICandidateFusion
IDecisionPolicy
```

### Infrastructure

Owns concrete technology/runtime details:

- ONNX Runtime sessions/providers
- OpenCV operations
- model loading/checksum verification
- detector/OCR adapters
- concrete image/workspace implementation
- concurrency/runtime adapters

### Public API / composition root

Owns stable integration surfaces and assembles the production pipeline.

Vendor/runtime types must not leak into Domain, Application or the public C ABI.

## 6. Production composition

The current production recognition pipeline is conceptually:

```text
best.onnx
→ OnnxSession
→ YoloPoseOnnxDetector
→ PlateGeometryEvaluatorAdapter
→ OpenCvPerspectiveAligner
→ CropHypothesisGenerator
→ lprnet_turkey.onnx / OnnxSession
→ LprNetOnnxOcrAdapter
→ GenericOnnxOcrRecognizer
→ RecognitionEnsemble
→ IdentityConfidenceCalibrator
→ ConnectedComponentPlateLayoutAnalyzer
→ WeightedMultiCropCandidateFusion
→ SafeRecognitionDecisionPolicy
→ LprPipeline
```

Do not replace this with a parallel hidden inference path unless the product architecture explicitly changes.

## 7. Active model contracts

### Detector

Active detector:

```text
best.onnx
```

Authoritative contract:

```text
input  images   float32 [1,3,960,960]
output output0  float32 [1,17,18900]
```

Important assumptions:

- RGB CHW
- scale `1/255`
- letterbox pad `114`
- features-first output
- 4 bbox values
- one plate-class score
- no separate objectness score
- 4 keypoints with `(x, y, confidence)`

Geometry code must normalize/reorder corners itself rather than trusting undocumented model keypoint ordering.

### OCR

Active OCR:

```text
lprnet_turkey.onnx
```

Current production family: **V2 Mixed Epoch 7**.

Contract:

```text
input  input   float32 [1,3,40,160]
output output  float32 [1,34,24]
layout BCT
blank_index 33
charset 0123456789ABCDEFGHIJKLMNOPRSTUVYZ
```

Preprocessing:

```text
resize 160x40
BGR
float32
(img - 127.5) / 128
HWC → CHW
batch → [1,3,40,160]
```

These values are production contracts, not hints.

Do not infer or casually change:

- tensor names
- dimensions
- layout
- preprocessing
- class ordering
- charset
- blank index
- keypoint interpretation
- CTC semantics

Any model replacement must explicitly update the contract and regression tests.

## 8. Recognition decision semantics

Public technical results:

```text
ACCEPTED
REVIEW
REJECTED
```

The result is intentionally richer than a plate string.

Recognition evidence can include:

- detector confidence
- geometry quality
- crop quality
- OCR/provider evidence
- calibrated candidate confidence
- layout evidence
- alternative candidates
- degraded state
- provider failures
- stage timings
- technical reason codes

Default philosophy:

- strong coherent evidence → `ACCEPTED`
- uncertain/conflicting/degraded-but-reviewable evidence → `REVIEW`
- invalid/no trustworthy result/fatal technical condition → `REJECTED` or explicit failure as defined by the contract

Never convert uncertainty into optimistic plate text merely to increase apparent recall.

## 9. Public C ABI v1

Stable header:

```text
include/fac_lpr/fac_lpr_engine.h
```

Primary lifecycle:

```c
fac_lpr_engine_create_v1(...)
fac_lpr_engine_recognize_v1(...)
fac_lpr_engine_destroy_v1(...)
fac_lpr_get_last_error_v1(...)
```

ABI rules:

- opaque engine handle
- C11-compatible public surface
- no C++ exception crosses the ABI
- explicit struct size/version contract
- caller-owned result buffer
- two-call required-size pattern
- nested output represented with buffer-relative offsets/counts
- text uses explicit offset + length
- alignment/range/overflow validation before returning wire data
- destroy semantics must remain safe and deterministic

Do not casually alter field order, packing, enum values, ownership or exported symbol names. ABI compatibility is a release requirement.

## 10. Memory and concurrency model

The engine is intended for long-running production processes.

Core rules:

- use RAII ownership
- avoid scattered raw `new`/`delete`
- input image memory remains caller-owned unless an API explicitly says otherwise
- queues/workspaces/result buffers are bounded
- external dimensions/stride/offset arithmetic is checked before allocation/access
- worker failures must not corrupt unrelated work
- reusable workspaces should reach predictable steady-state capacity after warm-up
- shutdown semantics must be explicit

Strided image extent uses checked arithmetic equivalent to:

```text
(height - 1) * stride + packed_row_bytes
```

Integer overflow or impossible dimensions must fail before memory access.

## 11. Model lifecycle and integrity

Model activation is all-or-nothing.

Model metadata includes concepts such as:

- logical model identity/type
- version
- path
- exact file size
- SHA-256

Activation must reject unsafe/ambiguous state, including:

- path traversal
- canonical path escape
- unexpected absolute paths where forbidden
- duplicate model identity
- missing model
- size mismatch
- checksum mismatch
- incomplete/invalid model contract

A failed replacement must not partially replace a previously valid active model set.

## 12. Error / failure philosophy

The engine is fail-closed at external boundaries.

Examples:

- malformed image → reject safely
- unsafe dimensions/resource request → fail before allocation
- invalid model contract → do not activate
- checksum mismatch → do not activate
- provider/runtime failure → surface it explicitly
- invalid wire result values → do not expose them through C ABI
- exception → translate before crossing C boundary
- degraded execution → mark degraded rather than pretending it was normal

## 13. Privacy / observability rules

Do not log by default:

- raw input images
- plate crops
- full plate text as routine diagnostic payload
- secrets/credentials

Prefer observability based on:

- stage timing
- provider status/failure code
- resource telemetry
- model identity/version/checksum metadata
- non-sensitive technical decision reasons

Persistence of sensitive plate/image data belongs to the consuming product's explicit policy, not hidden inside this engine.

## 14. Consumer surfaces

The engine is designed to be embedded through multiple surfaces:

- native C++
- stable C ABI
- C# P/Invoke
- Python `ctypes`
- installed/exported CMake package

Consumer compatibility is a release concern. Internal refactoring is allowed only while declared public contracts remain compatible.

FAC Access consumes the C ABI through its .NET Device Service Infrastructure layer.

## 15. Build / validation baseline

Primary technology baseline:

- C++20
- C11-compatible public ABI
- CMake 3.25+
- Ninja where applicable
- ONNX Runtime
- OpenCV
- GoogleTest / CTest
- spdlog
- Windows x64 / MSVC
- Linux x64 / GCC and Clang
- macOS ARM64 validation

Representative Linux development flow:

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure
```

Use repository presets/scripts rather than inventing a parallel build procedure.

## 16. Release and CI philosophy

Production readiness is broader than unit tests.

The repository contains validation/gates for areas such as:

- unit tests
- integration tests
- real-model execution
- golden regression
- Linux x64 Debug/Release Docker validation
- macOS ARM64 validation
- sanitizers
- static analysis
- fuzz paths
- memory stress
- performance regression
- ABI compatibility
- resource budgets
- C / C# / Python consumers
- CMake package consumption
- dependency/security scanning
- SBOM/release metadata
- production-readiness
- release-readiness

Critical rule:

> A workflow existing is not proof it passed, and a skipped workflow is not a passed workflow.

Release promotion is fail-closed and must use evidence for the exact release candidate.

Do not weaken warnings, analyzers, ABI checks, security checks or release gates just to get green CI.

## 17. Branching model

- `main` — stable/release branch
- `dev` — active release-candidate/development branch

Changes should normally land on `dev`, pass applicable validation, then be promoted to `main` through the release process.

Old feature branches may remain after work is merged. Their existence is not evidence that a feature is unfinished.

## 18. Current project state snapshot

Snapshot date: **2026-08-25**.

Verify live GitHub state before acting because this section naturally ages.

The initial production-hardening roadmap (`#1`–`#78`) is complete.

The engine has already developed the core production concerns described in `PRODUCT.md`, including:

- recognition pipeline composition
- model contracts
- bounded native execution
- C ABI v1
- C/C#/Python consumer coverage
- model integrity
- memory/resource validation
- packaging/CMake export
- security/release metadata
- production/release readiness gates

The engine is also being consumed by FAC Access through its public C ABI. The FAC Access integration work uses the engine as a separate independent product rather than embedding FAC Access business rules into this repository.

Do not reopen already-completed foundation work without evidence of an actual product defect or new requirement.

## 19. How an AI agent should work here

For every task:

1. Determine whether the change belongs to Domain, Application, Infrastructure, public API, model contract, tooling, packaging or release validation.
2. Read the relevant section of `PRODUCT.md` and detailed docs.
3. Inspect existing implementation and tests before creating a new abstraction.
4. Preserve vendor isolation and inward dependency direction.
5. Preserve model/ABI contracts unless the task explicitly changes them.
6. Add/update regression coverage for any contract change.
7. Keep memory/resource behavior bounded.
8. Check real-model behavior when the change can affect inference semantics.
9. Check ABI/consumer compatibility when public integration surfaces change.
10. Check the exact CI evidence before declaring work complete.
11. Update canonical documentation if stable product/architecture/runtime contracts changed.

## 20. Things an AI must not assume

Do not assume:

- `ACCEPTED` means a vehicle is authorized
- model shapes/preprocessing can be inferred from an ONNX file name
- a new OCR/detector model is compatible because it loads
- a feature branch means the issue is still open
- `main` is always ahead of `dev`
- a skipped validation counts as success
- macOS success proves Windows packaging
- unit-test success proves ABI compatibility
- internal C++ types may cross the C ABI
- caller/engine buffer ownership may be changed casually
- unbounded queues/workspaces are acceptable because test data is small
- logging plate text/images is harmless

Verify instead of guessing.

## 21. Documentation ownership

Use documents for distinct purposes:

- `README.md` — human-facing product/build/integration introduction.
- `AGENTS.md` — continuity and operating rules for AI agents/new maintainers.
- `PRODUCT.md` — canonical product, architecture, model, ABI and release contracts.
- `docs/` — detailed topic-specific technical/operational/release documentation.
- GitHub issues — scoped work and acceptance criteria.
- PRs/CI — implementation and executed evidence.

If a change modifies a production model contract, public ABI, product responsibility boundary or release rule, update `PRODUCT.md` and the relevant tests/docs in the same change.

Do not turn `PRODUCT.md` into a running diary of every issue. Stable intent belongs there; implementation history belongs in GitHub.
