# FAC LPR Engine — AI / Agent Project Guide

This file is the fastest reliable entry point for AI coding agents and new maintainers working on FAC LPR Engine.

It explains the product boundary, architecture, production contracts, current temporal/stream design, release state and rules that must remain true while the engine evolves.

> Important: this is a continuity/navigation guide. `PRODUCT.md`, code, tests, model contracts, release policy and executed CI evidence remain authoritative for their own concerns.

---

## 1. Read this repository in this order

Before changing code:

1. `AGENTS.md` — orientation and non-negotiable working rules.
2. `README.md` — human-facing product/build/integration overview.
3. `PRODUCT.md` — canonical product, architecture, model, temporal, ABI and release specification.
4. `docs/stream-recognition-api.md` and `docs/temporal-stream-recognition.md` for stream/temporal work.
5. Other relevant files under `docs/`.
6. Relevant public headers under `include/` if changing integration surfaces.
7. Relevant tests/model-contract fixtures.
8. Relevant GitHub issue/PR and exact CI evidence.

Do not assume documentation is newer than code. If documentation and implementation disagree, determine which side is stale and update the stale side in the same coherent change.

---

## 2. What FAC LPR Engine is

FAC LPR Engine is an independent, reusable, production-grade native **license plate recognition engine** written in C++20.

Canonical per-frame pipeline:

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

---

## 3. Product boundary

The engine owns:

- plate detection
- geometry evaluation / rectification
- crop generation / enhancement
- OCR inference
- recognition ensemble behavior
- confidence/evidence collection
- plate-layout evidence
- within-frame candidate fusion
- technical recognition decision
- optional bounded cross-frame temporal consensus
- optional recognition-level stable emission / duplicate suppression
- stream-local recognition session state
- model loading / verification
- bounded native execution
- public C++ and stable C integration surfaces
- technical diagnostics / stage timing

The engine does **not** own:

- vehicle authorization
- barrier control
- registered vehicles
- visitors
- customer/user permissions
- backend/database state
- camera lifecycle / RTSP
- camera discovery/reconnect
- application UI
- payment/licensing business rules
- model training lifecycle
- business-level access-event cooldown/deduplication
- consuming-product audit persistence

Most important semantic rule:

> `ACCEPTED` means recognition evidence is technically strong enough. It never means “grant access”.

FAC Access or another consuming product must make its own business decision.

---

## 4. Repository map

```text
fac-lpr-engine/
├── include/                  # public C++ / C ABI headers
├── src/                      # Domain / Application / Infrastructure
├── tests/                    # unit, integration, temporal, regression, real-model
├── tools/                    # CLI, benchmark, evaluation, resource tools
├── samples/                  # consumer examples
├── cmake/                    # package/export helpers
├── scripts/                  # bootstrap/validation/release scripts
├── docs/                     # detailed technical/release documentation
├── models/                   # runtime model artifacts/docs
├── CMakeLists.txt
├── CMakePresets.json
├── PRODUCT.md                # canonical product/architecture/runtime specification
├── AGENTS.md                 # AI/maintainer continuity guide
└── README.md                 # human-facing repository entry point
```

---

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

Vendor-independent recognition concepts/value types.

Must not depend on:

- ONNX Runtime
- OpenCV
- filesystem/runtime adapters
- platform SDKs
- public ABI wire-layout concerns

### Application

Owns recognition orchestration and vendor-neutral ports/policies.

Representative contracts/components:

```text
IPlateDetector
IPlateGeometryEvaluator
IPlateAligner
ICropGenerator
IConfidenceCalibrator
IPlateLayoutAnalyzer
ICandidateFusion
IDecisionPolicy
LprPipeline
TemporalPlateConsensus
StablePlateEventFilter
RecognitionStreamSession
```

### Infrastructure

Owns concrete technology/runtime details:

- ONNX Runtime sessions/providers
- OpenCV operations
- model loading/checksum verification
- detector/OCR adapters
- native image/workspace implementation
- concrete concurrency/runtime adapters

### Public API / composition root

