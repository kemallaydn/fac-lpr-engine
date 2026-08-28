# FAC LPR Engine — Product and Architecture Specification

This document is the canonical description of **what FAC LPR Engine is, what it owns, how it is structured, which runtime contracts are production-critical, and what must be true before a release is promoted**.

Live code, tests and executed release evidence override stale prose. This document describes stable product/architecture intent; GitHub issues and PRs remain the implementation history.

---

## 1. Product definition

FAC LPR Engine is an independent, reusable, production-grade native **license plate recognition engine**.

Its core responsibility is to turn an input image/frame into a technically justified recognition result.

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
PlateRecognitionResult
```

Public technical outcomes:

```text
ACCEPTED
REVIEW
REJECTED
```

`ACCEPTED` means recognition evidence is technically strong enough under the configured recognition policy. It never means “grant access”.

### The engine owns

- plate detection
- geometric validation and rectification
- crop generation and enhancement
- OCR execution
- recognition evidence collection
- confidence calibration
- plate-layout analysis
- within-frame candidate fusion
- technical decision policy
- optional bounded cross-frame temporal consensus
- optional recognition-level stable emission / duplicate suppression
- stateful recognition stream sessions
- model activation/verification
- bounded native execution
- public C++ and stable C integration surfaces
- diagnostics and stage timing

### The engine does not own

- barrier/gate authorization
- FAC Access business rules
- registered-vehicle lookup
- user/customer permissions
- backend/database state
- UI state
- RTSP/camera lifecycle
- camera discovery/reconnect
- model training lifecycle
- payment/licensing business logic
- access-event cooldown/business deduplication
- audit/business-event persistence

That boundary is deliberate. FAC LPR Engine is a recognition component, not a complete access-control product.

---

## 2. Product principles

### 2.1 Correctness over optimistic output

The engine must prefer `REVIEW` or `REJECTED` over fabricated high-confidence output. Invalid, conflicting or incomplete evidence fails closed.

### 2.2 Deterministic runtime contracts

Tensor names, dimensions, preprocessing, charset, blank index, keypoint interpretation and decoder semantics are production contracts. They are never guessed at runtime.

### 2.3 Bounded native execution

External input must never cause uncontrolled allocation, queue growth, workspace growth or temporal-history growth. Resource limits are explicit and validated.

### 2.4 Stable integration boundary

Consumers should not need to know about ONNX Runtime, OpenCV or internal C++ ownership details. The public C ABI is versioned and intentionally flat.

### 2.5 Vendor isolation

ONNX Runtime and OpenCV are infrastructure details. Vendor-specific types must not leak into Domain, Application or the public C ABI.

### 2.6 Stateless behavior remains canonical

`LprPipeline::recognize()` remains the canonical per-frame implementation. Stateful stream recognition is additive and must not silently change existing single-frame semantics.

### 2.7 Stateful stream logic remains recognition-only

Temporal consensus and duplicate suppression may stabilize recognition across adjacent frames, but they must never absorb authorization, barrier or customer business rules.

### 2.8 Truthful release evidence

A source file existing is not proof a platform works. A skipped/queued workflow is not equivalent to a passed job. Release promotion is based on executed evidence for the exact candidate.

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

Contains vendor-independent recognition concepts/value types only. It must remain independent from OpenCV, ONNX Runtime, filesystem/runtime adapters and public wire-format concerns.

### Application

Owns recognition orchestration and vendor-neutral policies.

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

The Application layer now has two deliberate modes:

```text
Stateless:
frame -> LprPipeline -> per-frame result

Stateful optional stream mode:
frame -> LprPipeline
      -> TemporalPlateConsensus
      -> StablePlateEventFilter
      -> RecognitionStreamSession result
```

### Infrastructure

Owns concrete runtime implementations:

- ONNX Runtime sessions/providers
- OpenCV image operations
- detector/OCR adapters
- model loading and checksum verification
- native image/workspace implementation
- concrete concurrency/platform integration

### Public API / Composition Root

Owns stable integration surfaces and concrete assembly of the production pipeline.

---

## 4. Production recognition pipeline

Current production per-frame composition is conceptually:

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

`LprPipelineResult` preserves more than a final string. It carries recognition results, stage timings, degraded state and provider failures.

Do not create a hidden second inference implementation for stream recognition. Stateful behavior must consume the ordinary per-frame pipeline output.

---

## 5. Within-frame fusion vs cross-frame consensus

These concepts are intentionally separate.

### Within-frame fusion

`WeightedMultiCropCandidateFusion`, `RecognitionEnsemble` and related policies combine evidence produced from **one frame**.

### Cross-frame temporal consensus

`TemporalPlateConsensus` combines completed recognition results across **multiple ordered frames in one logical stream session**.

Temporal consensus must not:

- reimplement crop/provider fusion;
- modify detector/OCR contracts;
- change `LprPipeline::recognize()` semantics;
- promote weak/review-only evidence merely because it repeats;
- grow history without explicit bounds.

---

## 6. Stateful stream recognition

`RecognitionStreamSession` is the application-layer owner of stream-local recognition state.

Conceptual flow:

```text
already-decoded frame
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

