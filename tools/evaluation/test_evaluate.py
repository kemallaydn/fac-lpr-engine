#!/usr/bin/env python3
import json
import tempfile
import unittest
from pathlib import Path

import evaluate


class EvaluationTests(unittest.TestCase):
    def test_levenshtein(self):
        self.assertEqual(evaluate.levenshtein('34ABC123', '34ABC123'), 0)
        self.assertEqual(evaluate.levenshtein('34ABC123', '34ABC124'), 1)

    def test_manifest_requires_rows(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'manifest.tsv'
            path.write_text('# only comments\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                evaluate.load_manifest(path)

    def test_delta_reports_fail_closed_regression(self):
        base = {
            'metrics': {
                'exactPlateAccuracy': 0.8,
                'characterAccuracy': 0.9,
                'detectionRecall': 0.9,
                'alignmentSuccess': 0.8,
                'falseAcceptCount': 0,
                'falseReviewCount': 1,
                'latencyMs': {'mean': 10.0},
            }
        }
        candidate = json.loads(json.dumps(base))
        candidate['metrics']['falseAcceptCount'] = 2
        candidate['metrics']['exactPlateAccuracy'] = 0.9
        candidate['metrics']['latencyMs']['mean'] = 12.0
        result = evaluate.delta(base, candidate)
        self.assertEqual(result['falseAcceptCount'], 2)
        self.assertAlmostEqual(result['exactPlateAccuracy'], 0.1)
        self.assertAlmostEqual(result['meanLatencyMs'], 2.0)

    def test_markdown_contains_safety_metrics(self):
        run = {
            'version': 'v1',
            'metrics': {
                'exactPlateAccuracy': 1.0,
                'characterAccuracy': 1.0,
                'detectionRecall': 1.0,
                'alignmentSuccess': 1.0,
                'falseAcceptCount': 0,
                'falseReviewCount': 0,
            },
        }
        report = {
            'dataset': 'golden.tsv',
            'runs': [run, run],
            'deltaBMinusA': {
                'exactPlateAccuracy': 0.0,
                'characterAccuracy': 0.0,
                'detectionRecall': 0.0,
                'alignmentSuccess': 0.0,
                'falseAcceptCount': 0,
                'falseReviewCount': 0,
            },
        }
        text = evaluate.render_markdown(report)
        self.assertIn('False accepted', text)
        self.assertIn('Exact plate accuracy', text)


if __name__ == '__main__':
    unittest.main()