Owns stable integration surfaces and production assembly.

Vendor/runtime types must not leak into Domain, Application or the public C ABI.

---

## 6. Production composition

Current per-frame production recognition pipeline is conceptually:

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

---

## 7. Stateless vs stateful recognition

This distinction is now core project knowledge.

### Stateless path

`LprPipeline::recognize()` remains canonical and stateless per call:

```text
frame -> LprPipeline -> frame result
```

Do not add hidden global history to `LprPipeline`.

### Stateful optional stream path

For one logical camera/stream:

```text
frame
  ↓
LprPipeline
  ↓
frame_result
  ↓
TemporalPlateConsensus
  ↓
StablePlateEventFilter
  ↓
RecognitionStreamSessionResult
```

`RecognitionStreamSession` owns only bounded stream-local recognition state.

It does **not** own RTSP/network camera lifecycle.

---

## 8. TemporalPlateConsensus rules

`TemporalPlateConsensus` is already implemented. Do not create another temporal-fusion class because a future task mentions multi-frame recognition.

It exists to combine completed per-frame technical recognition results across ordered frames.

Current design principles:

- bounded history/window;
- configurable stale expiry;
- configurable minimum support/confidence;
- confidence-weighted support;
- recency weighting/decay;
- deterministic conflict/tie behavior;
- explicit reset;
- no unbounded state;
- out-of-order timestamps do not corrupt history;
- low-confidence/rejected/review-only evidence is not silently promoted to accepted;
- degraded accepted evidence preserves degraded state when stabilized.

Do not move existing within-frame candidate fusion into this layer.

---

## 9. RecognitionStreamSession rules

`RecognitionStreamSession` is the owner of stream-local state.

Important contract:

- one session = one temporal history + one recognition-emission suppression state;
- independent sessions cannot contaminate each other;
- mutable session operations are serialized by the session;
- `reset()` clears temporal/emission/timestamp state;
- `close()` is idempotent and prevents subsequent work according to the defined error contract;
- duplicate timestamps are handled by the tested contract;
- out-of-order timestamps are rejected without mutating prior history;
- raw images are not retained;
- history/state remains bounded.

### Ambiguous multi-plate frames

A frame with multiple recognitions remains visible in `frame_result`, but it is not fed into one single temporal plate identity.

Reason: without an explicit tracking identity, merging multiple vehicles would contaminate state.

Do not “fix” this by arbitrarily picking the first/highest-confidence plate unless product architecture explicitly introduces tracking identity.

---

## 10. StablePlateEventFilter rules

`StablePlateEventFilter` suppresses redundant **recognition emissions** from adjacent frames.

It is not FAC Access event deduplication.

Engine-level responsibilities:

- emit stable result after temporal support;
- suppress repeated stable emission of same plate inside bounded cooldown;
- allow materially different plate to break suppression;
- support reset/expiry;
- expose non-sensitive emitted/suppressed metrics.

Consuming-product responsibilities:

- whether same vehicle may enter again;
- whether a gate opens;
- authorization;
- audit/event persistence;
- customer-configured access cooldown.

Never move those business rules into FAC LPR Engine.

---

## 11. Active model contracts

### Detector

