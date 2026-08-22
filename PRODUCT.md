# FAC LPR Engine — Product, Architecture and AI Handoff

> **Purpose**
>
> This is the canonical handoff document for FAC LPR Engine. A developer or AI taking over the repository should be able to read this file, inspect current `dev` code/tests and GitHub issues, and continue without relying on chat history.
>
> **Authority rule:** current code, tests and live GitHub issue state are authoritative. If this file differs from the repository, verify the repository first and update this file.

---

## 1. Product definition

FAC LPR Engine is an independent, reusable, production-grade native license plate recognition engine.

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

The engine returns technical recognition evidence. It must not:

- open/close barriers;
- decide physical access authorization;
- contain FAC Access business rules;
- call the Spring/backend directly;
- own RTSP/camera lifecycle;
- own application DB/UI state;
- silently convert OCR confidence into access permission;
- embed runtime model binaries in Git.

Recognition statuses are technical only:

```text
ACCEPTED
REVIEW
REJECTED
```

`ACCEPTED` means recognition evidence is technically strong enough, not “grant access”.

---

## 2. Repository/workflow

Repository: `kemallaydn/fac-lpr-engine`

Branches:

- `main`: stable/release base
- `dev`: active development

Integration surface:

- Draft PR #79 — `FAC LPR Engine production development`
- base `main`, head `dev`

### Sequential issue rule

Issues must be handled numerically. For each issue:

1. read live acceptance criteria;
2. inspect existing implementation;
3. implement missing pieces only;
4. add/adjust tests;
5. perform strongest truthful validation available;
6. post a Turkish top-level completion comment;
7. close with `state_reason=completed`;
8. move to the next issue.

Never close an issue merely because similarly named code exists. Never invent test success.

---

## 3. Current authoritative checkpoint

GitHub state was re-audited on **2026-08-22**.

### Completed

**Issues #1 through #24 are closed with `state_reason=completed`.**

Most recent completions in this handoff session:

- #22 Turkish plate grammar/normalizer
- #23 Turkish constrained CTC beam search
- #24 connected-component plate layout analyzer

### Current next issue

- **#25 — Multi-crop candidate fusion oluştur** — OPEN/NEXT

Do not skip to later issues even when code already exists.

---

## 4. Recent issue-specific audit notes

### #22 Turkish grammar — completed

Verified/strengthened:

- ASCII uppercase + whitespace/hyphen normalization;
- non-ASCII rejection;
- province code 01–81, rejecting 00 and 82+;
- configurable allowed-letter set;
- supported 1/2/3-letter civilian plate families;
- prefix validation suitable for beam pruning;
- invalid ordering/group-length edge cases;
- additional unit-test coverage.

A local independent C++20 smoke harness compiled with `-Wall -Wextra -Wpedantic -Werror` and passed critical grammar scenarios.

### #23 constrained CTC beam search — completed

Verified/strengthened:

- configurable `beam_width`, `result_limit`, `classes_per_step`;
- prefix pruning calls Turkish grammar during beam expansion;
- confusion pairs `0/O`, `1/I`, `8/B`, `5/S`, `6/G`;
- deterministic sorting/tie-break behavior;
- greedy decode retained as reference/fallback;
- configurable blank index including non-terminal blank position;
- result/config boundary tests.

A real GCC warnings-as-errors portability bug was found in the public header: an `explicit TurkishPlateGrammar` constructor was used through `grammar = {}`. It was corrected to direct construction. Combined grammar + greedy CTC + beam smoke then passed under C++20 warnings-as-errors.

### #24 connected-component layout analyzer — completed

Verified:

- analyzer does not generate OCR and ignores candidate text for geometric evidence;
- gray → CLAHE → blur → Otsu → connected-components pipeline;
- component geometry filtering;
- character-count/height consistency;
- overlap sanity;
- 1/2/3-letter spacing-boundary analysis;
- bounded layout confidence;
- unreliable cases return neutral `reliable=false` evidence;
- overlap fixture exists;
- added explicit heavy-perspective/height-distortion fixture that must return neutral evidence;
- OpenCV implementation and tests are wired under `FAC_LPR_WITH_OPENCV`.

Current execution environment did not contain OpenCV development packages, so #24 OpenCV GTest binary could not be locally linked/run. This limitation was explicitly recorded rather than represented as a pass.

---

## 5. CI budget constraint

This remains a hard operational rule.

The GitHub account is under a zero-spend hosted Actions policy because included minutes were nearly exhausted.

- routine hosted Actions must not be enabled automatically;
- `foundation-build` and `dependency-restore` remain manual / `workflow_dispatch`;
- do not restore expensive push/PR matrices without explicit user approval;
- use local/free/self-hosted validation where possible;
- a workflow that fails with zero executed steps may be runner/account allocation failure, not code failure;
- full Windows/Linux matrix remains required for release readiness.

