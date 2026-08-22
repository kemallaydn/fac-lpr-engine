# FAC LPR Engine — Product, Architecture and AI Handoff

> **Purpose of this document**
>
> This file is the canonical handoff document for a developer or another AI taking over FAC LPR Engine development. A new agent should be able to read this document, inspect the referenced files/issues, and continue implementation without relying on previous chat history.
>
> **Code, tests and current GitHub issue state are authoritative.** If this document and the current `dev` branch differ, inspect the repository and GitHub issue state first, then update this document in the same change.

---

## 1. Product definition

FAC LPR Engine is an **independent, reusable, production-grade native license plate recognition engine**.

Its responsibility is deliberately narrow:

```text
image/frame
   ↓
plate detection
   ↓
geometry validation/alignment
   ↓
crop generation/enhancement
   ↓
OCR recognition
   ↓
candidate/evidence fusion
   ↓
recognition decision
   ↓
PlateRecognitionResult
```

The engine recognizes plates and returns technical evidence/results. It is intended to be consumed by FAC Access or unrelated applications through a stable native boundary.

### The engine MUST NOT

- open or close barriers;
- decide physical access authorization;
- contain FAC Access business rules;
- talk directly to the Spring/backend application;
- own RTSP/camera lifecycle;
- own the application's database;
- own a UI;
- silently turn OCR confidence into an access-control decision;
- embed model binaries into the source repository.

The engine decision vocabulary is technical only:

```text
ACCEPTED
REVIEW
REJECTED
```

`ACCEPTED` means recognition evidence is technically strong enough according to engine policy. It does **not** mean "grant access".

---

## 2. Repository and development workflow

Repository:

```text
kemallaydn/fac-lpr-engine
```

Branches:

- `main`: stable/release base.
- `dev`: active development branch.

Current long-running development PR:

- Draft PR **#79** — `FAC LPR Engine production development`
- base: `main`
- head: `dev`

Development should continue on `dev` and PR #79 should remain the integration surface until production-readiness work is complete.

### Mandatory issue workflow

Issues are completed **sequentially by issue number**.

For every issue:

1. read the current GitHub acceptance criteria;
2. inspect the existing implementation;
3. implement only the missing pieces;
4. add or adjust tests;
5. perform the strongest truthful validation available;
6. add a **Turkish top-level issue comment** explaining what was done and how it was verified;
7. close with `state_reason=completed`;
8. continue to the next issue.

Typical closing comment:

```text
Tamamlandı. <uygulanan değişiklikler>.
Doğrulama: <gerçek test/build/inceleme kanıtı>.
```

Never fake test success. Never close an issue merely because similarly named code exists.

---

## 3. Current authoritative checkpoint

GitHub issue state was re-audited on **2026-08-22**.

### Confirmed completed issues

**#1 through #21 are closed with `state_reason=completed`.**

The last confirmed closed issue is:

- **#21 — LPRNet greedy CTC decoder oluştur**

Its closing record confirms configurable charset/blank index handling, argmax decoding, repeat collapse, blank removal, confidence aggregation and unit-test coverage.

### Current next issue

The first open issue in the required sequential roadmap is:

- **#22 — Türk plaka grammar ve normalizer oluştur**

Therefore the project must resume from **#22**, not #6.

### Important stale-handoff correction

An earlier version of this document incorrectly stated that only #1–#5 were closed and that work should resume at #6. That statement was stale. Current GitHub issue state overrides it.

Code already exists for several issues after #22 as well. This does **not** authorize closing them out of order. Each issue still requires an acceptance-criteria audit when its turn arrives.

---

## 4. CI / GitHub Actions budget constraint

This remains a **hard operational constraint**.

The GitHub account reached roughly 90% of included Actions minutes and additional hosted-runner spend is not desired.

The repository therefore follows a **zero-spend CI policy** for routine development.

See:

```text
docs/ci-budget.md
```

Current policy:

