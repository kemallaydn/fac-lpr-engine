# FAC LPR Engine — Product, Architecture and AI Handoff

> **Purpose of this document**
>
> This file is the canonical handoff document for a developer or another AI taking over FAC LPR Engine development. It is intentionally more detailed than a normal README. A new agent should be able to read this document, inspect the referenced files/issues, and continue implementation without relying on previous chat history.
>
> **Do not treat this document as a substitute for code or tests.** When this document and the repository differ, inspect the current `dev` branch and update this document as part of the change.

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

The engine recognizes plates and returns technical evidence/results. It is intended to be consumed by applications such as FAC Access, but it must remain usable by unrelated consumers as well.

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

Development work should normally continue on `dev` and keep PR #79 as the integration surface until the production-readiness work is complete.

### Important issue workflow rule

Issues are intended to be completed **sequentially by issue number**.

Do not skip ahead and close later issues simply because some code already exists.

For every issue:

1. read its current acceptance criteria;
2. inspect existing implementation;
3. implement missing pieces;
4. add/adjust tests;
5. perform the strongest truthful validation available;
6. add a **Turkish top-level issue comment** explaining what was done and how it was verified;
7. close the issue with `state_reason=completed`;
8. continue to the next issue.

Typical closing-comment style:

```text
Tamamlandı. <what was implemented>.
Doğrulama: <tests/builds/acceptance evidence>.
```

Never fake test success and never close an issue whose acceptance criteria are not actually met.

---

## 3. Current high-level status

### Confirmed closed issues

The following issues have been completed and closed:

- **#1** C++20 + CMake project skeleton
- **#2** dependency management
- **#3** domain models and layer dependencies
- **#4** provider interfaces / SOLID extension points
- **#5** engine configuration model

The next issue in the required closure sequence is **#6**.

### Work already implemented beyond #5

Code exists for significantly more than the closed-issue count suggests. This happened because foundational pieces were implemented while earlier CI/dependency work was being stabilized.

Important: existing code does **not** automatically mean the corresponding issue can be closed. Validate the complete issue acceptance criteria first.

Current notable implementation state:

- #6 error taxonomy: implemented, including C ABI exception boundary primitives and tests; issue still open pending final validation.
- #7 structured logging: implemented, including callback serialization/thread-safety work and privacy documentation; issue still open pending final validation.
- #8 model inspector CLI: implementation exists.
- #9 ONNX Runtime session wrapper/lifecycle: implementation exists.
- #10 ImageView/input validation: substantial implementation exists and was hardened for caller-owned buffers.
- #11 YOLO preprocess: substantially redesigned to custom native C++ fused preprocessing, avoiding OpenCV in the hot path.
- #12 YOLO output parser/NMS: implementation exists but must be validated against the real model contract before issue closure.
- #13 geometry validator: contract/header exists; implementation was not completed at the point this handoff was written.
- #16/#19: low-level native crop and quality primitives exist, but their complete issue scopes are not necessarily finished.

Do not assume issues #6+ are complete merely from the above summary.

---

## 4. CI / GitHub Actions budget constraint

This is currently a **hard operational constraint**.

The GitHub account reached approximately 90% of its included Actions minutes and the user does not want to spend additional money on hosted Actions.

Therefore the repository has been changed to a **zero-spend CI policy**.

See:

```text
docs/ci-budget.md
```

### Current policy

GitHub-hosted Actions must **not run automatically** on every push/PR while this zero-spend policy is active.

The workflows were changed to manual `workflow_dispatch` execution.

- `foundation-build` is manual.
- `dependency-restore` is manual.
- do not casually restore automatic PR/push matrices;
- do not add expensive hosted-runner workflows without explicit user approval;
- release readiness still requires full Windows/Linux validation eventually.

The reason is not theoretical. Earlier matrix CI was running:

