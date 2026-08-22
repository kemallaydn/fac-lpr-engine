# FAC LPR Engine — Product, Architecture and AI Handoff

> **Purpose**
>
> Canonical handoff for FAC LPR Engine. A developer or AI should be able to read this document, inspect current `dev` code/tests and live GitHub issues, and continue without previous chat history.
>
> **Authority:** live GitHub issue state + current `dev` code/tests override stale text. If this file differs, verify first and update it.

---

## 1. Product definition

FAC LPR Engine is an independent, reusable, production-grade native license plate recognition engine.

```text
image/frame
→ plate detection
→ geometry validation/alignment
→ crop generation/enhancement
→ OCR recognition
→ candidate/evidence fusion
→ technical recognition decision
→ PlateRecognitionResult
```

It must not own barrier/access authorization, FAC Access business rules, Spring/backend calls, RTSP lifecycle, application database/UI state, or runtime model binaries in Git.

Technical statuses only:

```text
ACCEPTED
REVIEW
REJECTED
```

`ACCEPTED` means recognition evidence is technically strong enough, never “grant access”.

---

## 2. Repository/workflow

Repository: `kemallaydn/fac-lpr-engine`

- `main`: stable/release base
- `dev`: active development
- Draft PR #79: `FAC LPR Engine production development`, main ← dev

### Sequential issue procedure

For each issue, in numerical order:

1. read live acceptance criteria;
2. inspect current implementation;
3. implement missing pieces only;
4. add/adjust tests;
5. perform strongest truthful validation available;
6. post Turkish top-level completion comment;
7. close with `state_reason=completed`;
8. update this checkpoint at meaningful milestones;
9. continue.

Never fake CI/test/model validation and never close merely because similarly named code exists.

---

## 3. Current authoritative checkpoint

Re-audited on **2026-08-22**.

**Issues #1 through #30 are CLOSED / completed.**

Current first open sequential issue:

- **#31 — LPR pipeline orchestrator oluştur** — NEXT / OPEN

Do not skip #31 even though later foundation code may already exist.

---

## 4. Work completed during current handoff session

### #22 Turkish plate grammar

- ASCII uppercase/whitespace/hyphen normalization;
- non-ASCII rejection;
- province 01–81, reject 00/82+;
- configurable allowed letters;
- supported 1/2/3-letter civilian formats;
- beam-pruning prefix validation;
- expanded valid/invalid/boundary unit tests;
- independent C++20 warnings-as-errors grammar smoke passed.

### #23 constrained CTC beam search

- configurable beam/result/top-classes;
- Turkish prefix pruning during expansion;
- confusion map `0/O,1/I,8/B,5/S,6/G`;
- deterministic ordering;
- greedy reference/fallback;
- test helper fixed for non-terminal blank index;
- real GCC warnings-as-errors header portability bug fixed (`explicit` grammar default construction);
- combined grammar/greedy/beam smoke passed.

### #24 connected-component layout analyzer

- verified gray → CLAHE → blur → Otsu → connected-components;
- analyzer remains OCR-independent geometric evidence only;
- neutral evidence on unreliable component count/height/overlap;
- added explicit heavy-perspective height-distortion test;
- OpenCV CMake/test wiring verified;
- current execution environment lacks OpenCV dev package, therefore no fake local OpenCV GTest pass was claimed.

### #25 multi-crop candidate fusion

- discovered and fixed duplicate-margin bug: same plate text within one crop was incorrectly treated as a competing candidate;
- margin now compares only different plate texts;
- same-crop duplicate remains one max-score vote;
- cross-crop same plate uses probabilistic-OR consensus;
- bounded/deterministic ordering retained;
- independent formula smoke confirmed consistent crops can beat one extreme outlier.

### #26 recognition ensemble

- optional failure → degraded evidence while healthy providers continue;
- required failure → fatal ProviderError;
- provider weight and bounded child deadline retained;
- invalid calibrated confidence now rejected outside `[0,1]` instead of silently clamped/falling back;
- added disabled/zero-weight non-invocation tests;
- provider evidence/weight smoke passed.

### #27 optional PaddleOCR adapter

- external worker/encoder boundary retained; PaddleOCR is not a required native dependency;
- unavailable worker produces ProviderError for ensemble degradation;
- malformed calibrated confidence now rejected;
- added real LRU eviction test for bounded SHA-256 cache;
- added adapter timeout-overrun test;
- hard interruption of an infinitely blocking external worker remains later #59 cancellation/timeout scope.

### #28 generic ONNX OCR adapter

- model-specific adapter remains behind generic `IPlateRecognizer` wrapper;
- metadata/node contract validated against session descriptors;
- added session output-count validation before decode;
- invalid calibrated evidence now rejected, not silently repaired;
- added RAII lifetime test using weak_ptr expiration;
- current environment lacks provisioned ONNX Runtime dev/runtime artifact, so no fake ONNX-backed GTest pass was claimed.

### #29 confidence calibration

- identity when no/insufficient dataset segment;
- exact provider+crop context overrides provider fallback;
- runtime segment constructor path retained;
- expanded tests for min samples, epsilon, duplicate segments, input bounds and raw 0/1;
- independent logistic math smoke passed;
- no unnecessary production algorithm change was made.

