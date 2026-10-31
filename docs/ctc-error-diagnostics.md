# LPRNet / CTC karakter atlama nedenleri: tekrarlanabilir tanılama

**Amaç:** Üç örneği düzeltmek için sabit kural yazmak değil, bütün plakalarda
atlanan, eklenen ve karıştırılan karakterleri aynı protokolle incelemek.

## Verinin akışı

1. Mevcut YOLO + ONNX testleri gerçek modelin zaman-adımı logits dizilerini
   üretir (`24 x 34`, float32); modelin hash ve giriş sözleşmesi değişmez.
2. Mevcut **gerçek C++** beam search aynı logits üzerinde ilk beş adayı
   oluşturur. Bu Python aracı beam search yerine geçmez.
3. `tools/ocr-compare/ctc_error_diagnostics.py` gerçek ground-truth ile
   yalnızca test sırasında **CTC Viterbi forced alignment** yapar. CTC'nin
   tekrarlanan karakterler için gerekli `blank` geçişini de dikkate alır.
4. Her hata için hedef karakter, dizideki konumu, hizalanan zaman adımı,
   o adımda karakter/`blank` softmax değerleri, karakter sırası, greedy hata
   türü ve C++ beam sonucu JSON/Markdown olarak kaydedilir.

Bu analiz ground-truth **gerektirir**, bu yüzden asla canlı plaka kararına
veya yeni güven skoruna dönüşmez. Softmax ve beam skorları gerçek doğruluk
ihtimali değildir. Referansa zorlanmış bir CTC yolu, harfin model tarafından
gerçekten görüldüğünü **kanıtlamaz**.

## GitHub Actions, GitHub-hosted runner

- `lprnet-root-cause-hosted`: 32 sentetik kırpma, dört ön işleme varyantı,
  toplam 128 logits/beam/karakter analiz örneği.
- `real-photos-native-beam-hosted`: `tests/fixtures/real/` içindeki üç gerçek
  fotoğraf, gerçek YOLO kırpması, gerçek LPRNet ONNX + C++ beam ve karakter
  analizi. İkinci workflow **kırpma, karakter ve skor** sorunlarını ayrı inceler.
- Her iki workflow raporları Actions **Artifacts** olarak saklar; dosyalar
  `ctc-character-loss.json` ve `.md` adlarıyla bulunur.
- Genel algoritma testleri: `python -m unittest discover -s tools/ocr-compare -p 'test_ctc_error_diagnostics.py' -v`.

## Rapor nasıl yorumlanır?

- **Greedy deletion, blank yüksek:** Belirli bir zaman adımında CTC karakter
  yerine `blank` seçmiş olabilir. Bu, model/kırpma/CTC farkını araştırmak
  için bir ipucudur, tek başına kesin nedensellik değildir.
- **Doğru harf beam top-5 içinde, top-1 değil:** Model alternatif yolu
  bulmuş ama skorlamada daha aşağıya düşürmüş. Hedefe özel sabit bir ödül
  eklemek yerine veri üzerinde aday sıralaması doğruluğuna bak.
- **Kırpma değişince beam başarısı değişiyor:** YOLO kutusu, boşluk oranı,
  perspektif ve ölçek önemli olabilir. Sadece üç kareye göre canlı kırpma
  ayarını değiştirme.
- **Yüksek raw confidence, yanlış metin:** Gerçek örneklerle skoru
  kalibre etmeden `0.98 == %98 doğru` deme.

## Kalıcı iyileştirme için ölçüm şartı

Üretim ayarları veya yeni aday sıralayıcı parametreleri değiştirilmeden önce
farklı araç, kamera, font, gündüz/gece, bulanıklık, yağmur, mesafe ve açıdan
toplanmış **ayrı eğitim/kalibrasyon ve hiç görülmemiş test kümeleri** ile
exact plate rate, karakter hataları, yanlış otomatik ACCEPT ve REJECT oranları
ölçülmeli. Video akışında aynı aracın kareleri rastgele eğitim/teste
bölünmemeli. Kişisel veri mevzuatı ve kullanım hakları korunmalı.

**Güvenlik:** PaddleOCR ikinci görüşü `REVIEW` sınırını aşmaz; bu çalışma
`SafeRecognitionDecisionPolicy` veya üretim eşiklerini değiştirmez. `main`
değiştirilmez.