- Windows MSVC Debug
- Windows MSVC Release
- Linux GCC Debug
- Linux GCC Release
- Linux Clang Debug
- Linux Clang Release
- Windows dependency build
- Linux dependency build

on frequent commits, which consumed the account's hosted-runner allowance rapidly.

### Recommended future CI strategy

Until there is a self-hosted runner or a reset/budget change:

```text
routine development:
    local build/tests or other free execution environment

manual quick CI:
    Linux GCC Release only

manual dependency validation:
    Linux first
    Windows only when needed

release candidate:
    full Windows/Linux compiler matrix
    dependency-backed tests
    sanitizers/static analysis
    packaging/ABI/security gates
```

A future self-hosted runner is acceptable and preferred if available, because it avoids paid GitHub-hosted minutes.

Never report a red 0-step hosted-runner failure as a code failure without checking whether a runner was actually allocated.

---

## 5. Technology baseline

Language and build:

- C++20
- CMake 3.25+
- CMakePresets
- Windows x64 / MSVC
- Linux x64 / GCC + Clang

Core dependencies:

- ONNX Runtime
- OpenCV, intentionally minimized
- GoogleTest
- spdlog

Dependency management:

- vcpkg manifest mode for OpenCV/GTest/spdlog and normal C++ dependencies;
- pinned vcpkg baseline;
- ONNX Runtime is intentionally provisioned from official prebuilt Microsoft artifacts instead of being source-built through vcpkg;
- official ORT packages are checksum-pinned;
- model binaries are not dependencies committed into Git.

Why prebuilt ONNX Runtime?

- vcpkg source builds were excessively slow for CI;
- using a pinned official binary keeps builds faster and more deterministic;
- it avoids wasting Actions minutes compiling a large third-party runtime on every clean runner.

---

## 6. Architectural dependency rule

The architecture follows an inward dependency rule:

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

Domain contains stable value types only.

Rules:

- standard C++ only;
- no OpenCV headers;
- no ONNX Runtime headers;
- no PaddleOCR dependency;
- no filesystem/network/process dependency;
- no application UI/backend dependency;
- value semantics and RAII.

Representative domain types:

- `Point2f`
- `BoundingBox`
- `PlateQuadrilateral`
- `Detection`
- `PlateCandidate`
- `RecognitionEvidence`
- `PlateRecognitionResult`
- `RecognitionStatus`

### Application

Application defines use cases and vendor-neutral provider contracts.

Existing provider contracts include:

- `IPlateDetector`
- `IPlateAligner`
- `ICropGenerator`
- `IPlateRecognizer`
- `IPlateLayoutAnalyzer`
- `ICandidateFusion`
- `IConfidenceCalibrator`
- `IDecisionPolicy`

Application also owns portable contracts such as `ImageView`, `OperationContext`, config models and typed errors.

### Infrastructure

Infrastructure implements application contracts using concrete technology:

- ONNX Runtime
- OpenCV where justified
- spdlog
- future PaddleOCR external adapter
- model-specific preprocessing/postprocessing

Do not let concrete provider types leak upward into application/domain/public ABI.

---

## 7. Image-processing architectural decision

A significant design decision was made during development:

**OpenCV must not be the engine's fundamental image abstraction and should not dominate the hot path.**

OpenCV remains useful for complex, well-optimized algorithms, but simple LPR-specific operations are implemented in native C++.

### Custom native C++ hot path

Use custom/native code for:

- image validation;
- caller-owned `ImageView` representation;
- mutable image views;
- checked stride/size arithmetic;
- zero-copy crop views;
- explicit crop copies when contiguous ownership is needed;
- padding/basic pixel operations;
- YOLO bilinear sampling;
- resize/letterbox mapping;
- BGR/RGB/Gray handling;
- normalization;
- HWC → CHW tensor writing;
- reusable tensor/scratch workspaces;
- crop quality metrics such as exposure/clipping/sharpness.

### Keep OpenCV for complex algorithms

OpenCV is appropriate for things such as:

