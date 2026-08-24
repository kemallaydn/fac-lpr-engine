#!/usr/bin/env python3
import importlib.util
import unittest
from pathlib import Path

MODULE = Path(__file__).with_name('production-readiness.py')
spec = importlib.util.spec_from_file_location('production_readiness', MODULE)
readiness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(readiness)


class ProductionReadinessTests(unittest.TestCase):
    def test_all_required_gates_approve_release(self):
        gates = {name: 'success' for name in readiness.REQUIRED_GATES}
        report = readiness.evaluate(gates, 'abc123', '1.0.0')
        self.assertTrue(report['releaseApproved'])
        self.assertEqual(report['failedGates'], [])

    def test_missing_gate_fails_closed(self):
        gates = {name: 'success' for name in readiness.REQUIRED_GATES}
        gates.pop('real-model-integration')
        report = readiness.evaluate(gates, 'abc123', '1.0.0')
        self.assertFalse(report['releaseApproved'])
        self.assertIn('real-model-integration', report['failedGates'])

    def test_skipped_gate_is_not_success(self):
        gates = {name: 'success' for name in readiness.REQUIRED_GATES}
        gates['windows-clean-build'] = 'skipped'
        report = readiness.evaluate(gates, 'abc123', '1.0.0')
        self.assertFalse(report['releaseApproved'])
        self.assertIn('windows-clean-build', report['failedGates'])


if __name__ == '__main__':
    unittest.main()