- GitHub-hosted workflows must not casually run automatically on every push/PR;
- `foundation-build` is manual / `workflow_dispatch`;
- `dependency-restore` is manual / `workflow_dispatch`;
- do not restore expensive automatic matrices without explicit approval;
- do not treat a red workflow with zero executed steps as a code failure without checking runner allocation;
- release readiness still eventually requires full Windows/Linux validation.

Preferred strategy while hosted budget is constrained:

```text
routine development
    → local/free/self-hosted validation

manual quick CI when justified
    → Linux GCC Release

release candidate
    → Windows MSVC + Linux GCC/Clang
    → dependency-backed tests
    → sanitizers/static analysis
    → packaging/ABI/security gates
```

---

## 5. Technology baseline

- C++20
- CMake 3.25+
- CMake Presets
- Windows x64 / MSVC
- Linux x64 / GCC
- Linux x64 / Clang
- ONNX Runtime
- OpenCV, intentionally minimized
- GoogleTest
- spdlog

Dependency strategy:

- vcpkg manifest mode for normal C/C++ dependencies such as OpenCV/GTest/spdlog;
- pinned vcpkg baseline;
- ONNX Runtime from official Microsoft prebuilt artifacts with checksum pinning rather than expensive source builds;
- runtime ONNX model binaries are never committed to Git.

---

## 6. Architectural dependency rule

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

Equivalent mental model:

```text
API -> Application -> Domain
Infrastructure -> Application + Domain
```

### Domain

Domain contains stable value types only.

Rules:

- standard C++ only;
- no OpenCV headers;
- no ONNX Runtime headers;
- no PaddleOCR dependency;
- no filesystem/network/process dependency;
- no UI/backend dependency;
- value semantics and RAII.

Representative types:

- `Point2f`
- `BoundingBox`
- `PlateQuadrilateral`
- `Detection`
- `PlateCandidate`
- `RecognitionEvidence`
- `PlateRecognitionResult`
- `RecognitionStatus`

### Application

Application owns use cases and vendor-neutral contracts.

Existing provider interfaces include:

- `IPlateDetector`
- `IPlateAligner`
- `ICropGenerator`
- `IPlateRecognizer`
- `IPlateLayoutAnalyzer`
- `ICandidateFusion`
- `IConfidenceCalibrator`
- `IDecisionPolicy`

Application also owns portable contracts such as `ImageView`, `OperationContext`, config models and typed engine errors.

### Infrastructure

Infrastructure contains concrete integrations and optimized implementations:

- ONNX Runtime wrappers;
- YOLO/LPRNet adapters;
- native image kernels;
- OpenCV-backed complex CV operations;
- spdlog adapter;
- future PaddleOCR external adapter;
- future execution-provider implementations.

Vendor-specific implementation types must not leak into Domain/Application/public ABI.

---

## 7. Image-processing architecture

**OpenCV must not be the fundamental image abstraction and must not dominate the hot path.**

Use custom/native C++ for simple deterministic operations:

- caller-owned `ImageView` representation;
- image validation;
- checked stride/size arithmetic;
- zero-copy crop views;
- packed crop copy when needed;
- padding/basic pixel operations;
- YOLO bilinear sampling;
- resize/letterbox mapping;
- BGR/RGB/Gray handling;
- normalization;
- HWC → CHW tensor writing;
- reusable tensor/scratch workspaces;
- simple crop quality metrics.

Keep OpenCV for high-value complex algorithms:

- homography;
- `warpPerspective`;
- CLAHE;
- adaptive thresholding;
- connected components;
- complex morphology/layout helpers.

### Fused YOLO preprocessing target

```text
source ImageView
      ↓
letterbox coordinate mapping
      ↓
bilinear sample
      ↓
BGR/RGB/Gray → RGB values
      ↓
normalization
      ↓
direct write into reusable CHW float tensor
```

Do not regress to a chain of temporary `cv::Mat` allocations without measured accuracy/performance justification.

Scalar portable C++ remains the correctness reference. SIMD such as AVX2/NEON should be introduced only after profiling proves value.

---

## 8. Ownership and memory rules

These are production requirements:

