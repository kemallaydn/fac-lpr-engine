# FAC LPR Engine — Product, Architecture and Delivery Context

> This document is the canonical product/context brief for FAC LPR Engine. It is intentionally detailed so that another engineer or AI agent can read this file alone and understand what is being built, why it exists, what it must not do, the target architecture, model assumptions, runtime pipeline, production constraints, testing expectations, current implementation state, and remaining roadmap.

## 1. Product summary

FAC LPR Engine is an independent, reusable, production-grade native license plate recognition engine.

Its job is deliberately narrow:

- receive an image from a caller,
- detect one or more license plates,
- rectify plate geometry where useful,
- generate OCR-ready crop hypotheses,
- recognize Turkish license plate text,
- combine evidence from one or more recognizers/providers,
- calibrate confidence,
- return a structured recognition result and optional diagnostics/evidence.

The engine is designed to be embedded by FAC Access or any other consumer. It is not tied to a single application, UI, backend, database, camera vendor or business workflow.

The engine output answers one question:

> “What plate did the image most likely contain, with what evidence and confidence?”

It does **not** answer:

> “Should the barrier/gate/door open?”

That decision belongs to the consuming application.

---

## 2. Product boundaries

### 2.1 In scope

FAC LPR Engine owns:

- image input validation,
- plate detection,
- keypoint/geometry validation,
- full-frame and tile detection strategy,
- detection merge/NMS,
- plate alignment and homography,
- crop generation,
- crop enhancement strategies,
- double-row normalization,
- crop quality scoring,
- OCR preprocessing,
- OCR decoding,
- Turkish plate grammar and normalization,
- candidate generation,
- candidate fusion,
- provider confidence calibration,
- engine-level recognition decision,
- result/evidence DTOs,
- model/session lifecycle,
- diagnostics and performance metrics,
- public native ABI,
- CLI/test/evaluation tooling,
- model contract inspection and validation.

### 2.2 Explicitly out of scope

FAC LPR Engine must **not**:

- open barriers,
- contain access-control rules,
- decide whether a vehicle/person is allowed,
- own customer/business authorization logic,
- directly call the Spring backend,
- own REST/API client behavior,
- own RTSP camera lifecycle,
- own camera discovery/configuration,
- own application database state,
- own FAC Access UI,
- write recognition results directly into an application database,
- hard-code assumptions from one consuming application into the public engine API.

The engine may be used by FAC Access, but it must remain independently reusable.

---

## 3. Target technical baseline

Primary implementation stack:

- C++20
- CMake 3.25+
- CMake Presets
- vcpkg for selected C/C++ dependencies
- ONNX Runtime for neural-network inference
- OpenCV only where it genuinely provides high-value image-processing primitives
- GoogleTest
- spdlog adapter
- stable versioned C ABI for language-neutral consumers

Baseline platforms:

- Windows x64 / MSVC
- Linux x64 / GCC
- Linux x64 / Clang

Future-compatible design should allow:

- Windows/Linux ARM64,
- ONNX Runtime GPU execution providers,
- CUDA/TensorRT/DirectML/other providers where justified,
- additional OCR engines,
- additional plate detectors,
- additional model versions.

No architecture decision should unnecessarily make those impossible.

---

## 4. Repository and development model

Repository:

- `kemallaydn/fac-lpr-engine`

Branches:

- `main`: stable base
- `dev`: active development

Continuous draft PR:

- PR #79 — FAC LPR Engine production development

Runtime model binaries must not be committed to the repository.

Generated build output belongs under `build/` and must not pollute source directories.

---

## 5. Core architecture

Dependency direction is inward:

```text
Public API / Composition Root
            |
            v
      Infrastructure
            |
            v
       Application
            |
            v
          Domain
```

Equivalent mental model:

```text
API -> Application -> Domain
Infrastructure -> Application + Domain
```

### 5.1 Domain

Domain contains stable value models only.

Examples:

- `Point2f`
- `BoundingBox`
- `PlateQuadrilateral`
- `Detection`
- `PlateCandidate`
- `RecognitionEvidence`
- `PlateRecognitionResult`
- `RecognitionStatus`

Rules:

- standard C++ only,
- no OpenCV headers,
- no ONNX Runtime headers,
- no Paddle-specific types,
- no filesystem/network/logging dependencies,
- no raw ownership,
- value semantics and RAII containers by default.

