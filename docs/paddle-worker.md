# PaddleOCR ikinci görüş: yerel worker ve FAC LPR entegrasyonu

Bu entegrasyon **yalnızca opt-in** olarak `dev` üzerinde hazırlanmıştır.
`main` ve `FAC_LPR_WITH_PADDLE_HTTP_WORKER=OFF` ile derlenen mevcut
kurulumlar etkilenmez.

## 1. Worker'ı çalıştır

Aşağıdaki komutlar Python ve CPU PaddlePaddle wheel desteği bulunan bir
sistem içindir. Windows'ta ortam etkinleştirme komutu farklıdır.

```bash
git switch dev
python3 -m venv .venv-paddle
source .venv-paddle/bin/activate
python -m pip install --upgrade pip
python -m pip install paddlepaddle paddleocr opencv-python-headless
python tools/paddle-worker/worker.py --port 8765 --device cpu
```

İlk başlatma model indirebilir. Ağ erişimi olmayan sistemlerde önceden
indirilen uyumlu PaddleOCR inference modelini
`--model-dir /path/to/rec_model` ile belirt. Apple Silicon veya bazı
işletim sistemlerinde PaddlePaddle wheel kullanılabilirliğini kendi
ortamına göre doğrula.

Worker yalnızca `127.0.0.1:8765` dinler. Yönetim API'si, kamera yaşam
döngüsü veya plaka veritabanı sağlamaz.

İkinci terminalde sağlık kontrolü:

```bash
curl -fsS http://127.0.0.1:8765/health
# {"status":"ok"}
```

İsteğe bağlı doğrudan worker denemesi, **bütün kamera karesi değil**, plaka
alanına kırpılmış `plate.png` ile:

```bash
curl -fsS -H "Content-Type: image/png" \
  --data-binary @plate.png \
  http://127.0.0.1:8765/recognize
# {"candidates":[{"text":"34ABC123","confidence":0.95}]}
```

Yukarıdaki plaka ve skor yalnızca örnek yanıt formatıdır; canlı OCR sonucu
garanti edilmez.

## 2. C++ motorunu PaddleOCR desteğiyle derle

Mevcut FAC LPR geliştirme ortamında ONNX Runtime, OpenCV, CMake,
`nlohmann_json` ve `libcurl` gereklidir. Mevcut proje bağımlılık
kurulumunu kullan; bu komutlar eksik native paketleri otomatik kurmaz.

Linux GCC/Ninja preset örneği:

```bash
cmake --preset linux-gcc-debug \
  -DFAC_LPR_WITH_ONNX_RUNTIME=ON \
  -DFAC_LPR_WITH_OPENCV=ON \
  -DFAC_LPR_BUILD_LPR_CLI=ON \
  -DFAC_LPR_WITH_PADDLE_HTTP_WORKER=ON
cmake --build --preset linux-gcc-debug
```

Windows ve macOS için mevcut build/paket düzenini koruyup yalnızca
`-DFAC_LPR_WITH_PADDLE_HTTP_WORKER=ON` ekle. Yalnızca bu seçenek
derlenmişken `paddle.enabled=true` çalışır.

## 3. Runtime ayarını etkinleştir

`docs/lpr-cli-contract.example` dosyasının yerel bir kopyasında:

```ini
paddle.enabled=true
paddle.port=8765
```

Örnek Linux CLI çalıştırma:

```bash
cp docs/lpr-cli-contract.example ./lpr.contract
# ./lpr.contract içinde paddle.enabled=true yap
./build/linux-gcc-debug/artifacts/bin/fac-lpr-cli image.jpg \
  --model-dir ./models \
  --config ./lpr.contract \
  --debug-evidence
```

`models` içinde üretim contract'ına uyan `best.onnx` ve
`lprnet_turkey.onnx` bulunmalı. Gerçek model dosyaları Git deposunda
bulunmaz. Modelin yüklenmesi için mevcut checksum/şekil sözleşmesi aynen
geçerlidir.

## 4. Mevcut davranış ve güvenlik sınırı

- YOLO plakayı bulur, **LPRNet ana OCR** olarak kalır.
- PaddleOCR aynı kırpılmış plakayı bağımsız okur, C++ içindeki
  `RecognitionEnsemble` kanıtları taşır.
- PaddleOCR'ın **candidate fusion ağırlığı 0**'dır: LPRNet'in kalibre edilmemiş
  skoruyla toplanmaz, otomatik ACCEPTED üretemez.
- **paddle.enabled=true** iken yeni güvenli karar mantığı devrededir.
  Plaka formatına uygun, >=0.85 güvenli ve çelişmeyen Paddle sonucu;
  LPRNet adayının bulunmadığı, zayıf olduğu, tek bir eşleşen greedy
  karakterinin güveni <0.65 olduğu **veya Paddle metni LPRNet'in geçerli
  beam-search alternatiflerinden biriyle birebir eşleştiğinde** yalnızca
  **REVIEW-only** önerilebilir. İkinci durumda güçlü bir LPRNet top-1
  adayı bile Paddle ile eşleşen top-5 alternatifi engellemez; fakat
  güçlü iki sağlayıcıdan çelişkili okumalar varsa otomatik ACCEPTED
  üretilmez.
  Önerinin confidence alanı 0'dır; Paddle skoru LPRNet skoru diye sunulmaz.
  Güçlü LPRNet/Paddle uyuşmazlığında özgün aday korunur, sonuç REVIEW olur.
  Birden fazla güçlü Paddle alternatifi varsa seçim yapılmaz.
- LPRNet'in ham CTC dizisi seçili sonuçla birebir eşleşiyorsa karakter
  skorları kullanılabilir. Farklı beam-search adaylarına yanlış skor
  atanmaz.
- İki sağlayıcı güçlü şekilde **çelişirse** karar `REVIEW`e düşebilir.
  Yeni reason: `secondary_ocr_review_suggestion` (sadece inceleme, asla ACCEPTED).
  Bu nedenle mod tam anlamıyla pasif gözlemci değildir.
- PaddleOCR worker'ı etkin ama erişilemezse provider optional olsa da
  sonuç `degraded` olur ve `REVIEW`e düşebilir. İstenmeyen gecikmeler
  için özellik `false` tutulabilir.
- `ACCEPTED` yalnız teknik tanımadır; bariyer açma kararı değildir.
- Worker ham plakayı loglamaz veya diske kaydetmez; yalnızca loopback'te
  çalışır. Başka makineden erişim için güvenlik tasarımını yeniden ele al.

## 5. Sözdizimi ve doğrulama

```bash
python -m py_compile tools/paddle-worker/worker.py tools/paddle-worker/test_worker.py
python -m unittest discover -s tools/paddle-worker -p "test_*.py" -v
```

Bunlar Python protokolünü sınar; gerçek modelin plakayı doğru okuyacağını
garanti etmez. C++ kodunun tam derleme, API/ABI uyumu ve gerçek plaka
regresyonlarının ayrıca doğrulanması gerekir. `dev` değişiklikleri
başarılı CI kanıtı olmadan `main`e alınmamalıdır.
