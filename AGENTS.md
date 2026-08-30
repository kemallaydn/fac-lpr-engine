# FAC LPR Engine — AI / Maintainer Handoff Guide

Bu dosya, FAC LPR Engine reposunu devralan bir **AI coding agent veya yeni maintainer için en hızlı güvenilir giriş noktasıdır**.

Amaç eski sohbet geçmişine ihtiyaç duymadan; ürün sınırını, mimariyi, production contract'larını, mevcut implementation durumunu, CI/release yaklaşımını ve değiştirilmemesi gereken invariant'ları anlamaktır.

> Authority sırası: canlı source + public header + test/model contract + executed CI evidence gerçektir. `PRODUCT.md` canonical ürün/mimari niyetidir. Bu dosya continuity/navigation rehberidir. Stale prose, çalışan implementation'ın üstünde authority değildir.

---

## 1. Yeni bir çalışma oturumunda ilk yapılacaklar

Sırasıyla:

1. `AGENTS.md` dosyasını oku.
2. `README.md` dosyasını oku.
3. `PRODUCT.md` dosyasını oku.
4. `docs/current-state.md` dosyasını oku.
5. Yapacağın işe göre ilgili `docs/*.md` dosyalarını oku.
6. İlgili `include/`, `src/`, `tests/`, model contract ve workflow dosyalarını doğrula.
7. Canlı GitHub issue/PR durumunu yeniden sorgula.
8. İlgili exact commit/head için gerçekten çalışmış CI evidence'ını kontrol et.

Eski sohbet özeti, eski issue body veya geçmişte söylenmiş “çalışıyor” ifadesi tek başına kanıt değildir.

Doküman ile implementation çelişirse önce hangisinin stale olduğunu source/test/CI ile belirle, sonra stale tarafı aynı coherent değişiklikte düzelt.

---

## 2. Ürün nedir?

FAC LPR Engine bağımsız, yeniden kullanılabilir, C++20 tabanlı native bir **License Plate Recognition engine**'dir.

Canonical per-frame pipeline:

```text
image/frame
  -> plate detection
  -> geometry validation
  -> perspective/crop hypotheses
  -> OCR
  -> confidence/layout evidence
  -> candidate fusion
  -> technical decision
  -> ACCEPTED / REVIEW / REJECTED
```

Motor recognition component'tir; access-control application değildir.

### En kritik invariant

> `ACCEPTED` = recognition evidence teknik olarak yeterince güçlü.
>
> `ACCEPTED` ≠ araç yetkili, bariyeri aç.

FAC Access veya başka consuming product authorization kararını ayrıca verir.

---

## 3. Engine'in sahip olduğu sorumluluklar

Engine owns:

- plate detection;
- geometry evaluation / rectification;
- crop generation / enhancement;
- OCR inference;
- recognition ensemble;
- confidence/evidence collection;
- plate-layout evidence;
- within-frame candidate fusion;
- technical recognition decision;
- optional bounded cross-frame temporal consensus;
- optional recognition-level stable emission / duplicate suppression;
- stream-local recognition session state;
- model activation/verification;
- bounded native execution;
- C++ ve stable C integration surfaces;
- diagnostics/stage timing.

Engine does **not** own:

- vehicle authorization;
- barrier control;
- registered vehicle lookup;
- visitors/users/customer permissions;
- backend/database state;
- RTSP lifecycle;
- camera discovery/reconnect;
- application UI;
- payment/licensing business rules;
- model training lifecycle;
- business-level access-event cooldown/deduplication;
- consuming-product audit persistence.

Bu sınırı “kolaylık olsun” diye engine içine business rule taşıyarak bozma.

---

## 4. Mimari

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

### Domain

Vendor-independent recognition concepts/value types. ONNX Runtime, OpenCV, filesystem veya ABI wire-layout bağımlılığı taşımaz.

### Application

Recognition orchestration ve vendor-neutral policy/port'lar.

Temsilî component'ler:

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

ONNX Runtime, OpenCV, model loader/checksum verification, concrete detector/OCR adapter'ları, native workspace/runtime ayrıntıları.

