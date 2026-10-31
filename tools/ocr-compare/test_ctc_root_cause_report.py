import math
import unittest

import numpy as np

from ctc_root_cause_report import (
    ctc_forward, ctc_log_softmax, ctc_viterbi,
    deleted_from_greedy, describe_case,
)


class RootCauseDiagnosticsTest(unittest.TestCase):
    def test_ctc_forward_sums_all_alignment_paths(self):
        p = np.array([[0.8, 0.1, 0.1], [0.2, 0.1, 0.7]])
        # A,A + A,blank + blank,A
        expected = 0.8 * 0.2 + 0.8 * 0.7 + 0.1 * 0.2
        self.assertAlmostEqual(
            math.exp(ctc_forward(np.log(p), "A", "AB", 2)),
            expected, places=8)

    def test_viterbi_explains_missing_h_without_changing_model(self):
        charset = "0123456789ABCDEFGHIJKLMNOPRSTUVYZ"
        target = "06BH7562"
        logits = np.full((24, 34), -8.0, dtype=np.float32)
        logits[:, 33] = 6.0
        for t, char in zip([1, 3, 5, 7, 9, 11, 13, 15], target):
            logits[t, charset.index(char)] = 9.0 if char != "H" else 5.0
        logp = ctc_log_softmax(logits)
        likelihood, path = ctc_viterbi(logp, target, charset, 33)
        self.assertTrue(math.isfinite(likelihood))
        self.assertEqual(len(path), 24)
        result = describe_case(
            logits, target, "06B7562", charset, 33, "06B7562")
        self.assertEqual(result["deletedPositions"], [3])
        self.assertEqual(result["characters"][3]["char"], "H")
        self.assertEqual(result["characters"][3]["argmaxFrameCount"], 0)
        self.assertTrue(result["characters"][3]["alignedTimesteps"])
        self.assertLess(result["logProbabilityAdvantageCorrectMinusBeam"], 0)

    def test_repeated_characters_do_not_falsely_assign_deleted_position(self):
        self.assertEqual(deleted_from_greedy("AAAB", "AAB"), [])
        self.assertEqual(deleted_from_greedy("ABCD", "AXD"), [])
        self.assertEqual(deleted_from_greedy("ABCDE", "ABCDF"), [])

    def test_invalid_logits_or_alphabet_rejected(self):
        with self.assertRaises(ValueError):
            ctc_log_softmax(np.array([[float("nan"), 0.0]]))
        with self.assertRaises(ValueError):
            ctc_forward(
                np.log(np.array([[0.5, 0.5]])), "Z", "A", 1)


if __name__ == "__main__":
    unittest.main()
