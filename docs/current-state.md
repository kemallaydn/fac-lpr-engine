# Current validated project state

Snapshot date: **2026-08-30**

Bu dosya FAC LPR Engine'in kısa operasyonel snapshot'ıdır. `PRODUCT.md` canonical ürün/mimari specification'dır; source, public header, tests/model contracts ve executed CI evidence bir snapshot eskidiğinde authority kabul edilir.

## Branch modeli

- `dev`: aktif development/release-candidate branch.
- `main`: stable/release branch.
- Normal promotion: `dev -> main`.

2026-08-30 tarihinde önceki validated `dev` candidate, PR #123 üzerinden `main`e promote edildi. Promotion merge commit'i:

```text
5516d9e26ae5bc254e225e654dd637f971639ec4
```

Bu promotion sonrasında `main` ile promote edilen `dev` arasında dosya farkı yoktu. Daha sonra yapılan documentation-only synchronization çalışması tekrar `dev` üzerinde hazırlanmıştır; promotion öncesi `main...dev` diff yalnız beklenen Markdown/prose değişikliklerini içermelidir.

## Ürün sınırı

FAC LPR Engine teknik license-plate recognition yapar. Araç yetkilendirmez ve bariyer açmaz.

```text
ACCEPTED = recognition evidence teknik olarak güçlü
ACCEPTED != access allowed
```

Authorization FAC Access veya başka consuming product'a aittir.

## Production recognition pipeline

```text
image/frame
  -> detector
  -> geometry validation
  -> perspective/crop hypotheses
  -> OCR
  -> confidence/layout evidence
  -> candidate fusion
  -> technical decision
  -> ACCEPTED / REVIEW / REJECTED
```

Optional stream path:

```text
LprPipeline
  -> TemporalPlateConsensus
  -> StablePlateEventFilter
  -> RecognitionStreamSession
```

C ABI v1 stream/session özellikleri nedeniyle değiştirilmemiştir.

## Son recognition hardening

Plate-dominant input'larda detector bbox kaynak görüntünün büyük bölümünü kapladığında gereksiz rectification/cropping OCR evidence'ını bozmasın diye full source frame primary hypothesis olarak korunur. Ordinary scene crop stratejileri devam eder.

Bu davranış plate-specific hardcoded karakter düzeltmesi değildir.

Deterministic licensed public regression exact olarak şunları doğrular:

```text
38VU055
34VZ7387
```

Fixture'lar test sırasında Wikimedia kaynaklarından indirilir, pinned SHA-1 ile doğrulanır ve production `fac-lpr-cli` detector/crop/OCR yolundan geçirilir.

```text
38_VU_055.jpg   790095caf25739f07c00c55c7a513f70e8b7ef02
34_VZ_7387.jpg  6684fe7e4a6a0e742194ec12b08c980257512858
```

Expected plate text future regression'a uydurulmak için gevşetilmemelidir.

## Validation evidence

Promote edilen runtime candidate için doğrulanan başlıca gate'ler:

- Linux x64 Debug/Release full Docker validation: success;
- Windows x64 Debug/Release native validation: success;
- Release DLL clean-load: success;
- .NET P/Invoke consumer: success;
- Python `ctypes` consumer: success;
- macOS ARM64 C ABI / installed-package consumer: success;
- production lifecycle + real inference smoke: success;
- licensed public fixture exact regression: success.

Dokümantasyon-only değişiklik runtime davranışını değiştirmez. Bu tür bir promotion'da full runtime CI'ı yeniden koşturmak yerine diff'in yalnız beklenen Markdown/prose dosyalarından oluştuğu doğrulanabilir.

## Performance CI

Linux x64 performance validation, FAC-LPR macOS ARM64 self-hosted runner üzerinde `linux/amd64` Docker kullanır.

Workflow path-aware'dir:

- runtime/performance-sensitive change -> real benchmark/comparison;
- docs/test-metadata/runtime'ı etkileyemeyen CI change -> expensive benchmark skip;
- skip kararı runtime benchmark pass olarak yorumlanmaz.

Sampling:

```text
warmup: 10
iterations: 50
repeats: 2
```

Threshold'lar green CI uğruna gevşetilmez.

## Software work state

Son doğrulanan engine hardening çalışmaları:

- plate-dominant production crop fix;
- licensed public exact fixture regression;
- Windows CLI regression registration;
- C ABI/consumer validation;
- GitHub CLI bağımlılığı kaldırılmış performance baseline resolution;
- path-aware performance execution;
- CI/documentation synchronization.

Son canlı kontrolde:

```text
open engine issues: 0
open engine pull requests: 0
```

Bu sayıları gelecek oturumlarda sabit gerçek kabul etme; canlı GitHub state'i yeniden sorgula.

## FAC Access ilişkisi

FAC Access engine'i stable C ABI üzerinden .NET Device Service Infrastructure katmanında tüketir.

FAC Access tarafında real-camera E2E/soak/outage recovery ve ONVIF real-camera onboarding gibi fiziksel hardware release gate'leri bulunabilir. Bunlar repo-only evidence ile kapatılmamalıdır ve engine software backlog'u ile karıştırılmamalıdır.

## AI/maintainer için authority kuralı

Yeni bir oturumda:

1. `AGENTS.md`;
2. `README.md`;
3. `PRODUCT.md`;
4. `docs/current-state.md`;
5. ilgili source/header/test/docs;
6. live GitHub issue/PR/CI

sırasıyla doğrulanmalıdır.

Eski sohbet bağlamı, eski issue body veya stale snapshot source/test/executed CI evidence'ın üstünde authority değildir.

## Promotion checklist

Runtime/code promotion:

1. exact `dev` head'i doğrula;
2. applicable exact-head CI evidence'ını doğrula;
3. open issue/PR state'i kontrol et;
4. `main...dev` delta'yı incele;
5. validated candidate'ı `main`e promote et;
6. `main`in promoted candidate'ı içerdiğini doğrula.

Documentation-only promotion:

1. `main...dev` diff'i incele;
2. yalnız beklenen Markdown/prose değişiklikleri olduğundan emin ol;
3. runtime/model/build/test davranışını değiştiren dosya olmadığını doğrula;
4. full runtime CI tekrarı olmadan documentation promotion yapılabilir;
5. promotion sonrası branch içerik farkını yeniden kontrol et.