Session rules:

- one session owns one bounded temporal history;
- one session owns one bounded stable-emission suppression state;
- independent sessions cannot contaminate each other;
- session methods serialize access to session-owned mutable state;
- reset clears temporal/emission/timestamp state;
- close is idempotent and rejects future work according to the defined contract;
- out-of-order timestamps are rejected without corrupting state;
- duplicate timestamps are allowed according to the tested contract;
- no raw images are retained in temporal history;
- no global mutable per-camera state exists.

### Ambiguous multi-plate frames

A frame containing multiple recognitions is returned normally in `frame_result`, but it is not fed into one single temporal plate identity. Without an explicit tracker identity, merging multiple vehicles would be unsafe.

---

## 7. Temporal consensus semantics

Temporal consensus is bounded and deterministic for the same ordered input/configuration.

The policy supports:

- finite history/window;
- stale-entry expiry;
- configurable minimum support;
- minimum accepted confidence;
- confidence-weighted support;
- recency weighting/decay;
- deterministic conflict/tie handling;
- reset;
- bounded history size.

Only suitable technical recognition results participate in stable voting. `REVIEW`/weak/rejected observations do not become `ACCEPTED` simply because they repeat.

If a stable result is produced from degraded accepted observations, the stable result preserves the degraded state rather than laundering it into a normal result.

---

## 8. Stable recognition emission and duplicate suppression

`StablePlateEventFilter` operates at **recognition emission level**, not business-event level.

It may:

- emit a stable recognition after temporal support is satisfied;
- suppress repeated stable emissions of the same plate within a bounded cooldown;
- allow a materially different plate to emit without waiting for the previous plate's cooldown;
- preserve deterministic reset/expiry behavior;
- expose non-sensitive emitted/suppressed diagnostics.

It must not:

- decide if a vehicle is authorized;
- decide whether a barrier should open;
- implement “same vehicle may enter again after N seconds” business policy;
- hide meaningful recognition changes;
- retain unbounded plate state.

FAC Access remains responsible for business-level access-event deduplication/cooldown.

---

## 9. Detector contract

Active detector model:

```text
best.onnx
```

Authoritative tensor contract:

```text
input  images   float32 [1,3,960,960]
output output0  float32 [1,17,18900]
```

Assumptions:

- RGB CHW
- scale `1/255`
- letterbox pad value `114`
- features-first output
- 4 bounding-box values
- one plate-class score
- no separate objectness score
- 4 keypoints, each `(x, y, confidence)`

Geometry must normalize/reorder corners itself instead of trusting undocumented semantic keypoint ordering.

---

## 10. OCR contract

Active OCR model:

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

Native decoder contract:

```text
charset = 0123456789ABCDEFGHIJKLMNOPRSTUVYZ
blank_index = 33
class_count = 34
timesteps = 24
```

Preprocessing:

```text
resize: 160 x 40
color order: BGR
float32
(img - 127.5) / 128
HWC -> CHW
batch -> [1,3,40,160]
```

Deprecated assumptions such as `[1,3,24,94]` input or `[1,34,18]` output are forbidden for the active model.

---

## 11. Model lifecycle and integrity

Runtime model activation is all-or-nothing.

A model manifest identifies and verifies artifacts using metadata such as:

- logical model identity/type
- version
- path
- exact file size
- SHA-256

Activation rejects unsafe/ambiguous input including path traversal, canonical-path escape, duplicate identity, missing artifact, size mismatch, checksum mismatch or incomplete runtime contract.

A failed replacement must not partially replace the previously valid active model set.

---

## 12. Recognition evidence and decision semantics

A plate string alone is not the product contract.

Evidence may include:

