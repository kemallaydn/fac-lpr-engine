#!/usr/bin/env python3
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

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


    def test_multiple_accepted_results_count_each_false_accept(self):
        rows = [{
            'filename': 'multi.jpg',
            'expected_status': 'ACCEPTED',
            'required': ['34ABC123'],
            'optional': [],
        }]
        output = {
            'recognitions': [
                {'status': 'ACCEPTED', 'plate': '34ABC123', 'geometryScore': 1.0},
                {'status': 'ACCEPTED', 'plate': '06WRONG', 'geometryScore': 1.0},
                {'status': 'ACCEPTED', 'plate': '07WRONG', 'geometryScore': 1.0},
                {'status': 'REVIEW', 'plate': '34ABC123'},
                {'status': 'REVIEW', 'plate': '08WRONG'},
            ],
            'totalLatencyMs': 10.0,
        }
        with patch.object(evaluate, 'run_cli', return_value=output):
            report = evaluate.score_run(
                'candidate', rows, Path('.'), Path('cli'),
                Path('models'), Path('contract'), 'v1',
            )
        self.assertEqual(report['metrics']['falseAcceptCount'], 2)
        self.assertEqual(report['metrics']['falseReviewCount'], 1)
        self.assertEqual(report['metrics']['exactPlateAccuracy'], 1.0)

    def test_correct_accepted_plate_does_not_hide_wrong_accepted_plate(self):
        rows = [{
            'filename': 'multi.jpg', 'expected_status': 'ACCEPTED',
            'required': ['34ABC123'], 'optional': ['06DEF456'],
        }]
        output = {
            'recognitions': [
                {'status': 'ACCEPTED', 'plate': '34ABC123'},
                {'status': 'ACCEPTED', 'plate': '06DEF456'},
                {'status': 'ACCEPTED', 'plate': '35BAD999'},
            ],
            'totalLatencyMs': 10.0,
        }
        with patch.object(evaluate, 'run_cli', return_value=output):
            report = evaluate.score_run(
                'candidate', rows, Path('.'), Path('cli'),
                Path('models'), Path('contract'), 'v1',
            )
        self.assertEqual(report['metrics']['falseAcceptCount'], 1)



    def test_multi_plate_accuracy_counts_all_required_plates(self):
        rows = [{
            'filename': 'multi.jpg', 'expected_status': 'ACCEPTED',
            'required': ['34ABC123', '06DEF456', '35XYZ789'], 'optional': [],
        }]
        output = {
            'recognitions': [
                {'plate': '07WRONG', 'status': 'ACCEPTED'},
                {'plate': '34ABC123', 'status': 'ACCEPTED'},
                {'plate': '06DEF456', 'status': 'ACCEPTED'},
            ],
            'totalLatencyMs': 10.0,
        }
        with patch.object(evaluate, 'run_cli', return_value=output):
            report = evaluate.score_run(
                'candidate', rows, Path('.'), Path('cli'),
                Path('models'), Path('contract'), 'v1',
            )
        metrics = report['metrics']
        self.assertEqual(metrics['requiredPlateCount'], 3)
        self.assertAlmostEqual(metrics['exactPlateAccuracy'], 2 / 3)
        self.assertEqual(metrics['falseAcceptCount'], 1)

    def test_duplicate_predictions_cannot_satisfy_multiple_required_plates(self):
        rows = [{
            'filename': 'duplicates.jpg', 'expected_status': 'ACCEPTED',
            'required': ['34ABC123', '34ABC123'], 'optional': [],
        }]
        output = {
            'recognitions': [{'plate': '34ABC123', 'status': 'ACCEPTED'}],
            'totalLatencyMs': 10.0,
        }
        with patch.object(evaluate, 'run_cli', return_value=output):
            report = evaluate.score_run(
                'candidate', rows, Path('.'), Path('cli'),
                Path('models'), Path('contract'), 'v1',
            )
        self.assertEqual(report['metrics']['requiredPlateCount'], 2)
        self.assertAlmostEqual(report['metrics']['exactPlateAccuracy'], 0.5)


if __name__ == '__main__':
    unittest.main()