```text
best.onnx
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
- no separate objectness
- 4 keypoints with `(x,y,confidence)`

Geometry code normalizes/reorders corners itself.

### OCR

```text
lprnet_turkey.onnx
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
HWC -> CHW
batch -> [1,3,40,160]
```

These values are production contracts, not hints.

Do not infer/casually change tensor names, shapes, layout, preprocessing, class ordering, charset, blank index, keypoint interpretation or CTC semantics.

---

## 12. Recognition decision semantics

Public technical results:

```text
ACCEPTED
REVIEW
REJECTED
```

Evidence may include detector confidence, geometry/crop quality, OCR/provider evidence, calibrated confidence, layout evidence, alternative candidates, degraded state, provider failures, stage timings and technical reasons.

Default philosophy:

- strong coherent evidence → `ACCEPTED`
- uncertain/conflicting/degraded-but-reviewable evidence → `REVIEW`
- invalid/no trustworthy result/fatal technical condition → `REJECTED` or explicit failure

Temporal repetition is not permission to convert uncertainty into optimistic output.

---

## 13. Public C ABI v1

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
- no C++ exception crosses ABI
- explicit struct size/version
- caller-owned flat result buffer
- two-call required-size pattern
- buffer-relative offsets/counts
- explicit text offset/length
- alignment/range/overflow validation

### Critical stream decision

The current stream/session feature does **not** alter C ABI v1.

Do not add fields to existing v1 config/result records merely because stream recognition exists.

If downstream consumers later need a C ABI stream surface, create an additive separately versioned extension with a separate opaque stream handle and new symbols.

---

## 14. Memory and concurrency model

Core rules:

- RAII ownership;
- avoid scattered raw `new`/`delete`;
- input image memory caller-owned unless explicitly documented otherwise;
- queues/workspaces/result buffers bounded;
- temporal history/suppression state bounded;
- external dimensions/stride/offset arithmetic checked before allocation/access;
- worker failures isolated;
- reusable workspaces should reach predictable steady state;
- shutdown semantics explicit.

Do not add a second worker-pool implementation for stream recognition. Existing bounded concurrency infrastructure remains the foundation.

---

## 15. Error / failure philosophy

Fail closed at external boundaries.

Examples:

- malformed image → reject safely;
- unsafe dimensions/resource request → fail before allocation;
- invalid model contract → do not activate;
- checksum mismatch → do not activate;
- provider/runtime failure → surface explicitly;
- invalid wire result → do not expose through C ABI;
- exception → translate before C boundary;
- degraded execution → preserve degraded state;
- out-of-order temporal timestamp → reject without corrupting history.

---

## 16. Privacy / observability rules

Do not log by default:

- raw input images
- plate crops
- full plate text as routine diagnostics
- secrets/credentials

Prefer:

- stage timing
- provider status/failure code
- resource telemetry
- model identity/version/checksum metadata
- non-sensitive technical decision reasons
- emitted/suppressed counters without full plate logging

---

## 17. Consumer surfaces

Supported integration surfaces:

- native C++
- stable C ABI
- C# P/Invoke
- Python `ctypes`
- installed/exported CMake package

FAC Access consumes the C ABI through its .NET Device Service Infrastructure layer.

`RecognitionStreamSession` is currently a C++ Application-layer surface. Existing C/C#/Python consumers remain unchanged on C ABI v1.

---

## 18. Build / validation baseline

Primary baseline:

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
- macOS ARM64 self-hosted runner
- Linux x64 Docker validation/benchmark where applicable

Representative Linux development flow:

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure
```

Use repository presets/scripts instead of inventing a parallel build procedure.

---

## 19. Temporal test/benchmark coverage

Temporal behavior is already covered by deterministic regression tests and a real-model sequence benchmark.

Synthetic coverage includes:

- repeated agreement
- OCR jitter
- strong conflicts
- review-only sequences
- degraded accepted results
- duplicate suppression
- suppression expiry/reset
- vehicle transition
- session isolation
- out-of-order timestamps
- concurrent session calls
- bounded history

Real-model temporal smoke reports per-frame and temporal metrics separately so stream behavior cannot hide a per-frame regression.

Do not weaken golden/per-frame expectations merely to make temporal tests look better.

---

## 20. CI/release philosophy

Production readiness is broader than unit tests.

Validation/gates include areas such as:

- unit/integration tests
- temporal/session regression
- real-model execution
- golden regression
- temporal sequence smoke
- Linux x64 Debug/Release Docker validation
- Windows x64 native validation
- sanitizers/static analysis/fuzz paths
- memory stress
- performance regression
- ABI compatibility
- resource budgets
- C/C#/Python consumers
- CMake package consumption
- dependency/security scanning
- SBOM/release metadata
- production/readiness gates

Critical rule:

> A workflow existing is not proof it passed, and a skipped/queued workflow is not a pass.

