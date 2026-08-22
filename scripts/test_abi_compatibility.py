#!/usr/bin/env python3
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).with_name('abi-compatibility.py')
spec = importlib.util.spec_from_file_location('abi_compatibility', MODULE)
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)


class AbiCompatibilityTests(unittest.TestCase):
    def baseline(self):
        return {
            'semanticVersion': '1.2.0',
            'symbols': ['fac_lpr_a_v1'],
            'signatures': {'fac_lpr_a_v1': 'int fac_lpr_a_v1(void)'},
            'layouts': {'fac_lpr_x_v1': {'size': 8, 'align': 4}},
        }

    def test_backward_compatible_symbol_addition(self):
        current = self.baseline()
        current['symbols'] = current['symbols'] + ['fac_lpr_b_v1']
        current['signatures'] = dict(current['signatures'], fac_lpr_b_v1='int fac_lpr_b_v1(void)')
        self.assertEqual(abi.compare(self.baseline(), current), [])

    def test_removed_symbol_is_breaking(self):
        current = self.baseline(); current['symbols'] = []
        self.assertTrue(any('removed exported symbol' in item for item in abi.compare(self.baseline(), current)))

    def test_major_bump_allows_explicit_break(self):
        baseline = self.baseline(); current = self.baseline(); current['semanticVersion'] = '2.0.0'; current['symbols'] = []
        with tempfile.TemporaryDirectory() as root:
            base_path = Path(root) / 'base.json'; current_path = Path(root) / 'current.json'
            base_path.write_text(json.dumps(baseline), encoding='utf-8'); current_path.write_text(json.dumps(current), encoding='utf-8')
            self.assertEqual(abi.enforce(base_path, current_path, None), 0)

    def test_same_major_rejects_break(self):
        baseline = self.baseline(); current = self.baseline(); current['symbols'] = []
        with tempfile.TemporaryDirectory() as root:
            base_path = Path(root) / 'base.json'; current_path = Path(root) / 'current.json'
            base_path.write_text(json.dumps(baseline), encoding='utf-8'); current_path.write_text(json.dumps(current), encoding='utf-8')
            self.assertEqual(abi.enforce(base_path, current_path, None), 2)


if __name__ == '__main__':
    unittest.main()
