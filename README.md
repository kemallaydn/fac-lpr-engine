<div align="center">

# 🚘 FAC LPR Engine

### Türkiye plakaları için production-ready, gömülebilir plaka tanıma motoru

**C++20 · ONNX Runtime · OpenCV · CMake · Stable C ABI**

**Görüntüyü ver. Plakayı bulsun, düzeltsin, okusun ve sonucun ne kadar güvenilir olduğunu söylesin.**

</div>

---

## FAC LPR Engine nedir?

FAC LPR Engine, kamera görüntüsü veya tekil bir görsel içerisindeki araç plakasını tespit etmek ve okumak için geliştirilmiş bağımsız bir **License Plate Recognition (LPR)** motorudur.

Motor yalnızca OCR yapan ince bir wrapper değildir. Detection, geometri, perspective correction, crop üretimi, OCR, confidence/layout evidence, candidate fusion ve teknik karar aşamalarını tek bir production pipeline içerisinde yönetir.

Tek kare için canonical akış:

```text
Görüntü / Frame
      ↓
Plate Detection
      ↓
Geometry Validation
      ↓
Perspective Alignment
      ↓
Crop / Enhancement
      ↓
OCR
      ↓
Confidence + Layout Evidence
      ↓
Candidate Fusion
      ↓
Technical Decision
      ↓
ACCEPTED / REVIEW / REJECTED
```

Motorun çıktısı yalnızca `34ABC123` gibi bir metin değildir. Sonuç; plate text, confidence, evidence, degraded state, decision reason ve stage timing gibi teknik bilgileri taşıyabilir.

---

## Stateless ve stateful kullanım

FAC LPR Engine iki farklı ama birbirini bozmayan kullanım biçimine sahiptir.

### Stateless per-frame recognition

`LprPipeline::recognize()` her çağrıyı bağımsız işler. Bu mevcut ve temel davranıştır.

```text
frame -> LprPipeline -> PlateRecognitionResult
```

Tek görsel, API isteği veya birbirinden bağımsız frame senaryolarında bu yol kullanılır.

### Optional stateful stream recognition

Video/kamera akışlarında art arda gelen frame'ler aynı plakaya ait olabilir. Bunun için Application katmanında optional stateful stream desteği vardır:

```text
frame
  ↓
LprPipeline
  ↓
per-frame result
  ↓
TemporalPlateConsensus
  ↓
StablePlateEventFilter
  ↓
RecognitionStreamSession
```

`RecognitionStreamSession`:

- mevcut `LprPipeline`ı yeniden kullanır;
- per-frame recognition davranışını değiştirmez;
- session başına bounded temporal history tutar;
- confidence ve recency tabanlı multi-frame consensus uygular;
- aynı stabil plakanın art arda gereksiz tekrar emit edilmesini bounded cooldown ile azaltır;
- farklı session'ların state'ini birbirinden izole eder;
- reset/close lifecycle sağlar;
- birden fazla plaka içeren ambiguous frame'i tek bir temporal identity içine karıştırmaz.

Bu katman **FAC Access business deduplication değildir**. Engine yalnızca recognition-level tekrarları yönetir. “Aynı araç tekrar kapı açabilir mi?” gibi kurallar consuming product'a aittir.

Detay: `docs/stream-recognition-api.md` ve `docs/temporal-stream-recognition.md`.

---

## Ne işe yarar?

FAC LPR Engine, plaka tanımaya ihtiyaç duyan başka uygulamaların içine gömülmek üzere tasarlanmıştır.

Örnek kullanım alanları:

- otopark ve bariyer sistemleri
- site / plaza araç giriş sistemleri
- fabrika ve tesis girişleri
- güvenlik uygulamaları
- araç takip sistemleri
- edge cihazları
- masaüstü uygulamalar
- backend veya servis tabanlı LPR çözümleri

Engine'in görevi:

> **“Görüntüde hangi plaka var ve bu tanımaya teknik olarak ne kadar güvenebilirim?”**

Engine'in görevi değildir:

> “Bu araç içeri girebilir mi?”

`ACCEPTED`, yalnızca recognition evidence'ın teknik olarak yeterince güçlü olduğunu söyler. Access authorization ayrı bir business decision'dır.

---

## Tanıma kararları

| Karar | Anlamı |
| --- | --- |
| **ACCEPTED** | Plaka tanıma evidence'ı teknik olarak yeterince güçlü. |
| **REVIEW** | Sonuç mevcut ancak belirsizlik nedeniyle ek kontrol gerekebilir. |
| **REJECTED** | Güvenilir bir plaka sonucu üretilemedi. |