- homography;
- `warpPerspective`;
- CLAHE;
- adaptive thresholding;
- connected components;
- complex morphology/layout helpers.

The goal is not ideological "no OpenCV". The goal is:

- fewer temporary buffers;
- fewer memory passes;
- less allocation;
- smaller dependency surface;
- deterministic control of the inference hot path;
- no need to reimplement difficult CV algorithms unnecessarily.

### Fused YOLO preprocess

The intended custom path is conceptually:

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
direct write into CHW float tensor
```

Do **not** regress this to:

```text
cv::Mat
→ resize temporary
→ color-conversion temporary
→ letterbox temporary
→ normalization temporary
→ CHW tensor
```

unless a benchmark and accuracy test demonstrate a genuine reason.

---

## 8. Ownership and memory rules

These rules are production requirements, not style preferences.

- no raw ownership;
- no public `new/delete` lifecycle;
- RAII everywhere possible;
- `std::unique_ptr` is the default exclusive ownership mechanism;
- caller-owned image memory remains caller-owned;
- `ImageView` does not own image memory;
- `ImageBuffer` owns its buffer;
- reusable workspaces own reusable scratch/tensor capacity;
- no unbounded queues;
- no unbounded tensor/image/crop allocation;
- all image size/stride arithmetic must be checked for overflow;
- C ABI must not return engine-owned dangling `char*` buffers;
- later ABI result buffers should be caller-owned or have an explicit release contract.

### Zero-copy crop rule

A crop can be represented as a sub-view into a strided parent buffer.

For strided views, required byte extent is:

```text
(height - 1) * stride + packed_row_bytes
```

not blindly `height * stride`.

This was already fixed because a valid zero-copy crop should not be rejected merely because padding bytes after the final row are not included in the span.

---

## 9. Error handling contract

Typed engine errors are defined in the application layer.

Error taxonomy includes:

- configuration
- model load
- inference
- invalid image
- provider
- cancelled
- timeout
- resource exhausted
- internal

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

### C ABI boundary rule

**No C++ exception may cross a C ABI boundary.**

A C status mapping primitive already exists under:

```text
include/fac_lpr/c_api/error_boundary.hpp
include/fac_lpr/fac_lpr_error.h
```

Rules:

- typed `EngineError` → stable C status;
- `std::bad_alloc` → resource exhausted;
- unknown exception → internal error;
- exception message/internal implementation detail should not cross the ABI accidentally.

Full public ABI comes later in issues #35/#36, but all future C exports must use the central boundary behavior rather than ad-hoc `try/catch` blocks.

---

## 10. Logging and privacy rules

Logging exists to integrate the native engine with consumer logging systems.

Components include:

- `ILogger`
- `NullLogger`
- callback logger
- optional spdlog adapter
- C callback levels TRACE/DEBUG/INFO/WARN/ERROR

### Privacy defaults

Do not log by default:

- raw plate images;
- crop bytes;
- recognized full plate text unless explicitly permitted by a higher-level privacy/debug policy;
- secrets;
- unnecessary filesystem paths;
- credentials/tokens.

Logging must be best-effort and must not break recognition.

Consumer callbacks can be invoked from concurrent engine operations, so callback dispatch must follow the documented thread-safety behavior. Existing callback logger work serializes callback invocation rather than assuming consumer callbacks are reentrant.

---

## 11. Models and runtime artifacts

Expected external models:

### Detector

```text
best.onnx
```

Known role:

- YOLO Pose-style plate detector;
- expected to produce plate bounding boxes and four plate keypoints.

Approximate previously known file size: ~12.97 MB.

### OCR

```text
lprnet_turkey.onnx
```

Known role:

- Turkish LPRNet OCR model.

Approximate previously known file size: ~1.31 MB.

### Critical rule: NEVER GUESS MODEL CONTRACTS

At the time this handoff was written, the actual model binaries were not available inside the repository/session for authoritative inspection.

Therefore do **not** guess any of the following:

- tensor node names;
- input shapes;
- output shapes;
- layout order;
- class count;
- YOLO keypoint order;
- objectness/class offsets;
- LPRNet charset;
- CTC blank index;
- logits axis ordering;
- model normalization constants.

Use the model inspector and real model artifact to determine these.

Any configurable parser/preprocessor structure added before inspection must remain explicit/model-driven and must not pretend its defaults are the true contract.

### Model files are never committed

`.onnx` runtime files belong outside Git.

Future provisioning work is tracked explicitly in issue #73.

Real-model tests must either:

- receive models from an external/local/secure artifact path and actually run; or
- report a clear **SKIPPED / artifact missing** condition.

They must never turn "model missing" into a fake pass.

---

## 12. Target recognition pipeline

The intended production pipeline is:

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
Crop hypotheses
↓
Primary OCR candidates
↓
Turkish constrained CTC beam search where applicable
↓
Layout analysis
↓
Primary crop fusion
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

Important properties:

- deterministic ordering and tie breaks;
- evidence is preserved rather than collapsed too early;
- disagreement between strong recognizers should cause REVIEW rather than blind ACCEPTED;
- fail-closed defaults;
- optional provider failure may degrade operation according to policy but must not silently corrupt results;
- every expensive stage should eventually expose latency metadata.

---

## 13. Turkish plate recognition assumptions

The engine is designed specifically to support Turkish plates.

General grammar work is tracked in issues #22/#23.

Expected validation direction:

- province code 01–81;
- invalid 00 and 82+ province codes rejected;
- uppercase/ASCII normalization;
- variable letter groups;
- variable numeric suffix groups;
- constrained CTC prefix pruning;
- OCR confusion handling for pairs such as 0/O, 1/I, 8/B, 5/S, 6/G.

Do not encode overly aggressive grammar corrections directly inside the detector or generic recognizer provider. Grammar should remain a separate testable component so raw recognition evidence can still be inspected.

Double-row/square Turkish plates are explicitly in scope and should be normalized as a crop strategy rather than by corrupting single-row behavior.

---

## 14. Current implemented file map

The following files are particularly important when resuming work.

### Domain

```text
include/fac_lpr/domain/geometry.hpp
include/fac_lpr/domain/detection.hpp
include/fac_lpr/domain/recognition.hpp
src/domain/CMakeLists.txt
```

### Application contracts/config/errors

```text
include/fac_lpr/application/providers.hpp
include/fac_lpr/application/config.hpp
include/fac_lpr/application/error.hpp
include/fac_lpr/application/operation_context.hpp
include/fac_lpr/application/image.hpp
include/fac_lpr/application/image_validation.hpp
src/application/image_validation.cpp
```

### C boundary primitives

```text
include/fac_lpr/fac_lpr_error.h
include/fac_lpr/fac_lpr_logging.h
include/fac_lpr/c_api/error_boundary.hpp
```

### Logging

```text
include/fac_lpr/application/logging.hpp
include/fac_lpr/infrastructure/spdlog_logger.hpp
src/infrastructure/spdlog_logger.cpp
docs/logging.md
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