### Public API / Composition Root

Stable integration surfaces ve production assembly.

Vendor tiplerini Domain/Application/C ABI içine sızdırma.

---

## 5. Production composition

Per-frame production yolunun conceptual composition'ı:

```text
best.onnx
-> OnnxSession
-> YoloPoseOnnxDetector
-> PlateGeometryEvaluatorAdapter
-> OpenCvPerspectiveAligner
-> CropHypothesisGenerator
-> lprnet_turkey.onnx / OnnxSession
-> LprNetOnnxOcrAdapter
-> GenericOnnxOcrRecognizer
-> RecognitionEnsemble
-> IdentityConfidenceCalibrator
-> ConnectedComponentPlateLayoutAnalyzer
-> WeightedMultiCropCandidateFusion
-> SafeRecognitionDecisionPolicy
-> LprPipeline
```

CLI/test için gizli ikinci inference pipeline yaratma. Production davranışı mümkün olduğunca aynı gerçek composition üzerinden doğrulanır.

---

## 6. Plate-dominant recognition hardening

Production pipeline'da önemli bir edge-case fix vardır:

- detector bbox kaynak görüntünün yaklaşık `%65` veya daha fazlasını kaplıyorsa;
- gereksiz rectification/cropping OCR evidence'ını bozabileceği için;
- full source frame primary hypothesis olarak korunur;
- normal sahnelerde raw/padded/rectified crop stratejileri kullanılmaya devam eder.

Bu davranışı hardcoded karakter düzeltmesiyle (`V -> Y` gibi) değiştirme. Fix görüntü/crop evidence seviyesindedir.

Public deterministic regression exact olarak şunları doğrular:

```text
38VU055
34VZ7387
```

Fixture'lar `tests/fixtures/public/public-manifest.tsv` üzerinden lisanslı Wikimedia kaynaklarından indirilir ve pinned SHA-1 ile doğrulanır:

```text
38_VU_055.jpg   790095caf25739f07c00c55c7a513f70e8b7ef02
34_VZ_7387.jpg  6684fe7e4a6a0e742194ec12b08c980257512858
```

Regression olduğunda expected plate text'i mevcut yanlış çıktıya göre gevşetme.

---

## 7. Stateless ve stateful recognition

### Stateless canonical path

`LprPipeline::recognize()` her çağrıyı bağımsız işler:

```text
frame -> LprPipeline -> frame result
```

Buraya hidden global history ekleme.

### Optional stateful stream path

```text
frame
  -> LprPipeline
  -> frame_result
  -> TemporalPlateConsensus
  -> StablePlateEventFilter
  -> RecognitionStreamSessionResult
```

`RecognitionStreamSession` yalnız bounded stream-local recognition state sahibidir. RTSP/network camera lifecycle sahibi değildir.

### Temporal rules

- bounded history/window;
- stale expiry;
- confidence-weighted support;
- recency weighting;
- deterministic conflict/tie behavior;
- explicit reset;
- out-of-order timestamp state'i bozamaz;
- low-confidence/rejected/review-only evidence tekrarlandı diye accepted'a terfi ettirilmez;
- degraded accepted evidence stabilize olduğunda degraded bilgisi kaybolmaz.

### Ambiguous multi-plate frame

Bir frame'de birden fazla recognition varsa `frame_result` görünür kalır ancak explicit tracking identity olmadan hepsi tek temporal plate identity içine karıştırılmaz.

---

## 8. StablePlateEventFilter sınırı

Engine-level suppression:

- stabil recognition emit et;
- aynı plakanın komşu frame tekrarlarını bounded cooldown ile azalt;
- materially different plate suppression'ı kırabilsin;
- reset/expiry destekle;
- non-sensitive emitted/suppressed metrics üret.

Consuming-product responsibility:

- aynı araç tekrar girebilir mi;
- bariyer açılır mı;
- authorization;
- audit persistence;
- customer-configured access cooldown.

İkisini birbirine karıştırma.

---

## 9. Production model contract'ları

### Detector

```text
best.onnx
input  images   float32 [1,3,960,960]
output output0  float32 [1,17,18900]
```