### 5.2 Application

Application defines use-case-level contracts and engine configuration.

Important provider interfaces:

- `IPlateDetector`
- `IPlateAligner`
- `ICropGenerator`
- `IPlateRecognizer`
- `IPlateLayoutAnalyzer`
- `ICandidateFusion`
- `IConfidenceCalibrator`
- `IDecisionPolicy`

Operations that can be expensive must accept an operation context capable of carrying:

- cancellation,
- deadline/timeout.

Application must remain vendor/model implementation agnostic.

### 5.3 Infrastructure

Infrastructure contains concrete integrations and optimized implementations, including:

- ONNX Runtime wrappers,
- detector/OCR adapters,
- native image kernels,
- minimal OpenCV-backed complex image-processing primitives,
- spdlog adapter,
- execution-provider adapters.

### 5.4 Public API / composition root

The outer layer composes providers and exposes the engine to consumers.

Vendor-specific implementation types must not leak into the public API.

C++ exceptions must never cross a C ABI boundary.

---

## 6. Design patterns and extension strategy

Use patterns where they solve real extensibility problems, not as decoration.

Recommended patterns:

- Strategy for detector/recognizer/decision variants,
- Factory or registry for provider resolution,
- Pipeline for recognition flow,
- Adapter for third-party OCR/model runtimes,
- Composite for multi-provider evidence,
- Builder/composition root for engine assembly.

Avoid pattern theater and unnecessary abstraction layers.

The core should not require code changes when adding a new detector or OCR provider if an existing provider contract is sufficient.

---

## 7. Recognition pipeline

Target runtime flow:

```text
Image
↓
Full-frame detection
↓
Adaptive tile detection
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

Engine-level final status is limited to:

- `ACCEPTED`
- `REVIEW`
- `REJECTED`

These statuses describe recognition quality/confidence only. They are **not access decisions**.

---

## 8. Detector model

Known production detector artifact:

- `best.onnx`
- approximately 12.97 MB
- YOLO Pose style plate detector with plate keypoints/corners

The detector is expected to provide plate localization and, where supported by the model contract, four plate corner/keypoint locations.

Critical rule:

> Tensor names, exact input/output shapes, class layout, output orientation, keypoint offsets/order and other tensor contract details must never be guessed.

They must be discovered from the real model using the model inspector and later locked with contract regression tests.

The code must support model-driven configuration instead of embedding guessed tensor dimensions.

---

## 9. OCR model

Known primary OCR artifact:

- `lprnet_turkey.onnx`
- approximately 1.31 MB
- Turkish LPRNet-style OCR model

Again, the following must not be guessed:

- input tensor name,
- output tensor name,
- tensor shapes,
- time/class dimension order,
- charset,
- blank index,
- model-specific preprocessing details.

Those details must come from the real artifact/model manifest.

Primary OCR path is intended to support:

- deterministic preprocessing,
- greedy CTC decoding,
- Turkish constrained CTC beam search,
- Turkish plate grammar normalization,
- alternative candidate output.

---

## 10. Turkish plate constraints

The project targets Turkish license plates.

Typical formatting family:

```text
34 ABC 123
34 AB 1234
34 A 12345
```

The recognition layer should exploit known Turkish syntax carefully to improve OCR without inventing characters.

Expected grammar concepts include:

- province code starts with digits,
- subsequent group is letters,
- final group is numeric,
- letter group length commonly 1–3,
- numeric group length commonly 2–5 depending on valid format family,
- normalization may remove spaces/separators before validation and reformat for output.

The grammar layer should be data/config driven enough to evolve and must not blindly force invalid OCR into a valid-looking plate.

Ambiguous OCR examples previously observed include:

- `D ↔ 0`
- `D ↔ 8`
- `V ↔ Y`

Candidate search/grammar should help disambiguate these when evidence supports it.

---

## 11. Multi-frame recognition strategy

Real deployments may see a moving vehicle across many frames.

The broader system may provide 15–20 frames around a vehicle pass. FAC LPR Engine should be designed so callers can combine frame-level results or future engine orchestration can support temporal voting.

Useful concepts:

- select sharper frames,
- avoid relying on one blurred frame,
- aggregate repeated plate candidates,
- use confidence/evidence voting,
- reject inconsistent outliers.

Multi-frame support must remain bounded; no unbounded queues or retained frame history.

---

## 12. Image input model

The public/application image abstraction is based on caller-owned memory.

Core concepts:

- `ImageView`: non-owning read-only image
- `MutableImageView`: non-owning writable image
- `ImageBuffer`: owning image storage
- `ImageRegion`: crop rectangle
- `PixelFormat`: at least Gray8/BGR8/RGB8

Ownership rule:

```text
ImageView          -> does not own memory
MutableImageView   -> does not own memory
ImageBuffer        -> owns memory
Workspace          -> owns reusable temporary buffers
```

Input validation must reject:

- null pointer with non-zero bytes,
- zero width/height,
- unsupported pixel format,
- impossible stride,
- width × channels overflow,
- stride × row arithmetic overflow,
- buffer smaller than the actual strided extent,
- dimensions over configured limits,
- byte size over configured limits.

Zero-copy views are preferred when safe.

---

## 13. Native image hot path

A deliberate architecture decision was made to avoid using OpenCV as the fundamental image abstraction or for simple hot-path operations.

The engine should use custom portable C++ for:

- image validation,
- zero-copy crop views,
- explicit crop copy,
- padding/basic region operations,
- resize where required by model preprocessing,
- letterbox mapping,
- BGR/RGB/Gray sampling,
- normalization,
- HWC → CHW tensor writing,
- quality metrics,
- basic image statistics.

Why:

- fewer allocations,
- fewer temporary image buffers,
- fewer memory passes,
- easier buffer reuse,
- deterministic behavior,
- smaller runtime dependency surface,
- easier future SIMD specialization.

### 13.1 Fused YOLO preprocess

The preferred detector preprocess is:

```text
Input ImageView
      ↓