### Geometry

```text
include/fac_lpr/infrastructure/geometry/plate_geometry_validator.hpp
```

At handoff time the geometry implementation itself still required completion.

### Model inspector

```text
tools/model-inspector/CMakeLists.txt
tools/model-inspector/main.cpp
```

### Tests already present

```text
tests/error_boundary_tests.cpp
tests/image_validation_tests.cpp
tests/logging_tests.cpp
tests/native_image_tests.cpp
tests/yolo_preprocessor_tests.cpp
tests/yolo_preprocessor_opencv_reference_tests.cpp
```

---

## 15. Native image subsystem expectations

The native image subsystem should remain small, focused and dependency-light.

It is intended to support reusable primitives such as:

- zero-copy crop view;
- explicit packed crop copy;
- reusable tensor/scratch workspace;
- crop quality metrics;
- simple image sampling and conversion helpers.

### SIMD strategy

Do not prematurely make AVX2/NEON intrinsics the only implementation.

Preferred evolution:

```text
portable scalar reference implementation
↓
correctness/regression tests
↓
benchmark
↓
optional AVX2 implementation
↓
optional ARM NEON implementation
↓
runtime/compile-time strategy selection
```

Reference correctness matters more than clever intrinsics during early development.

---

## 16. Preprocess correctness strategy