Temel assumptions:

- RGB CHW;
- scale `1/255`;
- letterbox pad `114`;
- features-first output;
- 4 bbox values;
- one plate-class score;
- separate objectness yok;
- 4 keypoint `(x,y,confidence)`.

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

Bunlar hint değil production contract'ıdır. Tensor name/shape/layout/preprocessing/charset/blank/keypoint semantics'i tahmin ederek değiştirme.

---

## 10. Public C ABI v1

Stable header:

```text
include/fac_lpr/fac_lpr_engine.h
```

Lifecycle:

```c
fac_lpr_engine_create_v1(...)
fac_lpr_engine_recognize_v1(...)
fac_lpr_engine_destroy_v1(...)
fac_lpr_get_last_error_v1(...)
```

Kurallar:

- opaque handle;
- C11-compatible public surface;
- C++ exception ABI dışına çıkmaz;
- explicit struct size/version;
- caller-owned flat result buffer;
- two-call required-size pattern;
- buffer-relative offsets/counts;
- range/alignment/overflow validation.

Stream/session özelliği **C ABI v1'i değiştirmez**. Downstream stream C ABI ihtiyacı doğarsa additive/versioned yeni surface tasarlanmalıdır.

---

## 11. Memory ve concurrency

- RAII ownership;
- scattered raw `new/delete` kullanma;
- queues/workspaces/result buffers bounded;
- temporal state bounded;
- external dimensions/stride/offset arithmetic allocation öncesi doğrulanır;
- worker failures isolated;
- reusable workspaces steady-state'e ulaşmalı;
- shutdown semantics explicit olmalı.

Stream recognition için ikinci bağımsız worker-pool mimarisi kurma.

---

## 12. Failure ve privacy yaklaşımı

Fail closed:

- malformed image -> safe rejection;
- unsafe resource request -> allocation öncesi failure;
- invalid model/checksum -> activation yok;
- provider/runtime failure -> explicit surface;
- invalid wire result -> C ABI'ye çıkmaz;
- C++ exception -> C boundary öncesi translate;
- degraded execution -> degraded state korunur.

Default olarak loglama:

- raw input image;
- plate crop;
- full plate text;
- secret/credential.

Tercih edilen telemetry:

- stage timing;
- provider status/failure code;
- resource telemetry;
- model identity/version/checksum;
- non-sensitive decision reason;
- emitted/suppressed counters.

---

## 13. Consumer surfaces

Desteklenen integration yüzeyleri:

- native C++;
- stable C ABI;
- C# P/Invoke;
- Python `ctypes`;
- installed/exported CMake package.

FAC Access .NET Device Service, engine'i Infrastructure katmanında stable C ABI üzerinden tüketir.

---

## 14. Build / validation baseline

- C++20;
- C11-compatible public ABI;
- CMake 3.25+;
- Ninja;
- ONNX Runtime;
- OpenCV;
- GoogleTest / CTest;
- spdlog;
- Windows x64 / MSVC;
- Linux x64 / GCC/Clang;
- macOS ARM64 self-hosted host;
- Linux x64 Docker validation/benchmark.

Representative Linux flow:

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure
```

Repo preset/script'lerini kullan. Paralel, belgelenmemiş build prosedürü icat etme.

---

## 15. CI gerçeği

Production readiness unit testten daha geniştir. Relevant gate'ler:

- unit/integration;
- temporal/session regression;
- real-model inference;
- golden regression;
- licensed public fixture exact regression;
- Linux x64 Debug/Release Docker validation;
- Windows x64 Debug/Release native validation;
- DLL clean-load;
- C/C#/Python consumer validation;
- package/ABI/security/resource checks;
- memory stress;
- performance regression.

### Performance workflow

Performance benchmark Linux x64 `linux/amd64` Docker içinde, macOS ARM64 self-hosted runner üzerinde çalışır. Historical timing bu nedenle host/load/emulation variance içerebilir.

Workflow path-aware'dir:

- runtime/performance-sensitive değişiklik -> gerçek benchmark;
- docs/test-metadata/runtime'ı etkileyemeyen CI değişikliği -> expensive benchmark skip;
- skip, runtime benchmark pass anlamına gelmez.

Sampling:

```text
warmup 10
iterations 50
repeats 2
```

Threshold'ları sırf yeşil almak için gevşetme.

> Workflow dosyasının var olması pass kanıtı değildir. Queued/skipped run da ilgili runtime gate'in pass kanıtı değildir. Exact head için executed evidence'a bak.

---

## 16. Branch ve release modeli

```text
feature/fix
  -> dev
  -> applicable validation
  -> validated candidate
  -> main
