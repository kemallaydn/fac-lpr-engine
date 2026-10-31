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
  skoruyla toplanmaz. Varsayılan olarak REVIEW-only kalır; deneysel koşullu
  kabul ancak ayrı bir ayarla etkinleşir.
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

## 5. Deneysel koşullu kabul, varsayılan KAPALI

`paddle.conditional_accept=false` mevcut güvenli davranışı korur.
Yalnızca geniş ve bağımsız gerçek kamera verisiyle yanlış kabul oranı
ölçüldükten sonra kontrollü açılmalıdır. `paddle.enabled=true` gerektirir.

`paddle.conditional_accept=true` ile PaddleOCR ham skoru en az 0.90,
aynı metni içeren LPRNet beam adayı en az 0.78 olmalıdır. Paddle ve LPRNet
skorları birleştirilmez; final confidence LPRNet'in skorudur.
Aynı karede birden fazla güçlü fakat farklı Paddle okuması, çok güçlü
çelişen hizalı greedy karakteri, başarısız provider, zayıf dedektör,
geometri veya crop kalitesi otomatik kabule engeldir.
Top-1 LPR adayıyla eşleşmede ikinci adaya en az 0.10 fark;
LPR top-2 alternatifiyle eşleşmede top-1 ile en çok 0.05 fark aranır.
Diğer top-5 adayları ancak REVIEW önerisi olabilir.

Bu, TEK KARELIK `ACCEPTED` teknik kararıdır. Video için ayrıca
`RecognitionStreamSession` gerekir: varsayılan en az 2 ayrı kabul
edilmiş kare, 1500 ms geçmiş TTL ve eşleşen tekrarlı çıktılar için
2000 ms cooldown. Bunlar sabit bir sonuç gecikmesi değil, kriterdir.
RTSP bağlantısı, araç kimliği takibi, kapı/bariyer yetkilendirmesi
ve dış sistem event teslimatı motorun sorumluluğu değildir.

Üç gerçek fotoğrafta 3/3 REVIEW önerisi bulunması bu koşullu otomatik
kabul modunu canlıda açmak için yeterli kanıt DEĞİLDİR.

## 6. Sözdizimi ve doğrulama

```bash
python -m py_compile tools/paddle-worker/worker.py tools/paddle-worker/test_worker.py
python -m unittest discover -s tools/paddle-worker -p "test_*.py" -v
```

Bunlar Python protokolünü sınar; gerçek modelin plakayı doğru okuyacağını
garanti etmez. C++ kodunun tam derleme, API/ABI uyumu ve gerçek plaka
regresyonlarının ayrıca doğrulanması gerekir. `dev` değişiklikleri
başarılı CI kanıtı olmadan `main`e alınmamalıdır.