- detector confidence
- geometry quality
- crop quality
- OCR/provider evidence
- calibrated candidate confidence
- plate-layout evidence
- alternative candidates
- degraded provider state
- explicit technical decision reasons
- stage timing

The decision policy must expose why a result became `ACCEPTED`, `REVIEW` or `REJECTED`.

Temporal consensus consumes this completed technical result; it does not replace the decision policy.

---

## 13. Concurrency and resource model

### Reusable inference workspace

`NativeImageWorkspace` is bounded, RAII-managed and move-only. It supports reuse across inference operations and tracks capacity/growth telemetry.

### Worker pool

The bounded worker pool supports configurable worker count, bounded queue capacity, explicit backpressure, drain/discard shutdown, reusable workspace per worker and task/error telemetry.

Stateful stream recognition does not introduce a duplicate worker-pool architecture.

### Temporal state

Temporal history and suppression state are explicitly bounded by count/time configuration. Session ownership prevents accidental global growth.

### Resource arithmetic

For strided images, required extent is equivalent to:

```text
(height - 1) * stride + packed_row_bytes
```

Overflow/impossible dimensions fail before allocation or memory access.

---

## 14. Public C ABI v1

Stable header:

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
- explicit export/calling convention
- no C++ exception crosses ABI
- struct size/version explicit
- caller-owned flat result buffer
- two-call required-size pattern
- buffer-relative offsets/counts
- explicit text offset + length
- alignment/range/overflow validation

### Stream API decision

The new stream/session capability **does not modify C ABI v1**.

No stream fields were added to existing v1 config/result records and no existing symbol semantics were reinterpreted.

A future C ABI stream extension is allowed only when a real downstream need exists. It must be additive, separately versioned and use its own opaque stream handle/new symbols instead of resizing or reinterpreting v1 structures.

---

## 15. Consumer integration

Validated/covered consumer paths include:

- native C++
- C ABI
- C# P/Invoke
- Python `ctypes`
- installed/exported CMake package consumption

`RecognitionStreamSession` is currently a C++ application-layer integration surface. Existing C/C#/Python consumers remain on stable C ABI v1 and continue unchanged.

FAC Access consumes the engine through its public C ABI via the .NET Device Service Infrastructure layer.

---

## 16. Offline CLI and benchmark tools

Optional CLI build target:

```text
FAC_LPR_BUILD_LPR_CLI=ON
```

The CLI executes the real application pipeline and is not a second recognition implementation.

Optional benchmark build target:

```text
FAC_LPR_BUILD_BENCHMARK=ON
```

produces the ordinary performance benchmark and temporal sequence benchmark.

The temporal benchmark feeds ordered frames through the normal production pipeline and `RecognitionStreamSession`, then reports metrics such as:

- frames
- expected plate frames
- per-frame correct
- stable emitted/correct
- false stable
- duplicate suppressed
- first correct stable frame
- maximum temporal history observed

---

## 17. Temporal regression strategy

Temporal behavior is validated at two complementary levels.

### Synthetic deterministic regression

Covers scenarios including:

- repeated agreement
- OCR jitter
- conflicting/alternating candidates
- review-only sequences
- degraded accepted results
- duplicate suppression
- expiry/reset
- vehicle transition
- bounded history
- concurrent session calls

### Real-model temporal smoke

A versioned frame manifest feeds real fixture images through the production pipeline and session layer.

The real-model gate keeps per-frame accuracy independently visible and asserts no false-stable output plus bounded state behavior.

If production decision evidence remains `REVIEW`, the temporal layer is expected to stay fail-closed rather than forcing a stable `ACCEPTED` result.

---

## 18. Error handling and failure policy

The engine follows fail-closed behavior at external boundaries.

Examples:

- malformed images are rejected;
- invalid model contracts are rejected;
- checksum mismatch blocks activation;
- provider failures are surfaced;
- invalid result values do not cross the C ABI;
- C++ exceptions are translated before crossing C boundaries;
- unsafe resource requests fail before allocation;
- out-of-order temporal timestamps do not silently mutate history;
- degraded execution is preserved as degraded.

---

## 19. Privacy and logging

By default, the engine should not log:

- raw input images
- plate crops
- full plate text as routine diagnostic payload
- model/consumer secrets

Technical observability should favor stage timing, provider status, resource telemetry, model identity/checksum and non-sensitive decision reasons.

Stable-emission metrics should expose counts without requiring full plate logging.

---

## 20. Build and platform baseline

Primary baseline:

