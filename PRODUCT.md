# FAC LPR Engine — Product, Architecture and AI Handoff

> Canonical handoff for FAC LPR Engine. Live GitHub issue state + current `dev` code/tests override stale text.

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

The engine does **not** own barrier/access authorization, FAC Access business rules, backend/database/UI state, RTSP lifecycle, or runtime model binaries in Git.

Technical statuses only:

```text
ACCEPTED
REVIEW
REJECTED
```

`ACCEPTED` means recognition evidence is technically strong enough, never “grant access”.

---

## 2. Repository and workflow

Repository: `kemallaydn/fac-lpr-engine`

- `main`: stable/release base
- `dev`: active development
- Draft PR #79: `FAC LPR Engine production development`, main ← dev

Sequential issue procedure:

1. read live acceptance criteria;
2. inspect current implementation;
3. implement missing pieces only;
4. add/adjust tests;
5. perform strongest truthful validation available;
6. post Turkish top-level completion comment;
7. close only when acceptance is truthfully met;
8. update this checkpoint at meaningful milestones;
9. continue numerically.

Never fake CI/test/model validation and never close merely because similarly named code exists.

---

## 3. Current authoritative checkpoint

Re-audited on **2026-08-22**.

**Issues #1 through #36 are CLOSED / completed.**

Current first open sequential issue:

- **#37 — lpr-cli offline recognition aracı oluştur — OPEN / PARTIALLY IMPLEMENTED / BLOCKED ON REAL PROVIDER COMPOSITION**

#37 acceptance requires real `JPG/PNG -> PlateRecognitionResult`. The CLI shell now exists, but current `fac_lpr_engine_create_v1()` intentionally creates a lifecycle shell without a production pipeline. Repository currently has YOLO preprocess/parser and ONNX primitives, but no production concrete `IPlateDetector` composition. Do not close #37 until real provider composition produces a truthful result.

---

## 4. Key completed work in the current handoff

### #22–#30 recognition foundation

- Turkish plate grammar and constrained CTC beam search hardened.
- GCC warnings-as-errors portability bug fixed.
- Connected-component layout perspective edge test added.
- Multi-crop duplicate-margin fusion bug fixed.
- Recognition ensemble optional/required failure semantics hardened.
- PaddleOCR cache/timeout/malformed-evidence coverage improved.
- Generic ONNX OCR output-count/RAII/evidence validation improved.
- Confidence calibration boundary coverage expanded.
- Safe decision policy reason-level tests added.

### #31 LPR pipeline orchestrator

Vendor-neutral pipeline composed as:

```text
IPlateDetector
→ IPlateGeometryEvaluator
→ IPlateAligner
→ ICropGenerator
→ RecognitionEnsemble
→ IConfidenceCalibrator
→ IPlateLayoutAnalyzer
→ ICandidateFusion
→ IDecisionPolicy
```

`LprPipelineResult` preserves recognitions, stage timings, degraded state and provider failures. Geometry evidence is carried through a vendor-neutral application contract rather than leaking Infrastructure concrete classes inward.

### #32 model manifest/checksum/lifecycle

- `ModelManifest` / `ModelManifestEntry` / `ActiveModelInfo` added.
- model name/type/version/path/size/SHA-256 required;
- exact size + bounded read + SHA-256 verification;
- absolute path and `..` traversal rejected;
- canonical root containment prevents symlink/root escape;
- duplicate model identity rejected;
- activation is all-or-nothing;
- diagnostics-ready verified metadata produced.

### #33 reusable inference workspace

`NativeImageWorkspace` is bounded, RAII and move-only.

Telemetry:

```text
tensor_capacity
scratch_capacity
tensor_growth_count
scratch_growth_count
```

Limits prevent unbounded tensor/scratch growth. YOLO and LPRNet preprocessors expose workspace-based reuse paths. Generic ONNX OCR passes a reusable workspace through model input construction. Optional `FAC_LPR_BUILD_WORKSPACE_PROBE` measures warm-up/growth reuse behavior.

### #34 bounded worker pool / concurrency

- configurable worker count;
- bounded queue;
- `reject_newest` and blocking backpressure;
- `drain` / `discard_pending` shutdown;
- one reusable workspace per worker;
- task exceptions do not kill worker threads;
- submitted/completed/failed/dropped/pending/active/peak-pending telemetry;
- stress test covers 2000 tasks, queue bound and per-worker workspace count.

The stress test is wired into the native test target but has not yet been executed by hosted CI because Actions included minutes are exhausted.

### #35 Public C ABI v1

Public pure-C header:

```text
include/fac_lpr/fac_lpr_engine.h
```

Stable symbols:

```c
fac_lpr_engine_create_v1
fac_lpr_engine_recognize_v1
fac_lpr_engine_destroy_v1
fac_lpr_get_last_error_v1
```

Properties:

- opaque handle;
- export/calling convention macros;
- pointer-to-handle destroy clears caller slot;
- null/repeated destroy safe;
- no C++ exception crosses ABI;
- struct size/version contract documented;
- independent C11 warnings-as-errors header smoke passed.

### #36 C ABI result buffer / ownership

