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

The engine does **not** own barrier/access authorization, FAC Access business rules, backend/database/UI state, RTSP lifecycle, or model-training lifecycle.

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

Re-audited on **2026-08-23**.

**Roadmap issues #1 through #78 are CLOSED / completed.**

Current release state:

- #37 offline `lpr-cli` runtime acceptance is completed and closed.
- #78 production v1 readiness issue is completed and closed.
- PR #103 (`release: add production v1 readiness and acceptance gate`) has been merged into `dev`.
- `production-readiness.yml` now exists on `dev` and implements the fail-closed release gate.
- `main`'s standalone dependency-security registration commit has been merged into `dev`, resolving the previous branch divergence while keeping the newer `dev` security workflow content.
- PR #79 (`dev -> main`) is now mergeable and remains draft until current validation is truthfully complete.
- No roadmap issue is currently open.

Production v1 must still not be declared released merely because roadmap issues are closed. The exact release candidate must satisfy the release gate and produce machine-readable readiness evidence.

---

## 4. Recognition foundation and core implementation

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
- independent C11 warnings-as-errors header smoke covered.

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
- independent C11 wire-layout smoke covered.

---

## 5. Offline CLI and active model contracts

Optional build target:

```text
FAC_LPR_BUILD_LPR_CLI=ON
→ fac-lpr-cli
```

CLI supports:

- JPG/PNG path input via OpenCV `imgcodecs`;
- `--json`;
- `--debug-evidence`;
- `--model-dir <path>`;
- `--config <path>`;
- `--log-level trace|debug|info|warn|error`;
- real application `LprPipeline` execution;
- human and JSON result output;
- explicit process error handling.

Real CLI composition wires:

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

### Active detector contract

`best.onnx`:

```text
input  images   float32 [1,3,960,960]
output output0  float32 [1,17,18900]
```

- input scale `1/255`;
- pad `114`;
- RGB CHW tensor;
- features-first output;
- 4 bbox values;
- one plate class score;
- no separate objectness;
- 4 keypoints × `(x,y,confidence)`;
- geometry layer reorders corner points and does not assume model keypoint semantic order.

### Active OCR contract

`lprnet_turkey.onnx` active model: **V2 Mixed Epoch 7**.

```text
input  input   float32 [1,3,40,160]
output output  float32 [1,34,24]
layout: BCT (batch, classes, timesteps)
```

Exact training CHARS order:

```text
0 1 2 3 4 5 6 7 8 9 A B C D E F G H I J K L M N O P R S T U V Y Z -
```

The final `-` is not a real character. Native decoder charset is:

```text
0123456789ABCDEFGHIJKLMNOPRSTUVYZ
```

with `blank_index=33`, 34 total classes and 24 timesteps.

Training preprocessing:

```text
cv2.resize(..., (160,40), INTER_LINEAR)
keep BGR order
float32
(img - 127.5) * 0.0078125
HWC -> CHW
add batch
contiguous float32
```

Equivalent native normalization:

```text
color_order = BGR
input_scale = 1.0
mean = [127.5,127.5,127.5]
std = [128,128,128]
```

Regression tests lock BGR/normalization semantics, exact 33-character + blank-33 CTC mapping and `[1,34,24]` BCT adapter decoding.

Deprecated and forbidden for this active model:

```text
input  [1,3,24,94]
output [1,34,18]
```

---

## 6. CI / validation state

Current validation is based on the latest `dev` head and PR #79.

The repository contains dedicated workflows for:

```text
ci-pr
mac-arm64-validation
sanitizers
static-analysis
fuzz
memory-stress
performance-regression
abi-compatibility
resource-budget
release-package
cmake-package-release-smoke
dependency-security
production-readiness
release-readiness
coverage
C / C# / Python consumer smoke
```

Hosted CI can be intentionally disabled through repository variables, so a skipped hosted job is not equivalent to a successful validation. Production release policy is fail-closed: missing, skipped, cancelled or failed required release evidence must not be treated as approval.

The new `production-readiness` workflow collects exact-tag/exact-commit evidence and requires Linux/Windows clean builds, real-model/native/golden coverage, sanitizer/static/fuzz/memory/performance/ABI/package/security/documentation evidence before publishing can proceed.

Truthfulness rule: source/test wiring or independent smoke tests are not equivalent to full repository CI. Never claim a platform/gate passed until its corresponding execution evidence exists.

---

## 7. Technology and architecture baseline

- C++20
- C11 public ABI validation
- CMake 3.25+
- Windows x64 / MSVC
- Linux x64 / GCC + Clang
- macOS ARM64 self-hosted validation
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
- release policy provisions/verifies runtime model artifacts by checksum.

Strided image extent:

```text
(height - 1) * stride + packed_row_bytes
```

Expected runtime models currently include `best.onnx` and `lprnet_turkey.onnx`. Never guess tensor names/shapes/layout/class count/keypoint order/charset/blank index/normalization; use the authoritative model/training contract and regression tests.

**Current repository reality:** `dev/models` contains `best.onnx` and `lprnet_turkey.onnx`. Artifact provisioning and release evidence must remain deliberate and checksum-backed.

---

## 9. Canonical roadmap

Roadmap issues are #1–#78. Accidental #80 is not roadmap work.

```text
#1–#78                                      CLOSED
```

Major completed release-hardening areas include golden regression, detector fusion, memory stress, sanitizers, performance, static analysis, fuzzing, multi-platform CI, dependency security/SBOM, packaging, SemVer/ABI policy, diagnostics, startup readiness, model contract regression, real-model integration, C#/Python/C consumers, composition/configuration, execution providers, cancellation/deadlines, atomic model reload, deterministic inference, coverage, provenance, calibration/evaluation tools, CMake package export, artifact provisioning, production runbook, changelog/release automation, ABI compatibility and resource budgets.

---

## 10. Production v1 guardrail

Do not call production v1 released until the exact release candidate truthfully passes clean multi-platform build, unit/integration/real-model/golden tests, sanitizer/static/fuzz, memory/performance, ABI, packaged/consumer smoke, security/SBOM/licenses/provenance/checksums and readiness/runbook gates.

Closing #78 means the gate implementation is complete. It does **not** mean every future release candidate automatically passes that gate.

---

## 11. Handoff checkpoint

```text
checkpoint date: 2026-08-23
closed roadmap issues: #1 through #78
open roadmap issues: none
active development branch: dev
active release PR: #79 (dev -> main, draft, mergeable)
production readiness workflow on dev: YES
PR #103 production readiness implementation: MERGED
main-only security registration divergence: RESOLVED INTO DEV
runtime ONNX models committed to dev/models: YES
production v1 release: PENDING EXACT-CANDIDATE VALIDATION
```

**Next action:** finish current PR #79 validation, require truthful evidence for all mandatory release gates, then promote `dev` to `main` and create the first production release tag only after the release candidate is approved.