- no raw ownership;
- no scattered `new/delete` lifecycle;
- RAII by default;
- `std::unique_ptr` for exclusive ownership;
- caller-owned image memory stays caller-owned;
- `ImageView` is non-owning;
- `ImageBuffer` owns its storage;
- workspaces own reusable scratch/tensor capacity;
- no unbounded queues, caches or retained frame history;
- no unbounded image/tile/crop/tensor allocation;
- checked arithmetic for externally controlled dimensions and strides;
- no engine-owned dangling `char*` crossing C ABI.

For strided views, required byte extent is:

```text
(height - 1) * stride + packed_row_bytes
```

not blindly `height * stride`.

---

## 9. Error handling contract

Typed engine errors include:

- configuration;
- model load;
- inference;
- invalid image;
- provider;
- cancelled;
- timeout;
- resource exhausted;
- internal.

Representative C++ exceptions:

- `ConfigurationError`
- `ModelLoadError`
- `InferenceError`
- `InvalidImageError`
- `ProviderError`
- `CancelledError`
- `TimeoutError`
- `ResourceExhaustedError`
- `InternalError`

### C ABI rule

**No C++ exception may cross a C ABI boundary.**

Central boundary primitives live under:

```text
include/fac_lpr/c_api/error_boundary.hpp
include/fac_lpr/fac_lpr_error.h
```

Rules:

- typed `EngineError` → stable C status;
- `std::bad_alloc` → resource exhausted;
- unknown exception → internal error;
- implementation details and sensitive paths must not leak accidentally.

---

## 10. Logging and privacy

Logging integration includes:

- `ILogger`
- `NullLogger`
- callback logger
- optional spdlog adapter
- TRACE/DEBUG/INFO/WARN/ERROR levels

Privacy defaults prohibit logging by default:

- raw plate images;
- crop bytes;
- full recognized plate text unless explicitly enabled by a higher-level privacy/debug policy;
- secrets/tokens;
- unnecessary filesystem paths.

Logging must be best-effort and must never break recognition. Callback dispatch must follow documented thread-safety behavior.

---

## 11. Models and runtime artifacts

Expected external artifacts:

### Detector

```text
best.onnx
```

Role: YOLO Pose-style plate detector expected to produce plate bounding boxes and four keypoints/corners.

### OCR

```text
lprnet_turkey.onnx
```

Role: Turkish LPRNet-style OCR model.

### Critical rule: NEVER GUESS MODEL CONTRACTS

Do not guess:

- tensor names;
- input/output shapes;
- layout order;
- class count;
- objectness/class offsets;
- keypoint offsets/order;
- LPRNet charset;
- CTC blank index;
- logits axis ordering;
- normalization constants.

Use the model inspector and real runtime artifacts. Configuration scaffolding is not proof of a real model contract.

Real-model tests must either run using externally provisioned artifacts or report a clear skipped/artifact-missing condition. Missing models must never become a fake pass.

---

## 12. Target recognition pipeline

```text
Image
↓
Full-frame detection
↓
Adaptive tile detection when needed
↓
Detection merge / NMS
↓
Geometry validation
↓
Alignment / crop hypotheses
↓
Primary OCR candidates
↓
Turkish grammar + constrained CTC search where applicable
↓
Layout analysis
↓
Multi-crop fusion
↘ optional secondary recognizers
↓
Provider-specific confidence calibration
↓
Cross-source evidence fusion
↓
Recognition decision
↓
PlateRecognitionResult
```

Required properties:

- deterministic ordering and tie breaks;
- preserve raw evidence long enough for diagnostics/fusion;
- strong recognizer disagreement tends toward `REVIEW`;
- fail-closed defaults;
- optional provider failure becomes explicit degraded state when policy allows continuation;
- expensive stages should expose latency metadata.

---

## 13. Turkish plate recognition rules

Issues #22/#23 own Turkish grammar and constrained CTC behavior.

Expected direction:

- province code `01–81`;
- reject `00` and `82+`;
- uppercase ASCII normalization;
- configurable allowed ASCII letter set appropriate for Turkish plates;
- supported civilian format families;
- prefix validation usable for beam-search pruning;
- OCR confusion handling in constrained candidate search rather than detector logic;
- examples of useful ambiguity pairs include `0/O`, `1/I`, `8/B`, `5/S`, `6/G`.

