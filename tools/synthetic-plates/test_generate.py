"""Tests for *fixture creation*, not for recognition or model correctness."""
from __future__ import annotations

import importlib.util
from hashlib import sha256
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from PIL import Image, ImageChops

spec = importlib.util.spec_from_file_location("fac_synthetic_plates", Path(__file__).with_name("generate.py"))
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class GenerationTests(unittest.TestCase):
    def test_manifest_images_and_reproducibility(self):
        font = module.find_font()
        with TemporaryDirectory() as a, TemporaryDirectory() as b:
            rows_a = module.generate(Path(a), font)
            rows_b = module.generate(Path(b), font)
            self.assertEqual(rows_a, rows_b)
            self.assertEqual(len(rows_a), len(module.PLATES) * len(module.VARIANTS))
            self.assertEqual(len({name for name, _ in rows_a}), len(rows_a))
            manifest = (Path(a) / "manifest.tsv").read_text(encoding="utf-8")
            self.assertEqual(len([x for x in manifest.splitlines() if x and not x.startswith('#')]), len(rows_a))
            for name, plate in rows_a:
                self.assertIn(f"{name}\tANY\t{plate}\t", manifest)
                content_a = (Path(a) / name).read_bytes()
                content_b = (Path(b) / name).read_bytes()
                self.assertEqual(sha256(content_a).hexdigest(), sha256(content_b).hexdigest())
                with Image.open(Path(a) / name) as image:
                    self.assertEqual(image.format, "PNG")
                    self.assertEqual(image.size, module.SIZE)
                    self.assertIsNotNone(image.getbbox())
            clean = Image.open(Path(a) / rows_a[0][0]).convert("RGB")
            blur = Image.open(Path(a) / rows_a[1][0]).convert("RGB")
            self.assertIsNotNone(ImageChops.difference(clean, blur).getbbox())

    def test_plate_characters_and_no_real_claims(self):
        self.assertIn("34ABL489", module.PLATES)
        self.assertIn("06OZ123", module.PLATES)
        self.assertIn("35BI123", module.PLATES)
        self.assertEqual(len(module.PLATES), len(set(module.PLATES)))
        self.assertTrue(all(p.isalnum() and p.isupper() for p in module.PLATES))


if __name__ == "__main__":
    unittest.main()
