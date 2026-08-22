#!/usr/bin/env python3
import importlib.util
import unittest
from pathlib import Path

MODULE = Path(__file__).with_name('release-readiness-report.py')
spec = importlib.util.spec_from_file_location('readiness', MODULE)
readiness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(readiness)


class ReleaseReadinessTests(unittest.TestCase):
    def valid(self):
        return {
            'releaseCommit': 'abc123',
            'workflows': {name: 'success' for name in readiness.REQUIRED_GATES},
            'artifacts': ['checksum','provenance','sbom','third-party','package-smoke'],
            'realModelIntegrationPassed': True,
            'goldenRegressionPassed': True,
            'memoryLongRunPassed': True,
            'windowsCleanBuildPassed': True,
            'linuxCleanBuildPassed': True,
        }

    def test_all_critical_evidence_passes(self):
        report = readiness.evaluate(self.valid())
        self.assertTrue(report['ready'])
        self.assertEqual([], report['blockers'])

    def test_any_failed_gate_blocks_release(self):
        data = self.valid(); data['workflows']['abi-compatibility'] = 'failure'
        report = readiness.evaluate(data)
        self.assertFalse(report['ready'])
        self.assertIn('gate:abi-compatibility', report['blockers'])

    def test_real_model_and_packaging_evidence_are_mandatory(self):
        data = self.valid(); data['realModelIntegrationPassed'] = False; data['artifacts'] = []
        report = readiness.evaluate(data)
        self.assertFalse(report['ready'])
        self.assertIn('real-model-integration', report['blockers'])
        self.assertTrue(any(x.startswith('artifact:') for x in report['blockers']))


if __name__ == '__main__':
    unittest.main()
