<div align="center">

# 🚘 FAC LPR Engine

### Türkiye plakaları için production-ready, gömülebilir plaka tanıma motoru

**C++20 · ONNX Runtime · OpenCV · CMake · Stable C ABI**

![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus)
![CMake](https://img.shields.io/badge/CMake-3.25%2B-064F8C?style=flat-square&logo=cmake)
![ONNX Runtime](https://img.shields.io/badge/ONNX-Runtime-005CED?style=flat-square&logo=onnx)
![OpenCV](https://img.shields.io/badge/OpenCV-4.x-5C3EE8?style=flat-square&logo=opencv)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey?style=flat-square)

**Görüntüyü ver. Plakayı bulsun, düzeltsin, okusun ve sonucun ne kadar güvenilir olduğunu söylesin.**

</div>

---

## FAC LPR Engine nedir?

FAC LPR Engine, kamera görüntüsü veya tekil bir görsel içerisindeki araç plakasını tespit etmek ve okumak için geliştirilmiş bağımsız bir **License Plate Recognition (LPR)** motorudur.

Motor yalnızca OCR yapan ince bir wrapper değildir. Görüntünün alınmasından nihai teknik karara kadar plaka tanıma sürecinin tamamını kendi pipeline'ı içerisinde yönetir.

```text
Görüntü / Frame
      │
      ▼
┌─────────────────────┐
│   Plaka Detection   │
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ Geometry Validation │
│ + Perspective Fix   │
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ Crop / Enhancement  │
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│         OCR         │
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│ Confidence + Layout │
│      Evidence       │
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│  Candidate Fusion   │
└──────────┬──────────┘
           ▼
     Teknik Karar

 ACCEPTED / REVIEW / REJECTED
```

Motorun çıktısı yalnızca `34ABC123` gibi bir metin değildir. Sonuçla birlikte confidence, teknik evidence ve tanımanın kullanılabilir olup olmadığına ilişkin açık bir karar üretir.

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

Engine'in görevi şudur:

> **“Görüntüde hangi plaka var ve bu tanımaya teknik olarak ne kadar güvenebilirim?”**

Engine'in görevi **şu değildir:**

> “Bu araç içeri girebilir mi?”

Örneğin FAC Access, engine'den `ACCEPTED` sonucu alabilir ancak araç yetkili değilse giriş kararını yine `DENIED` verebilir.

Bu ayrım bilinçlidir. FAC LPR Engine herhangi bir access-control veya müşteri business rule'una bağlı değildir.

---

## Tanıma kararları

Engine sonuçları üç temel teknik karar seviyesinden biriyle döner:

| Karar | Anlamı |
| --- | --- |
| **ACCEPTED** | Plaka tanıma evidence'ı teknik olarak yeterince güçlü. |
| **REVIEW** | Sonuç mevcut ancak belirsizlik nedeniyle ek kontrol gerekebilir. |
| **REJECTED** | Güvenilir bir plaka sonucu üretilemedi. |

> `ACCEPTED`, **“bariyeri aç”** anlamına gelmez. Yalnızca plakanın teknik olarak yeterli güvenle tanındığını ifade eder.

---

## Öne çıkan özellikler

### 🇹🇷 Türkiye plakalarına özel pipeline

Production OCR modeli Türkiye plaka karakter seti ve plaka yapısı dikkate alınarak çalışır. Layout evidence ve candidate değerlendirme mekanizmaları OCR çıktısını tek başına körü körüne kabul etmez.

### 🎯 Detection + keypoint tabanlı geometri

Plaka yalnızca bounding-box olarak bulunmaz. Detector/keypoint çıktıları perspective correction ve crop üretim aşamalarında kullanılır.

### 🔍 Multi-crop ve candidate fusion

Tek bir crop ve tek OCR sonucuna bağımlı kalmak yerine farklı adaylardan gelen evidence birleştirilebilir.

### 📊 Confidence ve evidence tabanlı karar

Engine yalnızca plaka metni üretmez. Tanımanın teknik güvenilirliğini değerlendiren decision katmanına sahiptir.

### 🔒 Model integrity ve runtime contract kontrolü

Bir `.onnx` dosyasının yüklenebilmesi production için yeterli kabul edilmez.

Engine model tarafında şu sözleşmeleri doğrular:

- tensor isimleri
- input / output boyutları
- preprocessing beklentileri
- class sırası
- charset
- blank semantics
- detector keypoint davranışı
- SHA-256 model bütünlüğü

Contract uyuşmazlığı durumunda model sessizce kullanılmaz.

### 🧠 Kontrollü native kaynak kullanımı

Queue, workspace ve buffer davranışları bounded olacak şekilde tasarlanmıştır. Native ownership RAII prensipleriyle yönetilir.

### 🛡️ Fail-closed yaklaşımı

Engine belirsiz veya bozuk veriyi iyimser şekilde kabul etmek yerine güvenli tarafta kalacak şekilde tasarlanmıştır.

Malformed input, model contract problemi veya yetersiz evidence gibi durumlar açık şekilde reddedilir veya degraded olarak raporlanır.

### 🔌 Uygulamadan bağımsız entegrasyon

Engine şu ortamlardan kullanılabilir:

- **C++**
- **C / Stable C ABI v1**
- **C# / P/Invoke**
- **Python / ctypes**
- diğer FFI destekleyen runtime'lar

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
| Ek validation | **macOS ARM64** |

---

## Production modelleri

Engine'in production pipeline'ı şu temel modelleri kullanır:

```text
models/
├── best.onnx             # Plaka detector + keypoints
└── lprnet_turkey.onnx    # Türkiye plaka OCR modeli
```

Mevcut OCR runtime contract'ı:

```text
input   : float32 [1,3,40,160]
output  : float32 [1,34,24]
layout  : BCT
blank   : 33
charset : 0123456789ABCDEFGHIJKLMNOPRSTUVYZ
```

Bu değerler implementation detayı gibi görülmemelidir. Production model contract'ının parçasıdır ve regression testleriyle korunur.

---

# Hızlı başlangıç

## 1. Gereksinimler

Temel geliştirme ortamında şunlara ihtiyaç vardır:

```text
C++20 uyumlu compiler
CMake 3.25+
Ninja
ONNX Runtime
OpenCV
```

Repository içerisindeki bootstrap ve validation scriptleri desteklenen ortamlarda bağımlılıkların hazırlanmasına yardımcı olur.

---

## 2. Repository'yi klonla

```bash
git clone https://github.com/kemallaydn/fac-lpr-engine.git
cd fac-lpr-engine
```

---

## 3. Linux üzerinde build

### GCC / Debug

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure
```

### GCC / Release

```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release --output-on-failure
```

### Clang

```bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug --output-on-failure
```

---

## 4. Windows üzerinde build

MSVC toolchain ile:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
cmake --build --preset windows-msvc-release
```

Repository ayrıca gerçek Windows x64 self-hosted validation akışına sahiptir.

---

# CLI ile ilk plakayı okutmak

Engine, gerçek production pipeline'ını doğrudan görseller üzerinde çalıştırabilmek için offline CLI sağlar.

CLI build sırasında:

```text
FAC_LPR_BUILD_LPR_CLI=ON
```

aktif olmalıdır.

Örnek kullanım:

```bash
fac-lpr-cli plate.jpg \
  --model-dir ./models \
  --config ./path/to/model-contract.conf \
  --json
```

Sık kullanılan seçenekler:

```text
--json
--debug-evidence
--model-dir <path>
--config <path>
--log-level trace|debug|info|warn|error
```

`--json` kullanıldığında stdout machine-readable kalır. Bu sayede CLI başka script veya test sistemleri içerisinde de kullanılabilir.

---

# Uygulamaya nasıl entegre edilir?

## Stable C ABI

Engine'in dilden bağımsız ana entegrasyon yüzeyi:

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

- opaque engine handle kullanılır
- C++ exception ABI dışına çıkmaz
- result buffer caller tarafından yönetilir
- required-size için two-call pattern kullanılır
- version / size contract'ları açık şekilde tanımlıdır
- nested veriler buffer-relative offset/count ile taşınır
- ABI compatibility CI tarafından doğrulanır

Bu interface sayesinde engine yalnızca C++ uygulamalarına bağlı kalmaz.

```text
                   ┌─────────────────┐
                   │ FAC LPR Engine  │
                   │      C++20      │
                   └────────┬────────┘
                            │
                       Stable C ABI
                            │
          ┌─────────────────┼─────────────────┐
          │                 │                 │
          ▼                 ▼                 ▼
       C / C++          C# P/Invoke      Python ctypes
```

Repository içerisinde farklı consumer senaryolarını doğrulayan örnek ve test yolları bulunur.

---

# Mimari

FAC LPR Engine katmanlar arası bağımlılığı kontrollü tutan bir mimariye sahiptir.

```text
┌──────────────────────────────┐
│ Public API / Composition Root│
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│        Infrastructure        │
│ ONNX · OpenCV · Model Loader │
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│          Application         │
│ Orchestration · Policies     │
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│            Domain            │
│ Recognition Concepts / Rules │
└──────────────────────────────┘
```

### Domain

Vendor bağımsız plaka tanıma kavramlarını ve temel domain modellerini içerir.

### Application

Pipeline orchestration, port'lar ve recognition policy'lerini yönetir.

### Infrastructure

ONNX Runtime, OpenCV, model loading ve concrete runtime adapter'ları burada bulunur.

### Public API / Composition Root

Dış uygulamalara sunulan stabil interface'leri sağlar ve production pipeline'ını compose eder.

> ONNX Runtime veya OpenCV gibi vendor/runtime tiplerinin Domain, Application veya public C ABI içerisine sızmaması temel mimari kurallardan biridir.

---

# Repository yapısı

```text
fac-lpr-engine/
│
├── include/          Public C++ ve C ABI header'ları
├── src/              Engine implementation
├── tests/            Unit, integration ve regression testleri
├── tools/            CLI, evaluation ve resource araçları
├── samples/          Consumer entegrasyon örnekleri
├── cmake/            CMake package / export yardımcıları
├── scripts/          Bootstrap, validation ve release scriptleri
├── docs/             Teknik ve operasyonel dokümantasyon
├── models/           Model artifact / dokümantasyonu
│
├── README.md         İlk başlangıç ve genel kullanım
├── PRODUCT.md        Canonical ürün + mimari specification
└── AGENTS.md         AI agent / maintainer proje rehberi
```

---

# Test ve production validation

Bir LPR engine'in “bende çalışıyor” seviyesinde olması production-ready olduğu anlamına gelmez. Bu yüzden repository yalnızca unit testlerden ibaret değildir.

Validation kapsamı içerisinde:

- unit testler
- integration testleri
- real-model inference
- golden regression
- Linux x64 Debug / Release
- Windows x64 native validation
- macOS ARM64 validation
- sanitizer / static analysis / fuzz yolları
- memory ve resource stress testleri
- performance regression
- ABI compatibility
- C consumer
- C# consumer
- Python consumer
- CMake package consumption
- security / dependency kontrolleri
- SBOM ve release metadata
- production-readiness
- release-readiness

bulunur.

> **Skipped bir workflow, passed kabul edilmez.** Release politikası fail-closed çalışır ve promotion yapılacak exact candidate için gerekli gate'lerin gerçekten geçmesi beklenir.

---

# Güvenlik ve dayanıklılık prensipleri

Engine production kullanımında hatayı gizlemek yerine görünür ve kontrollü hale getirmeyi hedefler.

Temel prensipler:

- malformed input güvenli şekilde reddedilir
- model checksum / contract mismatch activation'ı engeller
- allocation arithmetic ve dışarıdan gelen dimension'lar doğrulanır
- queue / workspace / buffer kullanımı bounded tutulur
- native ownership RAII ile yönetilir
- C++ exception public C ABI dışına çıkmaz
- degraded execution açık şekilde raporlanır
- hassas görüntüler, crop'lar ve tam plaka metinleri rutin diagnostic loglara yazılmaz

---

# FAC Access ile ilişkisi

FAC LPR Engine ve FAC Access aynı şey değildir.

```text
┌────────────────────────────────────┐
│          FAC LPR Engine            │
│                                    │
│  “Bu görüntüde hangi plaka var?”   │
│  “Bu okumaya güvenebilir miyim?”   │
└──────────────────┬─────────────────┘
                   │
                   │ ACCEPTED
                   │ 34ABC123
                   ▼
┌────────────────────────────────────┐
│             FAC Access             │
│                                    │
│ “Bu plakanın giriş yetkisi var mı?”│
└──────────────────┬─────────────────┘
                   ▼
              ALLOW / DENY
```

FAC Access bugün engine'i public C ABI üzerinden consume eder. Ancak engine FAC Access olmadan da bağımsız olarak kullanılabilir.

Bu sayede aynı LPR motoru gelecekte farklı ürünlere gömülebilir ve business logic recognition katmanına taşınmaz.

---

# Branch modeli

```text
main  → stabil / production
 dev  → geliştirme / release candidate
```

Değişiklikler normalde `dev` üzerinde doğrulanır ve gerekli validation tamamlandıktan sonra `main` branch'ine promote edilir.

---

# Dokümantasyon

Projeye ilk kez geliyorsanız:

| Dosya | Ne zaman okunmalı? |
| --- | --- |
| **README.md** | Engine'in ne olduğunu, nasıl build edildiğini ve nasıl kullanıldığını anlamak için. |
| **AGENTS.md** | AI coding agent veya projeye yeni katılan maintainer olarak çalışmaya başlamadan önce. |
| **PRODUCT.md** | Ürün sınırları, mimari, model contract'ları, ABI ve release kurallarının canonical kaynağı olarak. |
| **docs/** | Detaylı teknik, operasyonel ve release dokümantasyonu için. |

AI agent'ların değişiklik yapmadan önce `AGENTS.md` dosyasını okuması beklenir.

---

## Projenin mevcut durumu

FAC LPR Engine'in ilk production-hardening yol haritası tamamlanmıştır.

Engine bugün:

- gerçek native recognition pipeline'ına,
- Türkiye plaka OCR modeline,
- model contract enforcement'a,
- stable C ABI'ye,
- C / C++ / C# / Python consumer yollarına,
- resource ve memory kontrollerine,
- regression / performance / ABI validation'larına,
- production ve release readiness gate'lerine

sahiptir.

Engine, FAC Access tarafından public C ABI üzerinden kullanılabilecek şekilde tasarlanmış ve aynı zamanda başka ürünlere gömülebilecek bağımsız bir LPR motoru olarak konumlandırılmıştır.

---

<div align="center">

### FAC LPR Engine

**Plakayı okumak ayrı iştir. O plakayla ne yapılacağına karar vermek ayrı.**

`Detection → Geometry → OCR → Evidence → Decision`

</div>