Grammar must remain a separate testable component and must not blindly force invalid OCR into a valid-looking plate.

Double-row/square Turkish plates are in scope as a crop-normalization strategy without corrupting normal single-row behavior.

---

## 14. Important implementation map

### Domain

```text
include/fac_lpr/domain/geometry.hpp
include/fac_lpr/domain/detection.hpp
include/fac_lpr/domain/recognition.hpp
```

### Application

```text
include/fac_lpr/application/providers.hpp
include/fac_lpr/application/config.hpp
include/fac_lpr/application/error.hpp
include/fac_lpr/application/operation_context.hpp
include/fac_lpr/application/image.hpp
include/fac_lpr/application/image_validation.hpp
include/fac_lpr/application/turkish_plate_grammar.hpp
src/application/turkish_plate_grammar.cpp
```

### C boundary

```text
include/fac_lpr/fac_lpr_error.h
include/fac_lpr/fac_lpr_logging.h
include/fac_lpr/c_api/error_boundary.hpp
```

### ONNX Runtime

```text
include/fac_lpr/infrastructure/onnx/onnx_session.hpp
src/infrastructure/onnx/onnx_session.cpp
cmake/OnnxRuntimePrebuilt.cmake
```

### Native image hot path

```text
include/fac_lpr/infrastructure/native_image/native_image.hpp
src/infrastructure/native_image/native_image.cpp
```

### YOLO

```text
include/fac_lpr/infrastructure/yolo/yolo_pose_preprocessor.hpp
src/infrastructure/yolo/yolo_pose_preprocessor.cpp
include/fac_lpr/infrastructure/yolo/yolo_pose_parser.hpp
src/infrastructure/yolo/yolo_pose_parser.cpp
```

### Turkish grammar test

```text
tests/turkish_plate_grammar_tests.cpp
```

---

## 15. Production quality rules

Non-negotiable standards:

- C++20;
- warnings-as-errors in supported builds;
- no exception escapes through C ABI;
- no raw ownership;
- no per-frame ONNX session;
- no unbounded queue/cache/workspace;
- no silent fatal fallback;
- no business/access rule in public engine API;
- checked external size arithmetic;
- bounded image/tile/crop/tensor/workspace resources;
- sensitive image/plate logging off by default;
- model checksum/manifest before production load;
- startup self-test/readiness before production-ready status;
- sanitizer/fuzz/stress/performance/ABI/package smoke before v1;
- no fake validation when artifacts are missing.

---

## 16. Canonical roadmap

The product roadmap is GitHub issues **#1–#78**. Accidental placeholder **#80** is not roadmap work.

### Completed checkpoint

```text
#1  C++20 + CMake skeleton                         CLOSED
#2  dependency management                         CLOSED
#3  domain models/layering                        CLOSED
#4  provider interfaces                           CLOSED
#5  engine config                                 CLOSED
#6  error taxonomy / C ABI exception boundary     CLOSED
#7  structured logging                            CLOSED
#8  model inspector                               CLOSED
#9  ONNX session wrapper                          CLOSED
#10 ImageView/input validation                    CLOSED
#11 YOLO Pose preprocess                          CLOSED
#12 YOLO Pose parser + NMS                        CLOSED
#13 geometry validator                            CLOSED
#14 perspective alignment / homography            CLOSED
#15 adaptive full-frame + tile detection          CLOSED
#16 crop hypothesis generator                     CLOSED
#17 CLAHE/sharpen/threshold enhancers              CLOSED
#18 double-row normalizer                         CLOSED
#19 crop quality scoring                          CLOSED
#20 LPRNet preprocess                             CLOSED
#21 greedy CTC decoder                            CLOSED
```

### Current work