- C++20
- C11-compatible public ABI
- CMake 3.25+
- Ninja where applicable
- Windows x64 / MSVC
- Linux x64 / GCC and Clang
- macOS ARM64 self-hosted runner
- Linux x64 validation/benchmark through `linux/amd64` Docker on that runner where applicable
- ONNX Runtime
- OpenCV
- GoogleTest / CTest
- spdlog

Windows standalone benchmark executables must deploy the required ONNX Runtime DLL beside the executable; this is part of runtime validation, not an optional convenience.

---

## 21. Validation strategy

Production readiness is broader than unit tests.

Repository validation includes areas such as:

- unit tests
- integration tests
- temporal consensus/session regression
- real-model execution
- golden regression
- temporal real-sequence smoke
- Linux x64 Debug/Release Docker validation
- Windows x64 native validation
- macOS ARM64 host validation
- sanitizer/static-analysis/fuzz paths
- memory stress
- performance regression
- ABI compatibility
- resource budgets
- C/C#/Python consumers
- CMake package consumption
- dependency/security scanning
- SBOM/release metadata
- release-readiness / production-readiness

A workflow existing does not prove it passed. A skipped, cancelled or permanently queued job is not a pass.

Performance regression runs on the existing FAC-LPR self-hosted macOS ARM64 runner and executes the benchmark in a `linux/amd64` Docker environment, avoiding dependency on a nonexistent dedicated `[self-hosted, linux, x64]` runner.

---

## 22. Current project state

Snapshot: **2026-08-28**.

Verify live GitHub state before acting because snapshots age.

Completed foundation roadmap:

```text
#1–#78: complete / closed
```

Completed temporal/stream roadmap:

```text
#111  bounded temporal plate consensus
#112  stateful recognition stream session
#113  stable recognition emission / duplicate suppression
#114  optional stream API decision while preserving C ABI v1
#115  multi-frame temporal regression benchmark
```

Current live issue state at the time of this update:

```text
open issues: none
```

The temporal/stream implementation has Windows and Linux full validation evidence for the relevant code path. The performance workflow runner mismatch discovered during final validation was corrected by moving the Linux x64 benchmark execution onto the existing FAC-LPR macOS ARM64 runner through `linux/amd64` Docker.

This snapshot does **not** mean every future commit is automatically releasable. Every release candidate still requires exact-commit validation according to release policy.

---

## 23. Roadmap and future evolution

The initial production-hardening and first temporal/stream recognition roadmap are complete.

Future work should be driven by measured product needs, for example:

- improved confidence calibration from larger evaluation sets
- stronger vehicle/plate tracking identity before multi-object temporal association
- additional detector/OCR model generations
- country/plate-format expansion behind explicit contracts
- hardware-provider tuning and acceleration
- throughput/latency optimization with unchanged decision semantics
- improved operational diagnostics
- additive C ABI stream extension only if downstream consumers actually require it

Do not reopen completed foundation work without evidence of a defect or a new requirement.

---

## 24. Non-negotiable guardrails

Do not:

- treat `ACCEPTED` as access authorization;
- guess model tensor/charset/preprocessing semantics;
- bypass checksum/model-contract validation;
- merge unrelated vehicles into one temporal identity without tracking evidence;
- use temporal repetition to promote weak/review-only evidence to accepted;
- duplicate `LprPipeline`, candidate fusion or worker-pool logic inside stream recognition;
- put RTSP/camera lifecycle into the engine;
- put FAC Access business cooldown/authorization into the engine;
- change public C ABI v1 layouts or semantics for stream state;
- allow queues, workspaces, temporal history or suppression state to become unbounded;
- claim a queued/skipped workflow is successful;
- weaken regression/resource/ABI gates merely to get green CI.

---

## 25. Documentation ownership

Use documents for distinct purposes:

- `README.md` — human-facing product/build/integration overview.
- `AGENTS.md` — continuity and operating rules for AI agents/new maintainers.
- `PRODUCT.md` — canonical product, architecture, runtime, temporal and ABI contracts.
- `docs/stream-recognition-api.md` — stream session lifecycle/integration details.
- `docs/temporal-stream-recognition.md` — temporal consensus and stable-emission behavior.
- GitHub issues — scoped work and acceptance criteria.
- PRs/CI — implementation and executed evidence.

If a change modifies a production model contract, public ABI, product responsibility boundary, temporal semantics or release rule, update `PRODUCT.md` and relevant tests/docs in the same coherent change.
