# Real LPR fixtures

Bu dizin `fac-lpr-cli` ve uçtan uca LPR doğrulaması için gerçek araç/plaka görüntülerini içerir.

## Dosya düzeni

Mevcut golden fixture seti:

- `plate_01.jpg`
- `plate_02.jpg`
- `plate_03.jpg`

JPEG veya PNG kullanılabilir. Tercihen aracın tamamı/kameranın gerçek görüşü bulunsun; yalnızca kırpılmış plaka görüntüsü olmasın.

## Beklenen sonuç formatı

`expected-results.txt` aşağıdaki formatı kullanır:

```text
filename=REQUIRED_PLATE[,REQUIRED_PLATE...]|OPTIONAL_PLATE[,OPTIONAL_PLATE...]
```

- `|` işaretinin solundaki plakalar testin geçmesi için mutlaka tanınmalıdır.
- Sağ taraftaki plakalar aynı frame'de görünür ancak mesafe/blur/örtüşme gibi nedenlerle opsiyonel kabul edilir.
- `|` yoksa listedeki tüm plakalar required kabul edilir.
- Plaka metni decoder çıktısıyla aynı şekilde, boşluksuz ve büyük harfle yazılır.

Örnek:

```text
plate_02.jpg=06ACY760|06AHU99
```

Burada `06ACY760` yakın/net primary golden result, `06AHU99` ise secondary/optional result'tır.

## E2E doğrulama

`FAC_LPR_BUILD_LPR_CLI=ON`, `FAC_LPR_BUILD_TESTS=ON`, OpenCV ve ONNX Runtime açık olduğunda `fac_lpr_cli_real_e2e` CTest'i gerçek `fac-lpr-cli` executable'ını çalıştırır. Test synthetic/fake provider kullanmaz; `models/best.onnx`, `models/lprnet_turkey.onnx`, aktif CLI model contract'ı ve bu dizindeki görüntüler üzerinden gerçek pipeline sonucunu kontrol eder.

Gerçek kişisel/veri hassasiyeti taşıyan görüntüler production repository'sine eklenmeden önce veri kullanım izni ve saklama politikası ayrıca değerlendirilmelidir.