### #30 safe recognition decision policy

- technical recognition only, no access/barrier business logic;
- fatal → reject, degraded → at most review;
- weak detector/no valid candidate → reject;
- weak geometry/crop/accept threshold and strong conflict → review;
- strong consistent evidence → accept;
- tests expanded to assert explicit explainable `RecognitionDecisionReason` values.

---

## 5. CI budget constraint

Hard operational rule: routine development stays **zero-spend** for GitHub-hosted Actions while account minutes are constrained.

- automatic expensive push/PR matrices must not be re-enabled without explicit approval;
- foundation/dependency workflows remain manual (`workflow_dispatch`);
- prefer local/free/self-hosted validation;
- red zero-step workflow may be runner/account allocation, not code;
- release candidate still requires full Windows/Linux validation.

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

Dependencies: vcpkg for normal C/C++ deps, pinned baseline; official checksum-pinned Microsoft ONNX Runtime prebuilts; no runtime `.onnx` binaries in Git.

---

## 7. Architecture

```text
Public API / Composition Root
            ↓
      Infrastructure
            ↓
       Application
            ↓
          Domain
```

Domain: standard C++ value types only, no vendor/framework/process/UI/backend dependencies.

Application: vendor-neutral use cases/contracts including detector, aligner, crop generator, recognizer, layout analyzer, candidate fusion, confidence calibration and decision policy interfaces; also ImageView, OperationContext, config and typed errors.

Infrastructure: concrete ONNX/OpenCV/native-image/logging/model adapters. Vendor types never leak inward or into public ABI.

---

## 8. Image/memory rules

OpenCV is not the fundamental image abstraction. Native C++ owns simple hot-path validation/crop/letterbox/sampling/color/normalization/HWC→CHW/workspace/statistics. OpenCV remains for high-value complex algorithms like homography, warpPerspective, CLAHE, thresholding and connected components.

Ownership/resource rules:

- RAII;
- no raw ownership/scattered new/delete;
- caller owns ImageView memory;
- ImageBuffer/workspace own bounded storage;
- no unbounded queues/caches/tensors/crops/images;
- checked external dimension/stride arithmetic;
- no dangling engine-owned strings through C ABI.

Strided extent:

```text
(height - 1) * stride + packed_row_bytes
```

---

## 9. Error/privacy/model rules

No C++ exception crosses C ABI. Typed errors include configuration/model-load/inference/invalid-image/provider/cancelled/timeout/resource-exhausted/internal.

Sensitive images/crops/full plate text/secrets are not logged by default.

Expected runtime models: `best.onnx`, `lprnet_turkey.onnx`.

**Never guess model contracts:** tensor names/shapes/layout/class count/keypoint order/offsets/charset/blank index/logit axes/normalization must come from inspector + real artifacts. Missing artifacts produce explicit skipped/missing validation, never fake pass.

---

## 10. Target recognition pipeline

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
Turkish constrained search
↓
Layout analysis
↓
Multi-crop fusion
↘ optional secondary recognizers
↓
Provider calibration / cross-source evidence
↓
Safe technical recognition decision
↓
PlateRecognitionResult
```

Deterministic ordering, evidence preservation, explicit degradation and fail-closed behavior are required.

---

## 11. Canonical roadmap

Roadmap is issues #1–#78. Accidental #80 is not roadmap work.

```text
#1–#30                                      CLOSED
#31 LPR pipeline orchestrator               NEXT / OPEN
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

## 12. Immediate continuation: #31 LPR pipeline orchestrator

Live issue acceptance criteria must be read before implementation.

Target responsibility is expected to compose the already-built vendor-neutral stages rather than reimplement them:

```text
detection
→ geometry/alignment
→ crop generation
→ recognition ensemble
→ layout evidence
→ candidate fusion
→ decision policy
```

Requirements from architecture:

- depend on application interfaces, not concrete provider classes;
- preserve partial-failure/degraded evidence;
- propagate operation context/deadline;
- produce per-stage/total latency metadata where issue requires it;
- return structured `PlateRecognitionResult`;
- no access-control business logic;
- no vendor-specific types leaking into the orchestrator contract.

Audit current code before creating new abstractions; do not duplicate existing provider responsibilities.

---

## 13. Production v1 guardrails

Do not call production v1 until #78 truthfully passes clean multi-platform build, unit/integration/real-model/golden tests, sanitizer/static/fuzz, memory/performance, ABI, packaged/consumer smoke, security/SBOM/licenses/provenance/checksums and readiness/runbook gates.

Never re-enable expensive hosted CI without approval, guess ONNX contracts, commit models/sensitive data, add access rules, expose exceptions, use raw ownership/unbounded resources, log sensitive plate data by default, close out of order or fabricate validation.

---

## 14. Handoff checkpoint

```text
checkpoint date: 2026-08-22
closed issues: #1 through #30
last closed issue: #30 safe recognition decision policy
next issue to audit/close: #31 LPR pipeline orchestrator
active development branch: dev
active draft PR: #79
GitHub Actions mode: manual / zero-spend
runtime ONNX models committed to Git: NO
production v1 ready: NO
```

**Resume at #31. Inspect live acceptance criteria and current code first, implement missing orchestration only, validate truthfully, comment/close sequentially.**