```text
#22 Turkish plate grammar / normalizer             NEXT / OPEN
#23 Turkish constrained CTC beam search
#24 connected-component layout analyzer
#25 multi-crop fusion
#26 recognition ensemble / evidence fusion
#27 optional PaddleOCR provider
#28 generic ONNX OCR adapter
#29 confidence calibration infrastructure
#30 safe technical recognition decision policy
#31 pipeline orchestrator
#32 model manifest/checksum/lifecycle
#33 reusable inference workspace
#34 bounded worker pool/concurrency
#35 public C ABI v1
#36 ABI result buffer/ownership
#37 offline lpr-cli
#38 golden dataset regression infrastructure
#39 multi-detector fusion
#40 long-run memory stress
#41 ASan/LSan/TSan
#42 performance benchmark/latencies
#43 static analysis
#44 fuzz testing
#45 multi-platform CI
#46 dependency security/SBOM
#47 versioned binary packaging
#48 SemVer/ABI policy
#49 diagnostics/metrics snapshot
#50 startup self-test/readiness
#51 best.onnx contract regression
#52 lprnet_turkey.onnx contract regression
#53 real-model end-to-end integration
#54 C# P/Invoke consumer
#55 Python ctypes consumer
#56 engine builder/provider registry/composition root
#57 JSON config adapter/schema
#58 ONNX execution-provider abstraction
#59 cancellation/timeout/deadline propagation
#60 atomic model reload/safe swap
#61 deterministic inference/reproducibility
#62 native C ABI smoke
#63 packaged artifact smoke
#64 public API thread-safety/reentrancy contract
#65 public consumer docs
#66 coverage gate
#67 performance regression gate
#68 third-party licenses/NOTICE
#69 reproducible build/provenance
#70 offline confidence calibration fit tool
#71 evaluation/report tool
#72 CMake package export/C++ consumer
#73 model/test dataset artifact provisioning
#74 production troubleshooting runbook
#75 changelog/automated release
#76 automated ABI compatibility gate
#77 memory/resource budget/OOM guard
#78 production v1 release readiness gate
```

---

## 17. Immediate continuation plan

### Current issue: #22

Inspect:

```text
include/fac_lpr/application/turkish_plate_grammar.hpp
src/application/turkish_plate_grammar.cpp
tests/turkish_plate_grammar_tests.cpp
```

Acceptance criteria from GitHub:

- `00` and `82+` province codes are rejected;
- prefix validator is directly usable by beam-search pruning;
- valid and invalid Turkish plate formats are unit-tested;
- scope also includes uppercase/ASCII normalization, allowed letter set and supported format families.

Procedure:

1. audit current implementation against every criterion;
2. add missing edge cases/tests if needed;
3. perform strongest truthful validation available without wasting hosted Actions budget;
4. add Turkish top-level completion comment;
5. close #22 with `state_reason=completed`;
6. update this checkpoint to #23;
7. continue sequentially.

Do not skip directly to #23 just because constrained beam-search code already exists.

---

## 18. Testing philosophy

### Unit

- grammar;
- decoder;
- geometry;
- NMS;
- crop quality;
- error mapping;
- config validation;
- deterministic fusion/policy.

### Reference/regression

- custom native preprocess vs OpenCV reference;
- model-contract snapshots;
- deterministic ordering;
- ABI symbol/struct snapshots.

### Integration

- real ONNX session load;
- parser against real detector artifact;
- LPRNet decode against real OCR artifact;
- image → result pipeline.

### Production hardening

- sanitizer;
- fuzz;
- long-run memory;
- concurrency stress;
- performance regression;
- packaged artifact smoke;
- external consumer smoke.

Never use mock inference to satisfy real-model issue #53.

---

## 19. Performance and concurrency direction

Performance is measured, not assumed.

Goals:

- no per-frame session creation;
- reusable tensor/workspace capacity;
- zero-copy input/crop where safe;
- fused simple preprocessing passes;
- heavy OpenCV operations only when justified;
- adaptive tiling only when needed;
- benchmark P50/P95/P99;
- separate warm-up from steady state.

The synchronous core comes first. Later concurrency must use a fixed configurable worker count, bounded queue, explicit backpressure/drop policy, reusable per-worker workspace, graceful shutdown and cancellation propagation. Never create one thread per frame.