Recognition output is one **caller-owned flat byte buffer**. No engine-owned `char*`, candidate pointer or evidence pointer crosses ABI.

Layout families:

```text
fac_lpr_result_v1
fac_lpr_plate_result_v1[]
fac_lpr_evidence_v1[]
fac_lpr_candidate_v1[]
decision reason values
UTF-8/ASCII text slices
```

Nested values use buffer-relative offsets/counts. Text uses `fac_lpr_text_ref_v1 { offset, length }` and is not NUL-terminated.

- `FAC_LPR_STATUS_BUFFER_TOO_SMALL = 10`;
- two-call exact required-size pattern;
- 4-byte result-buffer alignment contract;
- explicit internal→C status/reason mapping;
- 32-bit wire overflow checks;
- finite/probability/latency validation;
- caller-owned last-error copy API;
- wire struct sizes locked with C11 `_Static_assert`;
- synthetic serializer tests cover nested result/evidence/alternatives/reasons, exact buffer, one-byte-short, empty result and misalignment;
- independent C11 wire-layout smoke passed.

---

## 5. #37 current partial implementation

Optional build target:

```text
FAC_LPR_BUILD_LPR_CLI=ON
→ fac-lpr-cli
```

Current shell supports:

- JPG/PNG path input via OpenCV `imgcodecs`;
- `--json`;
- `--debug-evidence`;
- `--model-dir <path>` surface;
- `--config <path>` surface;
- `--log-level trace|debug|info|warn|error|off`;
- public C ABI create/recognize/destroy flow;
- two-call caller-owned result buffer;
- human and JSON result decoding;
- explicit process error codes and C ABI last-error printing.

**Blocker:** model/config options cannot yet construct the production detector/recognizer pipeline because concrete provider composition is missing. The CLI must not fabricate recognition. Keep #37 open until a real image with real runtime model composition produces `PlateRecognitionResult`.

---

## 6. CI / validation state

GitHub Pro included Actions usage for the current month is exhausted:

```text
3000 / 3000 included minutes used
billable usage observed: $0 at checkpoint
```

Do not trigger expensive GitHub-hosted workflows until included usage resets or explicit approval is given.

A manual self-hosted validation workflow was prepared for Windows/Linux. Full release validation still requires:

```text
Windows x64
Linux x64
Debug + Release
OpenCV ON
ONNX Runtime ON
GTest/CTest
```

ARM64 portability can be added when an actual ARM64 deployment target/runner is available.

Truthfulness rule: source/test wiring or independent smoke tests are not equivalent to full repository CI. Never claim full GTest/OpenCV/ONNX PASS until those binaries actually run.

---

## 7. Technology and architecture baseline

- C++20
- C11 public ABI validation
- CMake 3.25+
- Windows x64 / MSVC
- Linux x64 / GCC + Clang
- ONNX Runtime
- OpenCV kept at infrastructure/tool edges
- GoogleTest
- spdlog

Dependency direction:

```text
Public API / Composition Root
            ↓
      Infrastructure
            ↓
       Application
            ↓
          Domain
```

Domain uses standard C++ value types only. Application owns vendor-neutral contracts/use cases. Infrastructure owns ONNX/OpenCV/native-image/model/concurrency adapters. Vendor types never leak into Domain/Application/public C ABI.

---

## 8. Memory, error and privacy guardrails

- RAII; no scattered raw ownership/new/delete;
- caller owns `ImageView` input memory;
- bounded workspaces/queues/caches;
- checked external dimensions/stride/offset arithmetic;
- no C++ exception across C ABI;
- no engine-owned result strings across C ABI;
- sensitive images/crops/full plate text/secrets are not logged by default;
- runtime `.onnx` models are not committed to Git.

Strided image extent:

```text
(height - 1) * stride + packed_row_bytes
```

Expected runtime models currently include `best.onnx` and `lprnet_turkey.onnx`. Never guess tensor names/shapes/layout/class count/keypoint order/charset/blank index/normalization; inspect real artifacts.

---

## 9. Canonical roadmap

Roadmap issues are #1–#78. Accidental #80 is not roadmap work.

```text
#1–#36                                      CLOSED
#37 Offline lpr-cli                         OPEN / BLOCKED ON REAL COMPOSITION
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

## 10. Production v1 guardrail

Do not call production v1 ready until #78 truthfully passes clean multi-platform build, unit/integration/real-model/golden tests, sanitizer/static/fuzz, memory/performance, ABI, packaged/consumer smoke, security/SBOM/licenses/provenance/checksums and readiness/runbook gates.

---

## 11. Handoff checkpoint

```text
checkpoint date: 2026-08-22
closed issues: #1 through #36
last closed issue: #36 C ABI result buffer/ownership
current issue: #37 offline lpr-cli
#37 state: OPEN / partial CLI shell implemented / real provider composition missing
active development branch: dev
active draft PR: #79
GitHub-hosted Actions included minutes: exhausted for current period
runtime ONNX models committed to Git: NO
production v1 ready: NO
```

**Resume at #37. Do not close it until a real JPG/PNG produces a real `PlateRecognitionResult` through actual engine/provider composition.**
