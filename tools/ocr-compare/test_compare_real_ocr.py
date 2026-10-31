"""Pure validation/metrics checks; real-model inference runs in GitHub Actions."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).with_name("compare_real_ocr.py")
spec = importlib.util.spec_from_file_location("compare_real_ocr", MODULE)
assert spec and spec.loader
code = importlib.util.module_from_spec(spec)
spec.loader.exec_module(code)


class RealOcrComparisonTests(unittest.TestCase):
    def test_ctc_repeated_characters_require_blank(self):
        import numpy as np

        data = np.full((1, 34, 24), -10.0, dtype=np.float32)
        data[0, code.BLANK, :] = 10.0
        data[0, 1, 0:2] = 30.0
        data[0, 1, 3:5] = 30.0
        text, confidence = code.decode_greedy(data)
        self.assertEqual(text, "11")
        self.assertEqual(len(confidence), 2)
        self.assertTrue(all(0 <= n <= 1 for n in confidence))

    def test_nonfinite_logits_fail_closed(self):
        import numpy as np

        x = np.zeros((1, 34, 24), dtype=np.float32)
        x[0, 0, 0] = float("nan")
        with self.assertRaises(ValueError):
            code.decode_greedy(x)

    def test_review_never_automatically_accepts_disagreement(self):
        self.assertEqual(
            code.review_evidence(
                {"text": "34ABC123", "characterConfidences": [0.98] * 8},
                {"text": "34A8C123", "confidence": 0.99})["paddleReviewSuggestion"],
            None)
        scores = [0.98] * 8
        scores[3] = 0.2
        review = code.review_evidence(
            {"text": "34ABC123", "characterConfidences": scores},
            {"text": "34A8C123", "confidence": 0.98})
        self.assertTrue(review["reviewRequired"])
        self.assertEqual(review["differentPositionsZeroBased"], [3])
        self.assertEqual(review["paddleReviewSuggestion"], "34A8C123")

    def test_unequal_lengths_never_assign_wrong_character_positions(self):
        review = code.review_evidence(
            {"text": "34ABC123", "characterConfidences": [0.2] * 8},
            {"text": "34AB123", "confidence": 1.0})
        self.assertEqual(review["differentPositionsZeroBased"], [])
        self.assertIsNone(review["paddleReviewSuggestion"])

    def test_accuracy_is_not_confidence_or_agreement(self):
        samples = [
            {"expected": "34ABC123", "lprnet": {"text": "34ABC123"}},
            {"expected": "06AB123", "lprnet": {"text": "06A8123"}},
        ]
        result = code.summarize(samples, "lprnet")
        self.assertEqual(result["exactCount"], 1)
        self.assertEqual(result["total"], 2)
        self.assertEqual(result["exactRate"], 0.5)
        self.assertLess(result["meanNormalizedCharacterAccuracy"], 1.0)

    def test_manifest_missing_or_incomplete_fails(self):
        with tempfile.TemporaryDirectory() as path:
            file = Path(path) / "manifest.tsv"
            file.write_text("abc.png\tANY\t34ABC123\t\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                code.load_manifest(file)

    def test_model_artifact_checksum_is_required(self):
        with tempfile.TemporaryDirectory() as path:
            model = Path(path) / "lprnet.onnx"
            model.write_bytes(b"invalid")
            with self.assertRaises(ValueError):
                code.validate_model(model, {
                    "ocr.size_bytes": "7",
                    "ocr.sha256": "0" * 64,
                    "ocr.charset": code.CHARSET,
                    "ocr.blank_index": str(code.BLANK),
                    "ocr.output_layout": "bct",
                })


if __name__ == "__main__":
    unittest.main()
