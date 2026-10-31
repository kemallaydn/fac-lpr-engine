# Deterministic synthetic Turkish plate evaluation fixtures

Run:

```bash
python -m pip install Pillow==11.3.0
python tools/synthetic-plates/generate.py --out build/synthetic-plates
python -m unittest discover -s tools/synthetic-plates -p 'test_*.py' -v
```

This creates 32 PNGs (8 labels x 4 variants) and an evaluation manifest usable
with `tools/evaluation/evaluate.py`. Cases cover `B/8`, `O/0`, `I/1` and other
common reading ambiguity; variants include clean, blurred, low-light and tilt.
The synthetic strings are **not asserted to correspond to actual registered
vehicles**. Generated images are synthetic *test artefacts*, never real-camera
validation or real-world accuracy evidence.

The GitHub Actions workflow `.github/workflows/synthetic-ocr-fixtures.yml`
checks deterministic generation and uploads the rendered PNGs as an artifact.
These images deliberately are not committed as binary blobs to keep the repo
small. A required OCR accuracy pass should only be added after both the ONNX
models and the PaddleOCR runtime are available on a suitable runner and the
metrics are validated with real images as well.

The repo already includes **two licensed, checksum-pinned Wikimedia Commons
plates** in `tests/fixtures/public/public-manifest.tsv`. Their original files
are downloaded only at test time, with attribution and licence tracked in the
manifest. Do not replace them with arbitrary search-engine images or silently
weaken the expected exact plate values.
