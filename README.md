# 🚘 FAC LPR Engine

FAC LPR Engine, **Türkiye plakaları için geliştirilmiş production-ready, gömülebilir bir plaka tanıma motorudur**.

**C++20 · ONNX Runtime · OpenCV · CMake · Stable C ABI v1**

Bir görüntü veya video karesini alır; plakayı tespit eder, geometrisini değerlendirir, uygun crop adaylarını üretir, OCR çalıştırır, kanıtları birleştirir ve teknik olarak gerekçelendirilmiş bir tanıma sonucu üretir.

> En önemli sınır: `ACCEPTED`, plakanın teknik olarak güvenilir biçimde tanındığı anlamına gelir. **Aracın içeri alınabileceği veya bariyerin açılabileceği anlamına gelmez.** Yetkilendirme FAC Access gibi tüketici uygulamanın sorumluluğudur.

---

## İçindekiler

- [Projenin amacı](#projenin-amacı)
- [Tanıma akışı](#tanıma-akışı)
- [Güncel yetenekler](#güncel-yetenekler)
- [Gerçek plaka regresyonları](#gerçek-plaka-regresyonları)
- [Stateless ve stream tanıma](#stateless-ve-stream-tanıma)
- [Mimari](#mimari)
- [Model sözleşmeleri](#model-sözleşmeleri)
- [Entegrasyon yüzeyleri](#entegrasyon-yüzeyleri)
- [Build ve test](#build-ve-test)
- [CI ve production doğrulama](#ci-ve-production-doğrulama)
- [Proje sınırı](#proje-sınırı)
- [Dokümantasyon haritası](#dokümantasyon-haritası)
- [Branch ve release modeli](#branch-ve-release-modeli)

---

## Projenin amacı

Motorun temel sorusu şudur:

> **“Bu görüntüde hangi plaka var ve bu sonuca teknik olarak ne kadar güvenebilirim?”**

FAC LPR Engine yalnızca OCR yapan ince bir wrapper değildir. Production pipeline içerisinde şunları birlikte yönetir:

- plaka detection;
- keypoint/geometri değerlendirmesi;
- perspective alignment;
- crop hypothesis üretimi;
- OCR inference;
- confidence ve layout evidence;
- aynı frame içindeki candidate fusion;
- teknik karar politikası;
- optional multi-frame temporal consensus;
- bounded recognition-level duplicate suppression;
- model/runtime contract doğrulaması;
- bounded native kaynak kullanımı;
- stage timing ve teknik diagnostics.

Public teknik kararlar:

| Karar | Anlamı |
| --- | --- |
| `ACCEPTED` | Recognition evidence teknik olarak yeterince güçlü. |
| `REVIEW` | Sonuç var ancak belirsizlik nedeniyle ek kontrol gerekebilir. |
| `REJECTED` | Güvenilir bir plaka sonucu üretilemedi. |

Motor fail-closed davranır. Zayıf veya çelişkili evidence sırf bir sonuç üretmek uğruna iyimser biçimde kabul edilmez.

---

## Tanıma akışı

Canonical tek-frame production akışı:

```text
Görüntü / Frame
      ↓
Plate Detection
      ↓
Geometry Validation
      ↓
Perspective / Crop Hypotheses
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

Production composition; detector, geometri/crop, OCR, evidence ve karar katmanlarını tek bir `LprPipeline` altında birleştirir. CLI ve consumer testleri ayrı bir recognition implementation kullanmaz; aynı production pipeline üzerinden çalışır.

---

## Güncel yetenekler

### 🇹🇷 Türkiye plakalarına özel OCR

Aktif OCR modeli Türkiye plaka karakter seti ve production runtime contract'ına göre çalışır.

### 🎯 Detection + keypoint geometri

Detector yalnızca bounding box üretmez. Keypoint evidence perspective ve crop aşamalarında kullanılır.

### 🔍 Multi-crop ve candidate fusion

Tek crop/tek OCR sonucuna körü körüne bağlı kalınmaz. Aynı frame içindeki farklı hipotezler ve evidence deterministic biçimde değerlendirilir.

### 🖼️ Plate-dominant görüntü koruması

Detector kutusu kaynak görüntünün çok büyük bölümünü kapladığında gereksiz rectification/cropping OCR evidence'ını bozmasın diye full source frame birincil hipotez olarak korunur. Normal sahnelerde mevcut crop stratejisi devam eder.

### 🧭 Multi-frame temporal consensus

Video/kamera senaryolarında optional `RecognitionStreamSession`, art arda gelen frame sonuçlarını bounded history içinde birleştirebilir.

### 🔁 Stable recognition emission

Aynı stabil plakanın komşu frame'lerde gereksiz tekrar emit edilmesi bounded, session-local suppression ile azaltılabilir. Bu mekanizma business-level giriş/çıkış deduplication değildir.

### 🔒 Model integrity ve runtime contract

Bir ONNX dosyasının açılması tek başına yeterli kabul edilmez. Tensor adları, shape, preprocessing, charset, blank semantics ve model bütünlüğü explicit contract olarak doğrulanır.

### 🔌 Stable entegrasyon

- C++
- C / Stable C ABI v1
- C# / P/Invoke
- Python / `ctypes`
- installed/exported CMake package

Stream/session özelliği mevcut C ABI v1 wire layout'ını değiştirmez.

---

## Gerçek plaka regresyonları

Production detector/crop/OCR yolunu gerçek, lisanslı public görüntülerle doğrulayan deterministic regression gate bulunmaktadır.

Exact beklenen plakalar:

```text
38VU055
34VZ7387
```

Fixture'lar binary olarak repoya kopyalanmaz. Test sırasında lisanslı Wikimedia kaynaklarından indirilir, **pinned SHA-1** ile doğrulanır ve production `fac-lpr-cli` üzerinden çalıştırılır.

```text
38_VU_055.jpg   790095caf25739f07c00c55c7a513f70e8b7ef02
34_VZ_7387.jpg  6684fe7e4a6a0e742194ec12b08c980257512858
```

Bu testin amacı “bir şey okudu” demek değildir. Beklenen exact plate text korunur. Gelecekte model veya pipeline gerilerse test beklentisi mevcut yanlış çıktıya göre gevşetilmemelidir.

Fixture provenance ve lisans bilgileri: `tests/fixtures/public/README.md` ve `tests/fixtures/public/public-manifest.tsv`.

---

## Stateless ve stream tanıma

### Stateless

`LprPipeline::recognize()` her çağrıyı bağımsız işler:

```text
frame -> LprPipeline -> PlateRecognitionResult
```

Tek görsel, API isteği veya bağımsız frame senaryoları için canonical yoldur.

### Optional stateful stream

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

Stream katmanı:

- bounded temporal history tutar;
- confidence/recency tabanlı consensus uygular;
- session state'lerini izole eder;
- reset/close lifecycle sunar;
- ambiguous multi-plate frame'leri tek identity altında karıştırmaz;
- zayıf `REVIEW` sonuçlarını tekrar sayısıyla sessizce `ACCEPTED` yapmaz.

Detaylar: `docs/stream-recognition-api.md` ve `docs/temporal-stream-recognition.md`.

---

## Mimari

Dependency yönü içeri doğrudur:

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

Vendor bağımsız recognition kavramları ve value type'ları. ONNX Runtime/OpenCV tipleri buraya sızmaz.

### Application

Recognition orchestration ve vendor-neutral policy/port'lar. `LprPipeline`, temporal consensus, stable emission ve stream session burada yaşar.

### Infrastructure

ONNX Runtime, OpenCV, model loader, detector/OCR adapter'ları ve native runtime ayrıntıları.

### Public API / Composition Root

Stable integration yüzeyleri ve production assembly.

Daha ayrıntılı mimari için `PRODUCT.md`, `AGENTS.md` ve `docs/architecture.md` canonical kaynaklardır.

---

## Model sözleşmeleri

Production modelleri:

```text
models/
├── best.onnx
└── lprnet_turkey.onnx
```

Detector contract:

```text
input  images   float32 [1,3,960,960]
output output0  float32 [1,17,18900]
```

OCR contract:

```text
input   input   float32 [1,3,40,160]
output  output  float32 [1,34,24]
layout  BCT
blank   33
charset 0123456789ABCDEFGHIJKLMNOPRSTUVYZ
```

Bu değerler örnek değil, production contract'ıdır. Model değişikliğinde tensor adı/shape/preprocessing/charset/blank semantics/checksum birlikte değerlendirilmelidir.

---

## Entegrasyon yüzeyleri

Stable public C header:

```text
include/fac_lpr/fac_lpr_engine.h
```

Temel C ABI lifecycle:

```c
fac_lpr_engine_create_v1(...);
fac_lpr_engine_recognize_v1(...);
fac_lpr_engine_destroy_v1(...);
fac_lpr_get_last_error_v1(...);
```

C ABI v1 tasarım ilkeleri:

- opaque engine handle;
- C11-compatible surface;
- C++ exception ABI dışına çıkmaz;
- caller-owned flat result buffer;
- required-size için two-call pattern;
- explicit size/version contract;
- nested veriler buffer-relative offset/count kullanır.

FAC Access, engine'i .NET Device Service Infrastructure katmanından bu stable C ABI üzerinden tüketir. Engine `ACCEPTED` üretti diye bariyer açılmaz; backend authorization ayrıca `ALLOWED` vermelidir.

---

## Build ve test

### Gereksinimler

```text
C++20 uyumlu compiler
CMake 3.25+
Ninja
ONNX Runtime
OpenCV
```

### Linux Debug

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure
```

### Linux Release

```bash
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release --output-on-failure
```

### Windows

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
cmake --build --preset windows-msvc-release
```

### CLI

```text
FAC_LPR_BUILD_LPR_CLI=ON
```

```bash
fac-lpr-cli plate.jpg \
  --model-dir ./models \
  --config ./docs/lpr-cli-contract.example \
  --json
```

---

## CI ve production doğrulama

Repository yalnızca unit test çalıştırıp kendini production-ready ilan etmez. Validation yüzeyleri arasında şunlar bulunur:

- unit/integration tests;
- temporal/session regression;
- real-model inference;
- golden regression;
- licensed public real-image regression;
- Linux x64 Debug/Release Docker validation;
- Windows x64 native Debug/Release validation;
- Release DLL clean-load;
- C ABI consumer smoke;
- C# P/Invoke consumer validation;
- Python `ctypes` consumer validation;
- package/ABI/security/resource checks;
- memory/resource stress;
- performance regression.

### Performance CI

Linux x64 benchmark, FAC-LPR macOS ARM64 self-hosted runner üzerinde `linux/amd64` Docker ile çalışır.

Runtime/performance-sensitive değişikliklerde gerçek benchmark çalışır. PR veya push yalnızca dokümantasyon, test metadata'sı veya runtime'ı etkileyemeyen CI alanlarına dokunuyorsa workflow bunu path-aware olarak belirler ve pahalı benchmark aşamalarını skip eder.

Mevcut sampling:

```text
warmup: 10
iterations: 50
repeats: 2
```

Performance threshold'ları sırf CI yeşil olsun diye gevşetilmez.

CI ayrıntıları: `docs/ci.md` ve `docs/ci-budget.md`.

---

## Proje sınırı

FAC LPR Engine şunları **yapmaz**:

- RTSP bağlantı yönetimi;
- kamera discovery/reconnect;
- araç yetkilendirme;
- kayıtlı araç lookup;
- bariyer açma kararı;
- customer/business event cooldown;
- audit persistence;
- UI/backend business state;
- model training lifecycle.

Bu sorumluluklar consuming product'a aittir.

---

## Repository yapısı

```text
fac-lpr-engine/
├── include/          Public C++ / C ABI header'ları
├── src/              Domain / Application / Infrastructure
├── tests/            Unit, integration, temporal, golden ve public regression
├── tools/            CLI, benchmark, evaluation ve resource araçları
├── samples/          Consumer entegrasyon örnekleri
├── cmake/            Package/export yardımcıları
├── scripts/          Bootstrap/validation/release scriptleri
├── docs/             Teknik ve operasyonel dokümantasyon
├── models/           Runtime model artifact'ları
├── README.md         İnsan odaklı Türkçe giriş
├── PRODUCT.md        Canonical ürün/mimari/runtime specification
└── AGENTS.md         AI/maintainer çalışma ve continuity rehberi
```

---

## Dokümantasyon haritası

Bir insan veya yapay zekâ projeyi devralırken dosyaları şu sırayla okumalıdır:

1. **`AGENTS.md`**: AI/maintainer için çalışma kuralları, değişmemesi gereken invariant'lar ve repo haritası.
2. **`README.md`**: Türkçe, insan odaklı ürün ve kullanım özeti.
3. **`PRODUCT.md`**: canonical ürün, mimari, model/runtime, ABI ve release specification.
4. **`docs/current-state.md`**: güncel doğrulanmış implementation/release snapshot'ı.
5. İlgili `docs/*.md`: konuya özel detaylar.
6. İlgili source/header/test dosyaları.
7. GitHub issue/PR ve **executed CI evidence**.

Önemli authority kuralı:

> Dokümantasyon ile source/test/CI çelişirse tahmin yürütme. Canlı implementation ve executed evidence'ı doğrula, sonra stale dokümanı düzelt.

Özellikle AI agent'ları eski sohbet bağlamını veya eski issue açıklamasını repository gerçeğinin üstünde authority kabul etmemelidir.

### Başlıca teknik belgeler

- `AGENTS.md` — AI/maintainer continuity ve çalışma rehberi
- `PRODUCT.md` — canonical product/architecture specification
- `docs/current-state.md` — güncel operasyonel snapshot
- `docs/architecture.md` — dependency/mimari özeti
- `docs/c-api-v1.md` — C ABI v1 contract
- `docs/abi-versioning.md` — ABI versioning kuralları
- `docs/stream-recognition-api.md` — stateful stream API
- `docs/temporal-stream-recognition.md` — temporal semantics
- `docs/ci.md` — CI topology ve gate'ler
- `docs/ci-budget.md` — CI maliyet/runner davranışı
- `docs/lpr-cli.md` — CLI kullanımı

---

## Branch ve release modeli

- `dev`: aktif geliştirme ve release-candidate branch'i.
- `main`: stable/release branch'i.

Normal akış:

```text
feature/fix
   ↓
dev
   ↓
applicable validation
   ↓
validated dev candidate
   ↓
main
```

Kod/runtime değişiklikleri applicable validation evidence olmadan `main`e taşınmamalıdır.

Dokümantasyon-only değişiklikler runtime davranışını değiştirmez; yine de branch farkı açıkça incelenmeli ve yalnız beklenen Markdown/prose değişikliklerinin taşındığı doğrulanmalıdır.

---

## Güncel durum

Güncel doğrulanmış proje snapshot'ı için **`docs/current-state.md`** dosyasını kullanın. GitHub issue/PR/CI durumu zamanla değişebileceği için canlı GitHub state her yeni çalışma oturumunda yeniden doğrulanmalıdır.

FAC LPR Engine'in temel mimari kuralı değişmez:

> **Engine plakayı tanır. Consuming product erişime karar verir.**