Temporal consensus da bu semantiği korur. Zayıf veya `REVIEW` sonuçları sırf çok tekrarlandı diye sessizce `ACCEPTED` yapılmaz.

---

## Öne çıkan özellikler

### 🇹🇷 Türkiye plakalarına özel pipeline

Production OCR modeli Türkiye plaka karakter seti ve plaka yapısı dikkate alınarak çalışır.

### 🎯 Detection + keypoint tabanlı geometri

Plaka yalnızca bounding-box olarak bulunmaz. Detector/keypoint çıktıları perspective correction ve crop üretim aşamalarında kullanılır.

### 🔍 Multi-crop ve candidate fusion

Tek bir crop ve tek OCR sonucuna bağımlı kalmak yerine aynı frame içindeki farklı aday/evidence birleştirilir.

### 🧭 Multi-frame temporal consensus

Stateful stream modunda birbirini takip eden frame'lerden gelen sonuçlar bounded history içinde değerlendirilir. Bu, **within-frame candidate fusion'dan ayrı bir katmandır**.

### 🔁 Stable recognition emission

Aynı stabil plakanın komşu frame'lerde tekrar tekrar emit edilmesi session-local bounded suppression ile azaltılabilir. Business-level event cooldown consuming product'ta kalır.

### 📊 Confidence ve evidence tabanlı karar

Engine yalnızca plate text üretmez. Detection, geometry, OCR, layout ve candidate evidence birlikte değerlendirilir.

### 🔒 Model integrity ve runtime contract kontrolü

Bir `.onnx` dosyasının yüklenebilmesi production için yeterli kabul edilmez. Tensor isimleri, shape, preprocessing, charset, blank semantics, keypoint davranışı ve SHA-256 bütünlüğü explicit contract olarak doğrulanır.

### 🧠 Kontrollü native kaynak kullanımı

Queue, workspace, buffer ve temporal history davranışları bounded olacak şekilde tasarlanmıştır. Native ownership RAII ile yönetilir.

### 🛡️ Fail-closed yaklaşımı

Engine belirsiz veya bozuk veriyi iyimser şekilde kabul etmek yerine `REVIEW`, `REJECTED` veya explicit error/degraded state üretir.

### 🔌 Uygulamadan bağımsız entegrasyon

- **C++**
- **C / Stable C ABI v1**
- **C# / P/Invoke**
- **Python / ctypes**
- diğer FFI destekleyen runtime'lar

Stateful stream session şu an C++ Application API olarak sunulur. **C ABI v1 değiştirilmemiştir.** Future stream C ABI ancak gerçek downstream ihtiyaç oluşursa additive/versioned olarak tasarlanacaktır.

---

## Teknoloji

| Alan | Teknoloji |
| --- | --- |
| Ana dil | **C++20** |
| Inference | **ONNX Runtime** |
| Görüntü işleme | **OpenCV** |
| Build sistemi | **CMake 3.25+ / Ninja** |
| Test | **GoogleTest / CTest** |
| Logging | **spdlog** |
| Public native interface | **C11-compatible C ABI v1** |
| Ana hedef | **Windows x64 / Linux x64** |
| Ek validation | **macOS ARM64 host + Linux x64 Docker** |

---

## Production modelleri

```text
models/
├── best.onnx             # Plaka detector + keypoints
└── lprnet_turkey.onnx    # Türkiye plaka OCR modeli
```

OCR runtime contract:

```text
input   : float32 [1,3,40,160]
output  : float32 [1,34,24]
layout  : BCT
blank   : 33
charset : 0123456789ABCDEFGHIJKLMNOPRSTUVYZ
```

Bu değerler implementation detayı değil, production model contract'ının parçasıdır.

---

# Hızlı başlangıç

## Gereksinimler

```text
C++20 uyumlu compiler
CMake 3.25+
Ninja
ONNX Runtime
OpenCV
```

## Linux

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure
```

Release:

```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release --output-on-failure
```

## Windows

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
cmake --build --preset windows-msvc-release
```

Repository gerçek Windows x64 self-hosted validation akışına sahiptir.

---

# CLI ile plaka okutmak

Optional build flag:

```text
FAC_LPR_BUILD_LPR_CLI=ON
```

Örnek:

```bash
fac-lpr-cli plate.jpg \
  --model-dir ./models \
  --config ./docs/lpr-cli-contract.example \
  --json
```