See `docs/ci-budget.md`.

---

## 6. Technology baseline

- C++20
- CMake 3.25+
- CMake Presets
- Windows x64 / MSVC
- Linux x64 / GCC + Clang
- ONNX Runtime
- OpenCV, intentionally minimized
- GoogleTest
- spdlog

Dependency strategy:

- vcpkg manifest mode for normal C/C++ dependencies;
- pinned vcpkg baseline;
- official Microsoft prebuilt ONNX Runtime artifacts with checksum pinning;
- no runtime `.onnx` binaries committed to Git.

---

## 7. Architecture

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

Standard C++ value types only. No OpenCV/ORT/Paddle/filesystem/network/UI/backend dependencies. RAII/value semantics.

### Application

Vendor-neutral use cases/contracts, including:

- `IPlateDetector`
- `IPlateAligner`
- `ICropGenerator`
- `IPlateRecognizer`
- `IPlateLayoutAnalyzer`
- `ICandidateFusion`
- `IConfidenceCalibrator`
- `IDecisionPolicy`

Also owns portable `ImageView`, `OperationContext`, config and typed errors.

### Infrastructure

Concrete implementations using ONNX Runtime, native image kernels, OpenCV where justified, model-specific adapters and logging adapters.

Vendor-specific types must not leak into Domain/Application/public ABI.

---

## 8. Image hot-path rule

OpenCV is not the engine’s fundamental image type and should not dominate simple hot-path operations.

Use custom/native C++ for:

- validation and checked stride arithmetic;
- zero-copy crop views;
- crop copy/padding/basic pixels;
- bilinear sampling and letterbox mapping;
- RGB/BGR/Gray handling;
- normalization;
- HWC → CHW direct tensor writes;
- reusable scratch/tensor workspaces;
- simple quality metrics.

Keep OpenCV for mature complex operations such as homography, `warpPerspective`, CLAHE, adaptive thresholding and connected components.

Preferred YOLO preprocessing:

```text
ImageView
→ letterbox coordinate mapping
→ bilinear sampling
→ color handling
→ normalization
→ direct reusable CHW tensor write
```

Do not regress to chains of temporary `cv::Mat`s without measured justification.

---

## 9. Ownership/resource rules

- no raw ownership;
- no scattered `new/delete` lifecycle;
- RAII by default;
- caller-owned image data remains caller-owned;
- `ImageView` is non-owning;
- `ImageBuffer` owns storage;
- reusable workspaces own bounded scratch capacity;
- no unbounded queues/caches/tensor/image/crop growth;
- externally controlled dimensions/strides require checked arithmetic;
- no dangling engine-owned `char*` through C ABI.

Strided view required extent:

```text
(height - 1) * stride + packed_row_bytes
```

---

## 10. Error/logging/privacy

No C++ exception may cross a C ABI boundary.

Typed errors include configuration, model-load, inference, invalid-image, provider, cancelled, timeout, resource-exhausted and internal errors.

Central C boundary:

```text
include/fac_lpr/c_api/error_boundary.hpp
include/fac_lpr/fac_lpr_error.h
```

Privacy defaults: do not log raw plate images, crop bytes, full plate text, secrets/tokens or unnecessary paths. Logging is best-effort and must not break recognition.

---

## 11. Model contract rule

Expected external artifacts:

- detector: `best.onnx`
- OCR: `lprnet_turkey.onnx`

**Never guess model contracts.** Do not assume tensor names, shapes, layout, class count, YOLO keypoint order/offsets, LPRNet charset/blank index/logit axes or normalization constants.

Use model inspector + real artifacts. Missing real artifacts must produce explicit skipped/artifact-missing validation, never fake pass.

---

## 12. Target pipeline

```text
Image
↓
Full-frame detection
↓
Adaptive tiles when needed
↓
Detection merge/NMS
↓
Geometry validation
↓
Alignment/crop hypotheses
↓
Primary OCR candidates
↓
Turkish grammar/constrained CTC
↓
Layout analysis
↓
Multi-crop fusion
↘ optional secondary recognizers
↓
Provider calibration
↓
Cross-source evidence fusion
↓
Recognition decision
↓
PlateRecognitionResult
```

Required behavior: deterministic ordering, preserved evidence, fail-closed defaults, strong disagreement → review, explicit degraded provider state and eventual stage latency metadata.

---

## 13. Turkish plate rules

- province `01–81`, reject `00`/`82+`;
- ASCII uppercase normalization;
- configurable legal letter set;
- supported 1/2/3-letter civilian format families;
- prefix validation for constrained decoding;
- confusion handling remains in candidate search rather than detector logic;
- double-row/square normalization remains a crop strategy.

