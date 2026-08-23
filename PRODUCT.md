# FAC LPR Engine — Product and Architecture Specification

This document is the canonical description of **what FAC LPR Engine is, what it owns, how it is structured, what runtime contracts are considered production-critical, and what must be true before a release is promoted**.

Live code, tests and release evidence always override stale prose. This document should describe the product, not act as a running issue diary.

---

## 1. Product definition

FAC LPR Engine is an independent, reusable, production-grade native **license plate recognition engine**.

Its responsibility is to turn an input image/frame into a technically justified recognition result:

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
PlateRecognitionResult
```

The public technical outcomes are:

```text
ACCEPTED
REVIEW
REJECTED
```

`ACCEPTED` means the recognition evidence is technically strong enough under the configured policy. It never means “grant access”.

### The engine owns

- plate detection
- geometric validation and rectification
- crop generation and enhancement
- OCR execution
- recognition evidence collection
- confidence calibration
- plate-layout analysis
- candidate fusion
- technical decision policy
- model activation/verification
- bounded native execution
- public C++/C integration surfaces
- diagnostics and stage timing

### The engine does not own

- barrier/gate authorization
- FAC Access business rules
- user/customer permissions
- backend/database state
- UI state
- RTSP/camera lifecycle
- camera discovery
- model training lifecycle
- payment/licensing business logic
- audit/business-event persistence

That boundary is deliberate. FAC LPR Engine is a recognition component, not a complete access-control product.

---

## 2. Product principles

### 2.1 Correctness over optimistic output

The engine must prefer `REVIEW` or `REJECTED` over a fabricated high-confidence plate. Invalid or incomplete evidence must fail closed.

### 2.2 Deterministic runtime contracts

Model tensor names, dimensions, preprocessing, charset, blank index, keypoint interpretation and decoder semantics are part of the production contract. They are never guessed at runtime.

### 2.3 Bounded native execution

External input must never be allowed to cause uncontrolled allocation, queue growth, workspace growth or integer overflow. Resource limits are explicit and validated.

### 2.4 Stable integration boundary

Consumers should not need to know about ONNX Runtime, OpenCV, internal C++ classes or ownership details. The public C ABI is versioned and intentionally flat.

### 2.5 Vendor isolation

ONNX Runtime and OpenCV are infrastructure details. Vendor-specific types must not leak into Domain, Application or the public C ABI.

### 2.6 Truthful release evidence

A source file existing is not proof that a platform works. A skipped job is not equivalent to a passed job. Production promotion is based on executed evidence for the exact candidate.

---

## 3. Architecture

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

Contains recognition concepts and value types only. It must remain independent from OpenCV, ONNX Runtime, filesystem/runtime adapters and public wire-format concerns.

### Application

Owns orchestration and vendor-neutral ports/policies, including the recognition pipeline and contracts such as:

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

Owns concrete runtime implementations:

- ONNX Runtime sessions/providers
- OpenCV image operations
- detector/OCR adapters
- model loading and checksum verification
- native image/workspace implementation
- concurrency primitives and concrete platform integrations

### Public API / Composition Root

Owns the stable integration boundary and concrete assembly of the production pipeline.

---

## 4. Production recognition pipeline

The current production composition is conceptually:

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

`LprPipelineResult` preserves more than the final string. It carries the technical evidence needed to understand the decision, including recognition results, stage timings, degraded state and provider failures.

The pipeline must remain vendor-neutral at the application boundary even when concrete infrastructure uses OpenCV and ONNX Runtime.

---

## 5. Detector contract

The active detector model is:

```text
best.onnx
```

Authoritative tensor contract:

```text
input  images   float32 [1,3,960,960]
output output0  float32 [1,17,18900]
```

Preprocessing/output assumptions:

- RGB CHW
- scale `1/255`
- letterbox pad value `114`
- features-first output
- 4 bounding-box values
- one plate-class score
- no separate objectness score
- 4 keypoints, each `(x, y, confidence)`

The geometry layer must normalize/reorder corners itself. It must not depend on an undocumented semantic keypoint order from the model.

---

## 6. OCR contract

The active OCR model is:

```text
lprnet_turkey.onnx
```

Current model family: **V2 Mixed Epoch 7**.

Tensor contract:

```text
input  input   float32 [1,3,40,160]
output output  float32 [1,34,24]
layout: BCT
```

Training character order:

```text
0 1 2 3 4 5 6 7 8 9 A B C D E F G H I J K L M N O P R S T U V Y Z -
```

The final `-` is the CTC blank and is not a real output character.

Native decoder contract:

```text
charset = 0123456789ABCDEFGHIJKLMNOPRSTUVYZ
blank_index = 33
class_count = 34
timesteps = 24
```

Preprocessing:

```text
resize: 160 x 40, linear interpolation
color order: BGR
float32
(img - 127.5) / 128
HWC -> CHW
batch dimension -> [1,3,40,160]
```

Equivalent native configuration:

```text
input_scale = 1.0
mean = [127.5,127.5,127.5]
std  = [128,128,128]
```

Deprecated model assumptions such as `[1,3,24,94]` input or `[1,34,18]` output are forbidden for the active production model.

These contracts are regression locked. Any future model replacement must update the explicit contract and its tests instead of relying on compatibility by accident.

---

## 7. Model lifecycle and integrity

Runtime model activation is all-or-nothing.

A model manifest identifies and verifies model artifacts using metadata including:

- logical model identity/type
- version
- path
- exact file size
- SHA-256

Activation rejects unsafe or ambiguous input, including:

- absolute paths where not allowed
- `..` traversal
- canonical-path escape through symlinks/root manipulation
- duplicate model identity
- file-size mismatch
- checksum mismatch
- invalid/incomplete contract

A partially valid model set must never become active. The previously valid runtime state must remain intact when a replacement fails validation.

---

## 8. Recognition evidence and decision semantics

A plate string alone is not the product contract. The engine evaluates multiple evidence sources.

Evidence may include:

- detector confidence
- geometry quality
- crop quality
- OCR provider evidence
- calibrated candidate confidence
- plate-layout evidence
- alternative candidates
- degraded provider state
- explicit technical decision reasons

The decision policy must expose why a result became `ACCEPTED`, `REVIEW` or `REJECTED`. Typical reasons include weak detector evidence, weak geometry, weak crop quality, no valid candidate, low candidate confidence, conflicting strong candidates or degraded/fatal provider state.

This information is intended for technical observability and downstream review flows, not for silently converting uncertainty into a positive authorization decision.

---

## 9. Concurrency and resource model

### Reusable inference workspace

`NativeImageWorkspace` is bounded, RAII-managed and move-only. It supports reuse across inference operations and tracks capacity/growth telemetry.

Relevant telemetry includes:

```text
tensor_capacity
scratch_capacity
tensor_growth_count
scratch_growth_count
```

The goal is predictable steady-state behavior after warm-up rather than repeated large allocation churn.

### Worker pool

The bounded worker pool supports:

- configurable worker count
- bounded queue capacity
- reject-newest or blocking backpressure
- drain/discard-pending shutdown semantics
- one reusable workspace per worker
- exception isolation between tasks
- submitted/completed/failed/dropped/pending/active telemetry

### Resource arithmetic

External dimensions and allocation calculations are checked before allocation.

For strided images, the required extent is calculated as:

```text
(height - 1) * stride + packed_row_bytes
```

Integer overflow, impossible dimensions and budget violations must fail before allocation or memory access.

---

## 10. Public C ABI v1

The stable C header is:

```text
include/fac_lpr/fac_lpr_engine.h
```

Primary lifecycle symbols:

```c
fac_lpr_engine_create_v1
fac_lpr_engine_recognize_v1
fac_lpr_engine_destroy_v1
fac_lpr_get_last_error_v1
```

Design rules:

- opaque engine handle
- explicit export/calling-convention macros
- no C++ exception crosses the ABI
- destroy is null/repeated-safe
- pointer-to-handle destruction clears the caller slot
- struct size/version contract is explicit
- independent C11 compilation/layout validation protects the wire format

### Result ownership

Recognition output is serialized into one **caller-owned flat byte buffer**.

No engine-owned nested string/pointer graph crosses the ABI.

Wire-layout families include:

```text
fac_lpr_result_v1
fac_lpr_plate_result_v1[]
fac_lpr_evidence_v1[]
fac_lpr_candidate_v1[]
decision reason values
text slices
```

Nested data is represented with buffer-relative offsets/counts. Text uses explicit offset + length and is not assumed to be NUL-terminated.

The API supports a two-call required-size pattern and returns `FAC_LPR_STATUS_BUFFER_TOO_SMALL` when appropriate.

The ABI layer validates alignment, finite numeric values, probability ranges and wire-size overflow before returning data to the consumer.

---

## 11. Consumer integration

The native engine is intended to support multiple integration surfaces without coupling consumer code to internal C++ implementation.

Validated/covered consumer paths include:

- native C++
- C ABI
- C# P/Invoke
- Python `ctypes`
- installed/exported CMake package consumption

Consumer compatibility is a release concern. Internal refactoring is allowed only when the public contract remains compatible under the declared SemVer/ABI policy.

---

## 12. Offline CLI

Optional build target:

```text
FAC_LPR_BUILD_LPR_CLI=ON
```

produces:

```text
fac-lpr-cli
```

It executes the real application pipeline against JPG/PNG input and supports:

```text
--json
--debug-evidence
--model-dir <path>
--config <path>
--log-level trace|debug|info|warn|error
```

JSON mode is a machine-readable contract. Third-party diagnostic logging must not contaminate stdout. Human diagnostics belong on stderr or behind the appropriate logging path.

The CLI exists for real-pipeline validation, operations/debugging and offline acceptance. It is not a second implementation of recognition logic.

---

## 13. Error handling and failure policy

The engine follows fail-closed behavior at external boundaries.

Examples:

- malformed images are rejected
- invalid model contracts are rejected
- checksum mismatch blocks activation
- provider failures are surfaced
- invalid result values do not cross the C ABI
- C++ exceptions are translated before crossing C boundaries
- unsafe resource requests fail before allocation
- cancellation/deadline semantics are explicit

A degraded pipeline must communicate that state rather than silently presenting a normal successful execution.

---

## 14. Privacy and logging

By default, the engine should not log:

- raw input images
- plate crops
- full plate text as routine diagnostic payload
- model secrets/credentials
- consumer secrets

Technical observability should favor:

- stage timing
- provider status/failure code
- resource telemetry
- model identity/version/checksum metadata
- non-sensitive decision reasons

Any future persistence of sensitive recognition data belongs to the consuming product's explicit privacy/audit policy, not hidden inside the engine.

---

## 15. Build and platform baseline

Primary baseline:

- C++20
- C11-compatible public ABI
- CMake 3.25+
- Ninja where applicable
- Windows x64 / MSVC
- Linux x64 / GCC and Clang
- macOS ARM64 self-hosted validation
- ONNX Runtime
- OpenCV
- GoogleTest / CTest
- spdlog

Normal validation treats warnings as errors where configured. Platform-specific undefined behavior or compiler diagnostics are considered product defects, not cosmetic differences.

---

## 16. Validation strategy

Production readiness is broader than “the unit tests passed”.

The repository contains validation/gates for areas including:

- unit tests
- integration tests
- real-model execution
- golden regression
- Linux x64 Debug/Release Docker validation
- macOS ARM64 validation
- sanitizer paths
- static analysis
- fuzzing
- memory stress
- performance regression
- ABI compatibility
- resource budgets
- C/C#/Python consumers
- CMake package consumption
- dependency/security scanning
- SBOM/release metadata
- release-readiness
- production-readiness

A workflow being present does not prove it passed. A workflow being skipped does not prove it passed either. The applicable release policy decides which evidence is mandatory for the exact candidate.

---

## 17. Production release policy

A production release must be promoted from an exact, validated candidate.

The release process is fail-closed:

1. candidate code is frozen by commit SHA;
2. mandatory validation executes against that candidate;
3. required evidence is collected;
4. model artifacts/contracts/checksums are verified;
5. ABI/package/security/readiness requirements are satisfied;
6. candidate is promoted to the stable branch;
7. the released commit is tagged/versioned according to release policy;
8. release artifacts/metadata correspond to that exact commit.

Missing, failed, cancelled or improperly skipped mandatory evidence must block promotion.

Closing implementation roadmap issues does not automatically make every future candidate releasable.

---

## 18. Current release-candidate state

Checkpoint: **2026-08-23**

Development state:

```text
roadmap issues #1–#78: complete / closed
open roadmap issues: none
active release-candidate branch: dev
promotion PR: #79 (dev -> main)
PR state: ready for review, mergeable
production v1 release: not yet tagged/released
```

The current `dev` release candidate has successful evidence for the final gates checked during this promotion cycle, including:

```text
Linux x64 full validation via Docker
production-readiness
release-readiness
abi-compatibility
cmake-package-smoke
resource-budget
evaluation-tool
release-metadata
```

Some workflows may be intentionally conditional/skipped for a specific event. Such skips are not described as passes; the production/release readiness policy remains the authority on mandatory evidence.

The next release action is promotion of the validated `dev` candidate to `main`, followed by exact-commit release/tag validation and publication under the repository release policy.

---

## 19. Roadmap and future evolution

The initial production-hardening roadmap is complete. Future development should be driven by measurable product needs rather than reopening already-solved foundation work.

Likely future evolution areas include:

- additional detector/OCR model generations
- improved confidence calibration from larger evaluation sets
- country/plate-format expansion behind explicit contracts
- hardware-provider tuning and acceleration
- throughput/latency optimization with unchanged decision semantics
- improved operational diagnostics
- stronger packaged-consumer automation

Any future model or provider upgrade must preserve the architecture boundary and pass the same class of contract, regression, resource, ABI and release checks.

---

## 20. Non-negotiable guardrails

Do not:

- treat `ACCEPTED` as access authorization
- guess model tensor/charset/preprocessing semantics
- bypass checksum/model-contract verification
- leak OpenCV/ONNX types into Domain/Application/public ABI
- expose engine-owned nested pointers through the C ABI
- allow unbounded queue/workspace/result growth
- weaken warnings/tests/security/release gates merely to get a green build
- call a skipped check “passed”
- call a candidate “production released” before exact-commit promotion/tag evidence exists

The product is considered trustworthy only when its recognition behavior, resource behavior and release evidence remain explicit and reproducible.