Custom preprocess must be validated against a trusted reference, not accepted merely because it is faster.

The repository already includes OpenCV-reference comparison test infrastructure for YOLO preprocessing.

Useful comparison metrics:

- output tensor shape;
- max absolute tensor difference;
- mean tensor difference;
- letterbox metadata;
- determinism;
- later, real YOLO bbox/keypoint/confidence output deltas;
- latency;
- allocation/copy behavior.

Pixel-for-pixel equality with OpenCV is not automatically required if different interpolation conventions are acceptable and real model output/accuracy remains equivalent. But any tolerated difference must be explicit and regression-tested.

---

## 17. ONNX session lifecycle expectations

ONNX sessions are expensive runtime resources.

Rules:

- never create an ONNX session per frame;
- one loaded model should reuse a session/lifecycle object;
- use RAII;
- cache model input/output metadata after load;
- CPU execution provider is baseline;
- future CUDA/DirectML/TensorRT support goes behind the execution-provider abstraction (#58);
- load/inference errors map into the engine error taxonomy;
- future model reload must use safe swap rather than destroying in-flight sessions (#60).

---

## 18. Parser/model-contract strategy

The YOLO parser implementation was deliberately made **configuration-driven** because the actual `best.onnx` output contract must not be guessed.

That means parser concepts may include configurable:

- features-first vs candidates-first layout;
- bbox offsets;
- optional objectness offset;
- class offsets/count;
- keypoint offset/count/stride;
- keypoint x/y/conf offsets;
- thresholds and NMS behavior.

This configurability is scaffolding, not proof of the real model layout.

When `best.onnx` becomes available:

1. run model inspector;
2. document exact contract;
3. create #51 regression snapshot;
4. update parser configuration/default production composition only from observed facts;
5. run real fixture inference.

The same rule applies to LPRNet and issue #52.

---

## 19. Production quality rules

The following are non-negotiable project standards:

- C++20;
- warnings-as-errors in supported builds;
- no exception escapes through C ABI;
- no raw ownership;
- no per-frame ONNX session;
- no unbounded queue;
- no silent fatal fallback;
- no model-specific business rule in public API;
- no barrier/access-control logic;
- checked arithmetic for externally controlled dimensions/sizes;
- bounded image/tile/crop/tensor/workspace resources;
- logging hides sensitive image/plate content by default;
- model manifest/checksum before production load;
- startup self-test/readiness before reporting production-ready;
- sanitizer/fuzz/stress/performance/ABI/package smoke before production v1;
- no fake tests when real artifacts are missing.

---

## 20. Backlog / issue roadmap

The canonical backlog is GitHub issues **#1–#78**.

There is also an accidental placeholder issue **#80**. It is not part of the product roadmap and should not be treated as a feature requirement.

### Foundation and contracts

- #1 C++20 + CMake skeleton — **closed**
- #2 dependency management — **closed**
- #3 domain models/layering — **closed**
- #4 provider interfaces — **closed**
- #5 engine config — **closed**
- #6 error taxonomy / C ABI exception boundary — **NEXT TO CLOSE AFTER VALIDATION**
- #7 structured logging
- #8 model inspector
- #9 ONNX session wrapper
- #10 ImageView/input validation

### Detector and native image path

- #11 YOLO Pose preprocess
- #12 YOLO Pose parser + NMS
- #13 geometry validator
- #14 perspective alignment / homography
- #15 adaptive full-frame + tile detection
- #16 crop hypothesis generator
- #17 CLAHE/sharpen/threshold enhancers
- #18 double-row normalizer
- #19 crop quality scoring

### LPRNet / Turkish OCR

- #20 LPRNet preprocess
- #21 greedy CTC decoder
- #22 Turkish plate grammar/normalizer
- #23 Turkish constrained CTC beam search
- #24 connected-component layout analyzer
- #25 multi-crop fusion
- #26 recognition ensemble / evidence fusion
- #27 optional PaddleOCR provider
- #28 generic ONNX OCR adapter
- #29 confidence calibration infrastructure
- #30 safe technical recognition decision policy
- #31 pipeline orchestrator

### Model/runtime lifecycle

- #32 model manifest/checksum/lifecycle
- #33 reusable inference workspace
- #34 bounded worker pool/concurrency
- #35 public C ABI v1
- #36 ABI result buffer/ownership
- #37 offline `lpr-cli`
- #38 golden dataset regression infrastructure
- #39 multi-detector fusion

### Production hardening

- #40 long-run memory stress
- #41 ASan/LSan/TSan
- #42 performance benchmark/latencies
- #43 static analysis
- #44 fuzz testing
- #45 multi-platform CI
- #46 dependency security/SBOM
- #47 versioned binary packaging
- #48 SemVer/ABI policy
- #49 diagnostics/metrics snapshot
- #50 startup self-test/readiness

### Real model validation and consumers

- #51 `best.onnx` contract regression
- #52 `lprnet_turkey.onnx` contract regression
- #53 real-model end-to-end integration
- #54 C# P/Invoke consumer
- #55 Python ctypes consumer

### Composition/config/provider/runtime extensions

- #56 engine builder/provider registry/composition root
- #57 JSON config adapter/schema
- #58 ONNX execution-provider abstraction
- #59 cancellation/timeout/deadline propagation
- #60 atomic model reload/safe swap
- #61 deterministic inference/reproducibility

### ABI/package/documentation gates

- #62 native C ABI smoke
- #63 packaged artifact smoke
- #64 public API thread-safety/reentrancy contract
- #65 public consumer docs
- #66 coverage gate
- #67 performance regression gate
- #68 third-party licenses/NOTICE
- #69 reproducible build/provenance

### Offline evaluation/release operations

- #70 offline confidence calibration fit tool
- #71 evaluation/report tool
- #72 CMake package export/C++ consumer
- #73 model/test dataset artifact provisioning
- #74 production troubleshooting runbook
- #75 changelog/automated release
- #76 automated ABI compatibility gate
- #77 memory/resource budget/OOM guard
- #78 production v1 release readiness gate

---

## 21. Immediate continuation plan for the next AI

The next agent should **not restart the architecture from scratch**.

Proceed in this order:

### Step 1 — inspect the current `dev` head

Confirm:

- branch head;
- PR #79 still open;
- automatic GitHub Actions remain disabled/manual under zero-spend policy;
- files listed in this document still exist.

### Step 2 — finish and close #6

Inspect:

```text
include/fac_lpr/application/error.hpp
include/fac_lpr/fac_lpr_error.h
include/fac_lpr/c_api/error_boundary.hpp
tests/error_boundary_tests.cpp
docs/error-handling.md
```

Acceptance must include:

- no C++ exception crossing C boundary primitive;
- typed mapping;
- `std::bad_alloc` mapping;
- unknown exception → internal error;
- no sensitive implementation detail unnecessarily exposed.

Run tests in a free/local environment if hosted Actions cannot run.

Only after truthful validation: Turkish closing comment + close #6.

### Step 3 — finish and close #7

Inspect callback logging thread safety and tests.

Acceptance:

- no raw sensitive plate/image logging by default;
- logger never throws into recognition path;
- callback behavior is thread-safe/serialized as designed.

Then close #7.

### Step 4 — #8 model inspector

Compile and verify the CLI implementation.

Do not close #8 unless it can actually read the required real model artifacts or the issue acceptance criteria are amended explicitly. The implementation existing is not enough if `best.onnx` and `lprnet_turkey.onnx` have never been inspected.

### Step 5 — #9 ONNX session wrapper

Validate RAII and model/session reuse. Ensure no frame-level session creation.

### Step 6 — #10 image boundary

Validate null pointer handling, byte count, stride, dimensions and overflow before span/view construction.

### Step 7 — #11 custom fused YOLO preprocess

Preserve the custom native hot path. Validate determinism and OpenCV-reference tolerances. Real model shape must come from model inspector once artifact exists.

### Step 8 — #12

Finish tests and wait for real model contract before declaring production defaults correct.

### Step 9 — #13

Implement `plate_geometry_validator.cpp` or equivalent. The existing header/contract is not sufficient.

Then continue sequentially.

---

## 22. Known validation history

Before the GitHub Actions budget constraint caused hosted-runner scheduling failures, the repository successfully achieved important cross-platform validation milestones.

Confirmed earlier successful foundation matrix included:

- Windows MSVC Debug
- Windows MSVC Release
- Linux GCC Debug
- Linux GCC Release
- Linux Clang Debug
- Linux Clang Release

A dependency-backed run also successfully completed on both Windows and Linux with:

- dependency restore;
- CMake configure;
- native build;
- native tests.

This successful validation covered the repository state around the native image/custom preprocess refactor before later #6/#7/#10 hardening commits.

After the account reached the Actions/budget limit, later runs began failing with **zero executed steps**, indicating runner/account allocation failure rather than a meaningful compiler/test result.

Therefore later commits must still be revalidated in a free/local execution environment before corresponding issues are closed.

---

## 23. Testing philosophy

Tests should be layered:

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
- model contract snapshots;
- deterministic ordering;
- ABI struct/symbol snapshots.

### Integration

- real ONNX session load;
- detector parser against real model;
- LPRNet decode against real model;
- image → result pipeline.

### Production hardening

- sanitizer;
- fuzz;
- long-run memory;
- concurrency stress;
- performance regression;
- packaged artifact smoke;
- external consumer smoke.

Never use a mock inference result to satisfy issue #53.

---

## 24. Performance direction

Performance should be measured, not assumed.

Important goals:

- no per-frame session creation;
- reuse tensor/workspace capacity;
- zero-copy input/crop where safe;
- fuse simple preprocessing passes;
- keep heavy OpenCV operations conditional;
- adaptive tiling only when needed;
- bounded worker count/queue;
- benchmark P50/P95/P99;
- separate warm-up from steady-state.

A custom implementation is not automatically faster just because it is written in C++. Keep a scalar correct reference first, then optimize based on profiling.

---

## 25. Concurrency direction

The core engine should remain synchronous first.

Later optional runtime concurrency (#34) must be bounded:

- configurable fixed worker count;
- bounded queue;
- explicit backpressure/drop policy;
- per-worker reusable workspace;
- safe session/provider sharing according to provider guarantees;
- graceful shutdown;
- cancellation propagation;
- no orphan threads.

Do not create one thread per frame.

---

## 26. Future C ABI direction

The production public C ABI is planned in #35/#36.

Expected properties:

- opaque engine handle;
- explicit `_v1` entry points;
- export/calling-convention macros;
- no C++ standard-library type in public C header;
- versioned/size-aware structs where needed;
- explicit thread-safety contract;
- explicit caller/result-buffer ownership;
- null/double-destroy safety;
- stable error codes;
- no exception escape.

Expected conceptual functions include:

```text
fac_lpr_engine_create_v1
fac_lpr_engine_recognize_v1
fac_lpr_engine_destroy_v1
```

The exact signature should be designed when #35/#36 are implemented, not guessed prematurely.

---

## 27. PaddleOCR direction

PaddleOCR is optional secondary evidence, not a mandatory core dependency.

Preferred design:

- external worker/process adapter;
- bounded timeout;
- cancellation support;
- malformed-response protection;
- optional bounded SHA-256 cache;
- unavailable provider → explicit degraded state according to policy;
- LPRNet can continue as primary provider when policy allows.

Do not link a large Python/Paddle runtime into the native engine core just for convenience.

---

## 28. Confidence and decision philosophy

Raw model confidence is not automatically a calibrated probability.

The intended process is:

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

If no calibration dataset exists, calibration should be identity rather than invented coefficients.

Strong disagreement should tend toward REVIEW.

A false automatic ACCEPT is more dangerous than REVIEW in the intended access-control ecosystem, so defaults should remain fail-closed.

---

## 29. Artifact, privacy and security direction

Production must eventually include:

- model SHA-256 validation;
- manifest/version association;
- path traversal prevention;
- test/model artifact provisioning outside Git;
- SBOM;
- vulnerability scanning;
- third-party notices;
- artifact checksums;
- provenance/source commit metadata;
- reproducible/pinned dependency strategy;
- no sensitive plate datasets committed accidentally.

Model licensing is separate from engine dependency licensing and must remain explicit.

---

## 30. Release definition of done

Do not call the project production v1 until issue #78 acceptance is truthfully satisfied.

A real production release must include at minimum:

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
- C/C#/Python consumer checks where specified;
- SBOM/licenses/provenance/checksum;
- documentation/readiness/runbook;
- no unresolved critical/high-risk acceptance failures.

If a critical gate fails, production release is not ready. Do not downgrade the gate merely to make the checklist green.

---

## 31. Things the next AI must not do

- Do not re-enable expensive automatic GitHub-hosted CI without permission.
- Do not guess ONNX shapes/node names/keypoint ordering/charset/blank index.
- Do not commit `best.onnx`, `lprnet_turkey.onnx` or sensitive plate datasets.
- Do not move access-control/barrier logic into this engine.
- Do not make OpenCV the domain/application image type.
- Do not regress custom fused preprocessing without benchmark/accuracy evidence.
- Do not create ONNX sessions per frame.
- Do not use raw owning pointers for runtime resources.
- Do not add unbounded queues/caches/workspaces.
- Do not expose exceptions across C ABI.
- Do not log plate images/text by default.
- Do not close GitHub issues out of order.
- Do not claim real-model validation without real models.
- Do not fabricate CI/test success because hosted Actions are unavailable.
- Do not treat accidental issue #80 as roadmap work.

---

## 32. Things the next AI should proactively do

- keep this `PRODUCT.md` updated when architectural decisions change;
- use GitHub issues as executable acceptance criteria;
- write tests with each implementation rather than batching them at the end;
- prefer deterministic/config-driven algorithms;
- keep domain/application independent from vendors;
- validate external dimensions before allocation;
- preserve evidence and explainability in result structures;
- benchmark optimizations;
- use real model inspector output as soon as model artifacts become available;
- build a free/local/self-hosted validation path while hosted Actions budget is constrained;
- comment and close completed issues in Turkish, sequentially.

---

## 33. Handoff checkpoint

At the time this file was created:

```text
closed issues: #1, #2, #3, #4, #5
next issue to close: #6
active development branch: dev
active draft PR: #79
GitHub Actions mode: manual / zero-spend
real ONNX model binaries in Git: NO
production v1 ready: NO
```

Latest development work before this document included:

- C ABI error-boundary primitives/tests for #6;
- structured/thread-safe logging work for #7;
- hardened caller-owned image validation for #10;
- native zero-copy crop/workspace/quality primitives;
- custom fused YOLO preprocessing;
- configurable YOLO parser/NMS scaffolding;
- ONNX session wrapper;
- model inspector CLI;
- manual CI budget policy.

**Resume at issue #6, validate truthfully, close sequentially, and continue the roadmap.**