---

## 20. Future public C ABI

Production ABI is planned in #35/#36.

Expected properties:

- opaque engine handle;
- explicit `_v1` functions;
- no C++ standard-library types in public C headers;
- stable status codes;
- explicit ownership;
- version/size-aware structs where needed;
- documented thread-safety;
- null/double-destroy safety;
- no exception escape.

Conceptual entry points include:

```text
fac_lpr_engine_create_v1
fac_lpr_engine_recognize_v1
fac_lpr_engine_destroy_v1
```

Exact signatures belong to #35/#36 and must not be guessed prematurely.

---

## 21. Multi-provider and confidence direction

Primary recognition is Turkish LPRNet. PaddleOCR is optional secondary evidence and should remain outside the native core through an adapter/worker boundary when implemented.

Confidence philosophy:

```text
raw provider confidence
+ crop quality
+ layout evidence
+ provider agreement
+ detector/geometry confidence
↓
calibration/fusion
↓
technical recognition decision
```

If no calibration dataset exists, calibration should remain identity rather than using invented coefficients.

Strong disagreement tends toward `REVIEW`. False automatic `ACCEPTED` is considered more dangerous than review in the intended ecosystem, so defaults remain fail-closed.

---

## 22. Artifact, privacy and security direction

Production work must eventually include:

- model SHA-256 validation;
- manifest/version association;
- path traversal prevention;
- model/test artifact provisioning outside Git;
- SBOM;
- vulnerability scanning;
- third-party notices;
- artifact checksums;
- provenance/source commit metadata;
- pinned/reproducible dependency strategy;
- no sensitive plate dataset committed accidentally.

Model licensing remains separate from engine dependency licensing.

---

## 23. Production v1 definition of done

Do not call FAC LPR Engine production v1 until #78 is truthfully satisfied.

Minimum production release gates include:

- Windows/Linux clean build;
- unit tests;
- integration tests;
- real `best.onnx` + `lprnet_turkey.onnx` integration;
- golden dataset regression;
- sanitizer/static-analysis/fuzz validation;
- long-run memory validation;
- performance benchmark/regression check;
- ABI compatibility check;
- packaged artifact smoke;
- specified C/C#/Python consumer checks;
- SBOM/licenses/provenance/checksum;
- documentation/readiness/runbook;
- no unresolved critical/high-risk acceptance failures.

A failing critical gate is not downgraded merely to make the checklist green.

---

## 24. Guardrails for future developers/AI agents

Do not:

- re-enable expensive automatic hosted CI without approval;
- guess ONNX contracts;
- commit runtime models or sensitive datasets;
- move access/barrier logic into this engine;
- make OpenCV the Domain/Application image type;
- regress fused preprocessing without benchmark/accuracy evidence;
- create ONNX sessions per frame;
- use raw owning pointers;
- add unbounded queues/caches/workspaces;
- expose exceptions through C ABI;
- log plate images/text by default;
- close issues out of order;
- claim real-model validation without real models;
- fabricate CI/test success;
- treat accidental #80 as roadmap work.

Do:

- keep `PRODUCT.md` synchronized with actual GitHub state;
- use issue acceptance criteria as executable requirements;
- write tests with each change;
- prefer deterministic/config-driven algorithms;
- keep Domain/Application vendor-independent;
- validate dimensions before allocation;
- preserve evidence/explainability;
- benchmark optimizations;
- use real model inspector output when artifacts are available;
- use free/local/self-hosted validation while Actions budget is constrained;
- comment and close completed issues in Turkish, sequentially.

---

## 25. Handoff checkpoint

```text
checkpoint date: 2026-08-22
closed issues: #1 through #21
last closed issue: #21 LPRNet greedy CTC decoder
next issue to audit/close: #22 Turkish plate grammar / normalizer
active development branch: dev
active draft PR: #79
GitHub Actions mode: manual / zero-spend
real ONNX model binaries committed to Git: NO
production v1 ready: NO
```

**Resume at issue #22. Audit acceptance criteria truthfully, close sequentially, update this checkpoint, then continue to #23.**