Do not weaken warnings, analyzers, ABI checks, security checks or regression gates merely to get green CI.

Windows benchmark executables require ONNX Runtime runtime deployment beside the executable. Linux performance regression runs on the existing FAC-LPR macOS ARM64 self-hosted runner using `linux/amd64` Docker rather than waiting for a nonexistent dedicated Linux x64 runner.

---

## 21. Branching model

- `main` — stable/release branch
- `dev` — active development/release-candidate branch

Changes should normally land on `dev`, pass applicable validation, then be promoted through the release process.

Old feature branches are not evidence a feature is unfinished.

---

## 22. Current project state snapshot

Snapshot date: **2026-08-28**.

Verify live GitHub state before acting because this section naturally ages.

Completed production-hardening roadmap:

```text
#1–#78 complete / closed
```

Completed temporal/stream roadmap:

```text
#111 bounded temporal plate consensus
#112 stateful recognition stream session
#113 stable recognition emission and duplicate suppression
#114 stream API decision while preserving C ABI v1
#115 multi-frame temporal regression benchmark
```

At this snapshot:

```text
open issues: none
```

The codebase already contains the stream/temporal architecture. Do not reopen or recreate these features without evidence of an actual defect or new requirement.

---

## 23. How an AI agent should work here

For every task:

1. Determine whether the change belongs to Domain, Application, Infrastructure, public API, temporal/session behavior, model contract, tooling, packaging or release validation.
2. Read the relevant `PRODUCT.md` section and detailed docs.
3. Inspect existing implementation/tests before creating a new abstraction.
4. Preserve vendor isolation and inward dependency direction.
5. Preserve stateless `LprPipeline` semantics unless the product requirement explicitly changes them.
6. Reuse existing `TemporalPlateConsensus`, `StablePlateEventFilter` and `RecognitionStreamSession` for stream behavior instead of duplicating them.
7. Preserve model/C ABI contracts unless explicitly versioning a change.
8. Add/update deterministic regression coverage for behavior changes.
9. Keep all memory/resource/temporal state bounded.
10. Check real-model behavior when inference semantics may change.
11. Check ABI/consumer compatibility when public integration surfaces change.
12. Check exact CI evidence before declaring work complete.
13. Update canonical documentation if stable product/architecture/runtime semantics changed.

---

## 24. Things an AI must not assume

Do not assume:

- `ACCEPTED` means a vehicle is authorized;
- model shapes/preprocessing can be inferred from a file name;
- a model is compatible merely because it loads;
- multi-frame recognition is missing because `LprPipeline` is stateless;
- temporal fusion belongs inside `LprPipeline`;
- repeated `REVIEW` should become `ACCEPTED`;
- the first plate in a multi-vehicle frame can safely represent the stream;
- FAC Access business cooldown belongs in `StablePlateEventFilter`;
- C ABI v1 should be enlarged for stream state;
- a skipped/queued workflow counts as success;
- macOS success alone proves Windows packaging;
- unit tests alone prove ABI compatibility;
- internal C++ types may cross the C ABI;
- unbounded queues/workspaces/history are acceptable;
- logging plate text/images is harmless.

Verify instead of guessing.

---

## 25. Documentation ownership

Use documents for distinct purposes:

- `README.md` — human-facing product/build/integration introduction.
- `AGENTS.md` — continuity and operating rules for AI agents/new maintainers.
- `PRODUCT.md` — canonical product, architecture, model, temporal, ABI and release contracts.
- `docs/stream-recognition-api.md` — stream ownership/lifecycle/integration detail.
- `docs/temporal-stream-recognition.md` — temporal consensus/emission detail.
- GitHub issues — scoped work and acceptance criteria.
- PRs/CI — implementation and executed evidence.

If a change modifies a production model contract, public ABI, product responsibility boundary, temporal semantics or release rule, update `PRODUCT.md` and relevant tests/docs in the same coherent change.

Do not turn `PRODUCT.md` into a running issue diary. Stable intent belongs there; implementation history belongs in GitHub.