bilinear sample + letterbox mapping
      ↓
BGR/RGB handling
      ↓
normalize
      ↓
direct CHW tensor write
```

There should not be an unnecessary chain like:

```text
source
→ resized Mat
→ RGB Mat
→ padded Mat
→ float Mat
→ CHW tensor
```

The custom implementation should write directly into a reusable tensor buffer.

### 13.2 Scalar first, SIMD later

First implementation should be portable scalar C++20 and serve as the correctness reference.

Potential later specializations:

- AVX2/SSE on x86,
- NEON on ARM.

Do not prematurely create an intrinsics maintenance burden before profiling proves value.

---

## 14. OpenCV policy

OpenCV remains useful, but only for image operations where reimplementing mature algorithms would create unnecessary risk.

Expected OpenCV use:

- homography/perspective transform,
- `warpPerspective`,
- CLAHE,
- adaptive thresholding,
- connected-components/layout analysis,
- possibly reference implementations used in regression tests.

OpenCV should **not** own the entire image pipeline.

Not needed for the core engine:

- video capture,
- RTSP ownership,
- HighGUI,
- OpenCV DNN,
- stitching,
- unrelated ML modules.

The dependency should remain minimal.

---

## 15. Crop generation

Crop generation must support multiple bounded hypotheses because OCR quality can vary strongly by crop and enhancement.

Expected crop families may include:

- raw bbox crop,
- geometry-aligned crop,
- padded crop,
- CLAHE-enhanced crop,
- sharpened crop,
- thresholded crop,
- double-row normalized crop.

`CropConfig.max_hypotheses` bounds the number of generated alternatives.

Simple rectangular crops should use:

- zero-copy views when lifetime and stride semantics allow,
- explicit packed copy only when a contiguous owned image is required.

---

## 16. Crop quality

Crop quality should be calculated with custom C++ rather than OpenCV where practical.

Metrics may include:

- sharpness,
- exposure,
- clipped-pixel ratio,
- overall quality score.

Goals:

- deterministic,
- bounded score range,
- low allocation,
- few memory passes,
- usable as evidence during candidate calibration/fusion.

Quality scoring must not be treated as an access-control signal. It is recognition evidence only.

---

## 17. Geometry

Detector geometry can include:

- bounding box,
- optional four-corner quadrilateral,
- per-keypoint confidence.

Geometry validation should evaluate:

- finite coordinates,
- valid bounding box,
- plausible aspect ratio,
- minimum useful pixel dimensions,
- keypoint confidence,
- corner ordering,
- polygon deformation,
- polygon/bbox area relationship,
- pathological or degenerate shapes.

Existing geometry configuration includes concepts such as:

- keypoint confidence threshold,
- min/max bbox ratio,
- preferred ratio range,
- minimum horizontal/vertical edge size,
- polygon area ratio,
- allowed point displacement relative to bbox.

Corner ordering must be deterministic before homography.

---

## 18. Full-frame + tile detection

Detection strategy should not rely only on resizing an entire high-resolution frame to model input size because small distant plates can disappear.

Target strategy:

1. run full-frame detector pass,
2. evaluate whether additional tile detection is useful,
3. generate bounded overlapping tiles,
4. run detector on tiles,
5. map detections back to source coordinates,
6. merge duplicate detections,
7. apply deterministic NMS/grouping.

Defaults currently discussed/configured include roughly:

- tile width 1280,
- tile height 960,
- overlap ratio 0.20.

These are config defaults, not model contract constants.

---

## 19. Detection output parsing

YOLO Pose output parsing must be driven by an explicit output specification.

The parser design supports configuration for:

- features-first vs candidates-first layouts,
- box offset,
- optional objectness offset,
- class offset/count,
- keypoint offset/count/stride,
- x/y/conf offsets,
- confidence threshold,
- NMS threshold,
- max detections,
- provider name.

No real `best.onnx` tensor layout should be assumed until model inspection confirms it.

Parser responsibilities:

- rank/element count validation,
- finite numeric validation,
- confidence composition,
- bbox conversion,
- coordinate reverse mapping through letterbox metadata,
- optional quadrilateral creation,
- deterministic sorting,
- IoU NMS,
- max result cap.

---

## 20. OCR candidate pipeline

Target OCR pipeline:

```text
Plate crop
↓
model-specific preprocess
↓
OCR logits
↓
greedy CTC baseline
↓
constrained CTC beam search
↓
Turkish grammar/normalization
↓
PlateCandidate[]
```

Multiple recognizers may contribute evidence.

Examples:

- primary Turkish LPRNet provider,
- optional PaddleOCR adapter,
- generic ONNX OCR adapter,
- future vendor/provider models.

Core candidate fusion should not care which library generated a candidate.

---

## 21. Confidence and decision policy

Raw confidence values from different providers are not automatically comparable.

The engine therefore needs:

- provider-specific calibration,
- crop quality as calibration context,
- source/crop type metadata,
- evidence fusion,
- safe recognition decision policy.

Current config defaults include values around:

- accepted confidence threshold: 0.78,
- minimum crop quality: 0.28,
- minimum effective detector confidence: 0.50,
- fail-closed: true.

These are defaults to be calibrated against real validation data, not sacred constants.

The engine must prefer REVIEW/REJECT over fabricating certainty.

---

## 22. Error model

Application error taxonomy currently includes:

- configuration,
- model load,
- inference,
- invalid image,
- provider,
- cancelled,
- timeout,
- resource exhausted,
- internal.

Typed C++ exceptions map to these categories.

C ABI policy:

- no exception escapes,
- typed engine errors map to stable C status codes,
- allocation failures map to resource exhaustion,
- unknown exceptions map to internal error,
- implementation messages/details do not have to cross the ABI.

Do not leak secrets or unnecessary internal paths in consumer-visible errors.

---

## 23. Logging and privacy

Logging is structured and optional.

Contracts include:

- `ILogger`,
- `NullLogger`,
- callback logger,
- spdlog adapter,
- C log callback levels TRACE/DEBUG/INFO/WARN/ERROR.

Rules:

- logging must never break recognition,
- logger adapters catch logging failures,
- callback invocation must be thread-safe,
- default logs must not include raw plate images,
- default logs should avoid raw recognized plate text unless explicitly enabled by a higher-level privacy policy,
- request/correlation IDs are preferred over sensitive payloads.

---

## 24. Model lifecycle

Model sessions are long-lived resources.

Required behavior:

- `Ort::Env`/`Ort::Session` managed with RAII,
- no session construction per frame,
- model/session metadata cached,
- model load failures mapped to engine error taxonomy,
- future execution provider selection abstracted,
- future atomic model reload without stopping in-flight work.

Later production features include:

- manifest,
- checksums,
- startup self-test,
- readiness status,
- safe atomic reload.

---

## 25. Model inspector

The repository contains/plans `fac-lpr-model-info`, a CLI for reading ONNX contract metadata.

It should print:

- model path/name,
- producer,
- graph name,
- model domain,
- model version,
- description,
- opset imports,
- input node names/types/shapes,
- output node names/types/shapes.

Purpose:

- eliminate tensor-contract guessing,
- support manifest creation,
- enable model contract regression tests.

Real acceptance requires running it against the actual `best.onnx` and `lprnet_turkey.onnx` artifacts when those artifacts are provisioned.

---

## 26. External model artifact policy

Known model files are external artifacts and must never be committed.

At minimum:

- `best.onnx`
- `lprnet_turkey.onnx`

Future artifact provisioning should define:

- expected filename,
- semantic model ID/version,
- checksum,
- source/provisioning instructions,
- licensing notes,
- expected tensor contract,
- startup validation behavior.

Tests requiring real models must clearly report “artifact missing/not provisioned” rather than fake a pass.

---

## 27. Dependency strategy

A hybrid dependency strategy is intentional.

vcpkg manages selected libraries such as:

- OpenCV,
- GoogleTest,
- spdlog.

ONNX Runtime is handled as a pinned official Microsoft prebuilt package rather than compiling the vcpkg port from source in CI.

Reasons:

- dramatically lower CI time,
- avoid unnecessary source compilation,
- work around problematic platform-specific vcpkg behavior,
- deterministic checksum verification.

Current prebuilt ONNX Runtime version:

- 1.23.2

Checksums are pinned in repo for Linux x64 and Windows x64.

---

## 28. CI policy under zero additional budget

The GitHub account has a limited hosted Actions allowance and no additional spend is allowed.

Observed monthly allocation:

- 3,000 Actions minutes,
- approximately 2,750 minutes were already used when the usage warning arrived.

Previous CI design was too expensive because every commit could create:

- Windows Debug,
- Windows Release,
- Linux GCC Debug,
- Linux GCC Release,
- Linux Clang Debug,
- Linux Clang Release,
- Linux dependency build,
- Windows dependency build.

The workflows were therefore changed to manual execution (`workflow_dispatch`) to stop automatic hosted-runner spending.

Policy while budget is constrained:

- code changes must not automatically consume hosted runner minutes,
- use local/self-hosted validation when available,
- hosted CI should be run manually only when a meaningful checkpoint needs validation,
- full matrix should be reserved for release/readiness checkpoints,
- do not mark unexecuted real-model/CI acceptance as passed.

A future self-hosted runner is a valid zero-GitHub-minute option.

---

## 29. Concurrency and resource behavior

Production constraints:

- synchronous core API first,
- bounded optional worker pool,
- bounded queue,
- no unbounded work retention,
- cancellation/deadline propagation,
- workspace/buffer reuse,
- no per-frame session creation,
- checked arithmetic,
- explicit maximum image size,
- explicit memory/resource budgets,
- graceful resource-exhausted errors rather than uncontrolled OOM.

Current configuration defaults include:

- worker count: 1,
- queue capacity: 8,
- max image dimension: 8192 × 8192,
- max image bytes: 128 MB,
- recognition timeout: 5 seconds.

Hard safety validation also caps absurd configuration values.

---

## 30. Public ABI requirements

The long-term public API must support non-C++ consumers reliably.

Requirements:

- stable versioned C ABI,
- explicit opaque handle lifecycle,
- no exception escape,
- clear result-buffer ownership,
- thread-safety/reentrancy contract,
- deterministic status/error mapping,
- ABI compatibility testing,
- native C smoke consumer,
- C# P/Invoke consumer,
- Python ctypes consumer.

Later CMake package export should also support native C++ consumers.

---

## 31. Diagnostics and operations

Production engine should expose diagnostics without leaking sensitive plate/image data.

Expected metrics/snapshot concepts:

- model readiness,
- active model version/hash,
- inference counts,
- accepted/review/rejected counts,
- detector/OCR latency,
- total pipeline latency,
- timeout/cancellation counts,
- resource-exhausted count,
- provider failures,
- queue/workspace health,
- model reload status.

The engine should support startup readiness/self-test before serving production traffic.

---

## 32. Testing strategy

Testing is a first-class product requirement.

### 32.1 Unit tests

Cover:

- config validation,
- checked image arithmetic,
- C ABI exception boundary,
- logging behavior,
- callback thread safety,
- native crop behavior,
- image quality metrics,
- YOLO preprocess math,
- parser/NMS,
- grammar,
- CTC decode,
- geometry validation,
- candidate fusion,
- decision policy.

### 32.2 Reference comparisons

Custom image preprocessing must be compared with an OpenCV reference implementation.

Measure:

- tensor maximum absolute difference,
- mean difference,
- letterbox metadata agreement,
- detector output difference when a real model is available,
- latency,
- allocation behavior.

The goal is model-equivalent correctness, not necessarily bit-identical pixels.

### 32.3 Real-model contract tests

When artifacts are provisioned:

- `best.onnx` contract regression,
- `lprnet_turkey.onnx` contract regression,
- real-model end-to-end integration.

Never fake these tests when models are absent.

### 32.4 Production quality gates

Planned:

- golden dataset regression,
- ASan,
- LSan,
- TSan,
- fuzzing,
- long-run memory test,
- static analysis,
- code coverage gate,
- performance regression gate,
- ABI compatibility gate,
- packaged artifact smoke tests.

---

## 33. Performance philosophy

Optimization priorities:

1. correctness,
2. deterministic behavior,
3. bounded resources,
4. minimize avoidable memory copies/allocations,
5. profile,
6. optimize actual hot paths.

Do not introduce unsafe micro-optimizations without measurable benefit.

Likely hot paths:

- detector preprocessing,
- tile inference,
- perspective rectification,
- OCR preprocessing,
- multi-crop OCR,
- candidate fusion.

Workspace reuse should avoid repeated per-frame allocations.

---

## 34. Security and privacy principles

Production rules:

- do not log raw images by default,
- do not log raw plate text by default,
- validate all caller-provided dimensions/strides/buffer sizes,
- perform checked integer arithmetic,
- validate model manifests/checksums,
- do not trust tensor shapes blindly,
- do not expose filesystem secrets through public error strings,
- no uncontrolled external network behavior in core engine,
- bounded queues and memory,
- fail closed on recognition uncertainty,
- exceptions terminate at ABI boundaries.

---

## 35. Production anti-patterns explicitly forbidden

Do not introduce:

- raw owning pointers,
- `new/delete` ownership scattered across pipeline code,
- exceptions crossing C ABI,
- ONNX session per frame,
- unlimited queues,
- unlimited crop hypotheses,
- silent fallback after fatal model-contract failure,
- hard-coded model tensor layout without inspected evidence,
- model-specific business rules inside public API,
- barrier/access logic,
- direct backend coupling,
- direct DB coupling,
- direct RTSP lifecycle ownership,
- fake real-model test passes,
- logging of sensitive plate/image data by default.

---

## 36. Existing implementation state

### Completed and closed issues

As of this document version:

- #1 C++20 + CMake project skeleton — closed
- #2 dependency management — closed
- #3 domain models/layer boundaries — closed
- #4 provider interfaces/SOLID extension points — closed
- #5 engine configuration model — closed

### Implemented or substantially implemented but not necessarily closed

- #6 error taxonomy and C ABI exception boundary
- #7 structured logging/callback logger/thread-safety hardening
- #8 model inspector CLI
- #9 ONNX session wrapper/lifecycle
- #10 image input validation/native image foundation
- #11 custom fused YOLO Pose preprocessing
- #12 YOLO Pose parser/NMS
- #13 geometry validator contract started
- native image crop primitives for #16
- custom quality metrics groundwork for #19

Issue closure must continue in strict numerical order and only after each acceptance criterion is genuinely met.

---

## 37. Important existing code decisions

### 37.1 Error boundary

A C ABI helper maps engine errors to stable C status codes and catches unknown exceptions.

### 37.2 Logging

Callback logger is intended to serialize callback execution so consumers do not need to be thread-safe by accident.

### 37.3 Image validation

Strided zero-copy views use the correct required extent:

```text
(height - 1) * stride + packed_row_bytes
```

rather than incorrectly requiring `stride * height` bytes after the final row.

### 37.4 YOLO preprocess

The old OpenCV-heavy preprocess path was replaced conceptually/implementation-wise with custom fused native C++.

### 37.5 MSVC portability

`std::isfinite`-based geometry predicates are runtime predicates rather than incorrectly forcing `constexpr` behavior on toolchains where it is not constexpr-compatible.

---

## 38. Current configuration model

The engine configuration includes:

### DetectorConfig

- confidence threshold
- NMS IoU threshold
- max detections
- adaptive tiling
- tile width/height
- tile overlap

### RecognitionConfig

- beam width
- result limit
- classes per step
- maximum recognizers
- minimum candidate confidence

### CropConfig

- maximum hypotheses
- horizontal/vertical padding
- minimum crop dimensions
- enable CLAHE
- enable sharpen
- enable adaptive threshold
- enable double-row processing

### DecisionConfig

- accepted confidence threshold
- minimum crop quality
- minimum effective detector confidence
- fail closed

### PerformanceConfig

- worker count
- queue capacity
- maximum image width/height
- maximum image bytes
- recognition timeout

JSON parsing is intentionally a later adapter issue; core config must not depend on JSON/filesystem.

---

## 39. Planned complete backlog

The product roadmap consists of the following issues:

1. C++20 + CMake project skeleton
2. vcpkg dependency management
3. Domain models/layer dependencies
4. Provider interfaces/SOLID extension points
5. Engine config model
6. Error taxonomy/exception boundaries
7. Structured logging/callback logger
8. Model inspector CLI
9. ONNX session wrapper/lifecycle
10. ImageView/input validation
11. YOLO Pose preprocess
12. YOLO Pose output parser/NMS
13. Geometry validator/score
14. Perspective/homography
15. Adaptive full-frame + tile detection
16. Crop hypothesis generator
17. CLAHE/sharpen/threshold enhancers
18. Double-row normalizer
19. Crop quality scoring
20. LPRNet preprocess
21. Greedy CTC decoder
22. Turkish plate grammar/normalizer
23. Turkish constrained CTC beam search
24. Connected-component layout analyzer
25. Multi-crop candidate fusion
26. Recognition ensemble/weighted evidence fusion
27. Optional PaddleOCR provider adapter
28. Generic ONNX OCR recognizer adapter
29. Confidence calibration infrastructure
30. Safe recognition decision policy
31. Pipeline orchestrator
32. Model manifest/checksum/lifecycle
33. Inference workspace/buffer reuse
34. Bounded worker pool/concurrency
35. Public C ABI v1/header/handle lifecycle
36. C ABI result buffer/ownership
37. `lpr-cli`
38. Golden dataset regression infrastructure
39. Multi-detector fusion/cross-model grouping
40. Memory leak/long-run stress
41. ASan/LSan/TSan builds
42. Performance benchmark/latency metrics
43. Static analysis/code quality gates
44. Input/config fuzz
45. GitHub Actions multi-platform CI
46. Dependency security scan/SBOM
47. Versioned release artifact packaging
48. Semantic versioning/ABI compatibility policy
49. Engine diagnostics/metrics snapshot API
50. Startup model self-test/readiness
51. `best.onnx` model contract regression
52. `lprnet_turkey.onnx` model contract regression
53. Real models end-to-end integration
54. C# P/Invoke consumer/integration
55. Python ctypes consumer/integration
56. Engine builder/provider registry/composition root
57. JSON config adapter/schema validation
58. ONNX execution provider abstraction
59. Cancellation/timeout/deadline propagation
60. Atomic model reload/safe swap
61. Deterministic inference/reproducibility
62. Native C consumer ABI smoke
63. Packaged release artifact smoke
64. Public API thread-safety/reentrancy contract
65. Public API reference/consumer docs
66. Code coverage/gate
67. Performance regression gate
68. Third-party licenses/NOTICE
69. Reproducible build/artifact provenance
70. Offline confidence calibration fit tool
71. Evaluation/report tool
72. CMake package export/C++ consumer
73. Model/test dataset artifact provisioning policy
74. Production ops/troubleshooting runbook
75. Release changelog/automated release
76. Automated ABI compatibility gate
77. Memory/resource budget/OOM guard
78. Production v1 release readiness/acceptance gate

---

## 40. Recommended implementation order from current point

Continue strict issue closure order, but keep architectural dependencies in mind.

Near-term sequence:

```text
#6 Error boundary
↓
#7 Logging
↓
#8 Model inspector
↓
#9 ONNX session
↓
#10 Native image/input boundary
↓
#11 Custom YOLO preprocess
↓
#12 Parser/NMS
↓
#13 Geometry
↓
#14 Homography
↓
#15 Full-frame + tile detection
↓
#16 Crop hypotheses using native crop primitives
↓
#17 Enhancement strategies
↓
#18 Double-row normalization
↓
#19 Custom crop quality
↓
#20+ OCR pipeline
```

Do not close an issue simply because related files exist. Verify acceptance criteria.

---

## 41. Issue closure policy

Issues are closed sequentially.

For each issue:

1. implement all acceptance requirements,
2. test/validate as far as the available environment allows,
3. do not invent successful tests,
4. add a Turkish closing comment explaining what was implemented and how it was verified,
5. close with reason `completed`,
6. proceed to the next issue.

Typical closing comment style:

```text
Tamamlandı. <implemented details>. Doğrulama: <tests/build/contract checks>.
```

---

## 42. Real-world camera assumptions

The engine does not own camera settings, but recognition design assumes real vehicles may be moving.

Previously discussed capture considerations:

- daytime shutter may need to be around 1/500–1/1000 for moving vehicles,
- 1/1000 freezes motion more strongly but requires more light,
- night capture requires balancing shutter, gain, illumination and camera capabilities,
- the wider system may dynamically use day/night exposure behavior,
- recognition should not assume every frame is sharp.

The engine should therefore make use of crop/frame quality instead of trusting any frame equally.

---

## 43. Plate alignment assumptions

The detector may see plates from oblique side angles.

Desired flow:

```text
YOLO Pose four corners
↓
validate/order corners
↓
homography
↓
rectified plate image
↓
OCR
```

Perspective alignment is expected to improve OCR compared with simply taking an axis-aligned detection box.

The four-corner ordering must remain consistent, e.g. logical top-left, top-right, bottom-right, bottom-left after normalization.

---

## 44. Plate types

The engine should eventually support:

- normal Turkish single-row plates,
- motorcycle/square plates,
- double-row layouts.

Layout analysis and double-row normalization exist in the roadmap specifically so OCR is not forced to treat every plate as a wide single line.

---

## 45. Dataset and training context

The wider LPR work has discussed:

- real plate detection/pose data,
- synthetic data augmentation,
- multiple plates in a scene must all be labeled where appropriate,
- detector labels should keep corner/keypoint order consistent,
- OCR errors under angle/blur require robust training and candidate logic,
- 10k+ real examples were discussed as a useful scale target, though final dataset size depends on diversity/quality.

This repository is the inference engine, not necessarily the training repository, but its tests/evaluation tooling must be compatible with real datasets and model revisions.

---

## 46. Release philosophy

Production v1 should not be declared ready until the final acceptance gate includes at minimum:

- supported platform builds,
- real model contract validation,
- real end-to-end inference,
- golden dataset regression,
- leak/stress testing,
- sanitizer runs,
- bounded memory behavior,
- documented ABI,
- C/C#/Python consumer smoke tests,
- packaged artifact smoke test,
- model manifest/checksum validation,
- diagnostics/readiness,
- third-party notices/SBOM,
- reproducible/versioned release artifacts,
- performance baseline and regression limits.

---

## 47. Definition of success

FAC LPR Engine is successful when it can be dropped into a production consumer as a stable native component and:

- accept caller-owned image buffers safely,
- reliably detect Turkish plates in realistic scenes,
- rectify difficult plate angles,
- recognize text using one or more OCR strategies,
- return calibrated evidence rather than false certainty,
- run for long periods without leaks/resource growth,
- remain deterministic and bounded,
- provide clear diagnostics without leaking sensitive data,
- survive model/provider replacement without redesigning the core,
- maintain a stable consumer-facing ABI,
- remain independent from FAC Access business/access logic.

The engine should be boring to operate, strict about invalid input, conservative about uncertain recognition and easy to integrate. That is the target.