---

## 14. Canonical roadmap

Roadmap is GitHub issues #1–#78. Accidental #80 is not product work.

```text
#1–#24                                      CLOSED
#25 Multi-crop candidate fusion             NEXT / OPEN
#26 Recognition ensemble / evidence fusion
#27 Optional PaddleOCR provider
#28 Generic ONNX OCR adapter
#29 Confidence calibration infrastructure
#30 Safe technical recognition policy
#31 Pipeline orchestrator
#32 Model manifest/checksum/lifecycle
#33 Reusable inference workspace
#34 Bounded worker pool/concurrency
#35 Public C ABI v1
#36 ABI result buffer/ownership
#37 Offline lpr-cli
#38 Golden dataset regression
#39 Multi-detector fusion
#40 Long-run memory stress
#41 ASan/LSan/TSan
#42 Performance benchmark/latencies
#43 Static analysis
#44 Fuzz testing
#45 Multi-platform CI
#46 Dependency security/SBOM
#47 Versioned binary packaging
#48 SemVer/ABI policy
#49 Diagnostics/metrics snapshot
#50 Startup self-test/readiness
#51 best.onnx contract regression
#52 lprnet_turkey.onnx contract regression
#53 Real-model end-to-end integration
#54 C# P/Invoke consumer
#55 Python ctypes consumer
#56 Engine builder/provider registry/composition root
#57 JSON config adapter/schema
#58 ONNX execution-provider abstraction
#59 Cancellation/timeout/deadline propagation
#60 Atomic model reload/safe swap
#61 Deterministic inference/reproducibility
#62 Native C ABI smoke
#63 Packaged artifact smoke
#64 Public API thread-safety/reentrancy
#65 Public consumer docs
#66 Coverage gate
#67 Performance regression gate
#68 Third-party licenses/NOTICE
#69 Reproducible build/provenance
#70 Offline confidence calibration fit tool
#71 Evaluation/report tool
#72 CMake package export/C++ consumer
#73 Model/test dataset artifact provisioning
#74 Production troubleshooting runbook
#75 Changelog/automated release
#76 Automated ABI compatibility gate
#77 Memory/resource budget/OOM guard
#78 Production v1 release readiness
```

---

## 15. Immediate continuation: issue #25

GitHub acceptance criteria:

- one abnormally high-confidence crop must not blindly override several consistent crops;
- returned candidate count is configurable/bounded;
- fusion is deterministic and unit-testable.

Scope:

- source weight;
- recognition confidence;
- crop quality;
- candidate margin;
- layout bonus;
- duplicate plate vote accumulation.

Audit existing `candidate_fusion` implementation and tests before changing code. Close only after truthful validation, then update this checkpoint to #26.

---

## 16. Production v1 definition

Do not call the project production v1 until #78 is truthfully satisfied, including Windows/Linux clean builds, unit/integration/real-model/golden tests, sanitizer/static/fuzz, memory/performance, ABI compatibility, packaged smoke, consumers, SBOM/licenses/provenance/checksums and runbook/readiness gates.

---

## 17. Guardrails

Do not:

- re-enable expensive automatic hosted CI without approval;
- guess ONNX contracts;
- commit runtime models/sensitive datasets;
- add access/barrier business logic;
- make OpenCV the Domain/Application image type;
- regress fused preprocess without evidence;
- create ONNX sessions per frame;
- add raw ownership/unbounded queues/caches/workspaces;
- expose exceptions through C ABI;
- log sensitive plate content by default;
- close issues out of order;
- claim real-model validation without models;
- fabricate tests/CI;
- treat #80 as roadmap work.

Do:

- keep this file synchronized with live GitHub state;
- treat issue acceptance criteria as executable requirements;
- write tests with implementation;
- prefer deterministic/config-driven code;
- preserve vendor independence/evidence/explainability;
- validate external sizes before allocation;
- benchmark optimizations;
- use real model inspector output when artifacts exist;
- prefer free/local/self-hosted validation while hosted budget is constrained;
- comment/close completed issues in Turkish and sequentially.

---

## 18. Handoff checkpoint

```text
checkpoint date: 2026-08-22
closed issues: #1 through #24
last closed issue: #24 connected-component plate layout analyzer
next issue to audit/close: #25 multi-crop candidate fusion
active development branch: dev
active draft PR: #79
GitHub Actions mode: manual / zero-spend
runtime ONNX models committed to Git: NO
production v1 ready: NO
```

**Resume at #25. Audit against live acceptance criteria, validate truthfully, close sequentially, then continue to #26.**