CLI gerçek production pipeline'ını kullanır; ikinci bir recognition implementation değildir.

---

# Stable C ABI

Ana header:

```text
include/fac_lpr/fac_lpr_engine.h
```

Temel lifecycle:

```c
fac_lpr_engine_create_v1(...);
fac_lpr_engine_recognize_v1(...);
fac_lpr_engine_destroy_v1(...);
fac_lpr_get_last_error_v1(...);
```

C ABI tasarımında:

- opaque engine handle
- C++ exception ABI dışına çıkmaz
- caller-owned flat result buffer
- required-size için two-call pattern
- explicit version/size contract
- nested veriler buffer-relative offset/count
- ABI compatibility regression validation

Temporal/stream özelliği mevcut v1 wire layout'a eklenmemiştir. Bu bilinçli bir compatibility kararıdır.

---

# Mimari

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

Vendor bağımsız recognition kavramları ve value type'ları.

### Application

Orchestration ve vendor-neutral policies. Burada hem stateless `LprPipeline` hem de optional stateful `TemporalPlateConsensus`, `StablePlateEventFilter` ve `RecognitionStreamSession` bulunur.

### Infrastructure

ONNX Runtime, OpenCV, model loader, concrete detector/OCR adapter'ları ve native runtime ayrıntıları.

### Public API / Composition Root

Stable integration surfaces ve production composition.

> ONNX Runtime/OpenCV vendor tipleri Domain, Application veya C ABI içine sızmamalıdır.

---

# Repository yapısı

```text
fac-lpr-engine/
├── include/          Public C++ ve C ABI header'ları
├── src/              Domain / Application / Infrastructure
├── tests/            Unit, integration, temporal ve regression testleri
├── tools/            CLI, benchmark, evaluation, resource araçları
├── samples/          Consumer entegrasyon örnekleri
├── cmake/            Package/export yardımcıları
├── scripts/          Bootstrap/validation/release scriptleri
├── docs/             Teknik ve operasyonel dokümantasyon
├── models/           Runtime model artifact'ları
├── README.md         İnsan odaklı giriş
├── PRODUCT.md        Canonical ürün + mimari specification
└── AGENTS.md         AI/maintainer proje rehberi
```

---

# Test ve production validation

Validation yalnızca unit test değildir. Repository şu sınıflarda evidence üretir:

- unit/integration tests
- temporal consensus/session regression tests
- real-model inference
- golden regression
- multi-frame temporal benchmark
- Linux x64 Debug/Release Docker validation
- Windows x64 native validation
- C ABI / C# / Python consumer validation
- memory/resource stress
- performance regression
- ABI/package/security/release checks

Windows standalone benchmark runtime packaging'i ONNX Runtime DLL'i ile doğrulanır; Linux performance workflow'u mevcut FAC-LPR self-hosted runner üzerinde `linux/amd64` Docker kullanır.

---

# Temporal regression yaklaşımı

Temporal davranış iki seviyede test edilir:

1. **Synthetic deterministic sequences**: OCR jitter, conflict, review-only, duplicate suppression ve vehicle transition gibi semantik davranışları doğrudan test eder.
2. **Real-model temporal sequence smoke**: gerçek production pipeline üzerinden per-frame correctness, false-stable ve bounded history gibi metrikleri doğrular.

Real-model fixture'da per-frame sonuçlar `REVIEW` kalıyorsa temporal katman bunları zorla `ACCEPTED` yapmaz. Bu fail-closed davranış tasarımın parçasıdır.

---

# Proje sınırı

FAC LPR Engine şunları **yapmaz**:

- RTSP bağlantı yönetimi
- kamera discovery/reconnect
- araç yetkilendirme
- bariyer açma kararı
- registered vehicle lookup
- customer/business event cooldown
- audit persistence
- UI/backend business state

Bunlar consuming product'a aittir.

---

# Dokümantasyon kaynakları

- `README.md` — hızlı giriş, kullanım ve genel mimari
- `PRODUCT.md` — canonical ürün/mimari/runtime contract
- `AGENTS.md` — AI ve maintainer çalışma kuralları
- `docs/stream-recognition-api.md` — stateful stream API ve lifecycle
- `docs/temporal-stream-recognition.md` — temporal consensus/emission semantiği
- GitHub issues/PR/CI — implementation history ve executed evidence

Kod/test ile prose çelişirse önce gerçek implementation ve executed CI doğrulanmalı, sonra stale doküman güncellenmelidir.