```

- `dev`: aktif development/release-candidate.
- `main`: stable/release.

Runtime/code değişikliği applicable validation olmadan promote edilmez.

Dokümantasyon-only değişikliklerde runtime CI tekrarı zorunlu değildir; fakat `main...dev` diff'i incelenerek yalnız beklenen prose/Markdown değişikliklerinin promote edildiği doğrulanmalıdır.

---

## 17. Current state

Canlı operasyonel snapshot için **`docs/current-state.md`** oku.

Bu dosyadaki tarihsel sayılar zamanla eskir. Her yeni oturumda issue/PR/branch/CI state'i canlı GitHub'dan yeniden doğrula.

En son production-hardening çizgisinde public exact fixture regression şu plakaları kapsar:

```text
38VU055
34VZ7387
```

FAC LPR Engine software backlog'u son doğrulanan durumda boş olabilir; bunu varsayma, canlı ara.

FAC Access tarafındaki real-camera soak/E2E ve ONVIF hardware validation gibi fiziksel kapılar engine software defect'i değildir ve gerçek hardware evidence olmadan kapatılmamalıdır.

---

## 18. AI agent çalışma protokolü

Bir task aldığında:

1. Task'ın product boundary içinde olup olmadığını belirle.
2. İlgili source/header/test/docs'u oku.
3. Live issue/PR/CI state'i doğrula.
4. Mevcut abstraction varsa yeniden kullan; duplicate pipeline/class üretme.
5. Public contract veya model contract değişiyorsa compatibility etkisini açıkça değerlendir.
6. Test beklentisini implementation bug'ına göre gevşetme.
7. Runtime değişikliğini applicable evidence olmadan “done” ilan etme.
8. Physical hardware gate'i simülasyon/repo-only evidence ile kapatma.
9. Değişiklik bittikten sonra stale docs/current-state bilgisini senkronize et.
10. Promotion öncesi exact `dev` head ve `main...dev` delta'yı yeniden doğrula.

### Yasak kestirmeler

- plate-specific hardcoded OCR correction;
- business authorization'ı engine'e taşımak;
- C ABI v1 wire layout'ını sessizce değiştirmek;
- unbounded temporal/cache/queue state;
- CI threshold/warning/analyzer'ı sadece green almak için gevşetmek;
- fake hardware evidence;
- stale chat context'i source/test/CI'ın üstünde authority kabul etmek.

---

## 19. Dokümantasyon haritası

- `README.md`: Türkçe insan odaklı giriş, build, validation ve genel mimari.
- `PRODUCT.md`: canonical product/architecture/model/ABI/release specification.
- `AGENTS.md`: bu dosya; AI/maintainer continuity ve çalışma protokolü.
- `docs/current-state.md`: güncel operational snapshot.
- `docs/architecture.md`: dependency/mimari özeti.
- `docs/c-api-v1.md`: public C ABI contract.
- `docs/abi-versioning.md`: ABI evolution kuralları.
- `docs/stream-recognition-api.md`: stream API/lifecycle.
- `docs/temporal-stream-recognition.md`: temporal semantics.
- `docs/ci.md`: CI topology ve gate semantics.
- `docs/ci-budget.md`: runner/maliyet yaklaşımı.
- `docs/lpr-cli.md`: production CLI.

Repository'yi devralan bir agent için hedef şudur: **beş dakika içinde neyin nerede olduğunu, hangi sınırların dokunulmaz olduğunu ve hangi kanıt olmadan “bitti” denemeyeceğini anlamak.**
