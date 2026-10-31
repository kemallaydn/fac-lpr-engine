"""General CTC root-cause diagnostics tests, independent of real plate fixtures."""
import importlib.util
from pathlib import Path
import unittest
import numpy as np

SCRIPT=Path(__file__).with_name("ctc_error_diagnostics.py")
spec=importlib.util.spec_from_file_location("ctc_error_diagnostics",SCRIPT)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

CHARSET="0123456789ABCDEFGHIJKLMNOPRSTUVYZ"
BLANK=len(CHARSET)

def logits_for(text="34ABC123",weak=None):
    values=np.full((24,len(CHARSET)+1),-11.0,dtype=np.float32)
    values[:,BLANK]=9.0
    for i,c in enumerate(text):
        values[2*i+1,CHARSET.index(c)]=5.0 if c==weak else 11.0
    return values

class CtcDiagnosticsTests(unittest.TestCase):
    def test_blank_wins_when_true_character_is_omitted(self):
        values=logits_for(weak="B")
        self.assertEqual(module.greedy_text(values,CHARSET,BLANK),"34AC123")
        ops=module.edit_operations("34ABC123","34AC123")
        self.assertEqual([o["expected"] for o in ops if o["type"]=="deletion"],["B"])
        aligned=module.reference_alignment(values,"34ABC123",CHARSET,BLANK)
        missing=aligned["characters"][3]
        self.assertEqual(missing["topTokenAtAlignment"],"<blank>")
        self.assertGreater(missing["blankProbabilityAtAlignment"],missing["targetProbabilityAtAlignment"])
        self.assertFalse(missing["targetTop1AtAnyAlignedStep"])

    def test_correct_character_ranks_first(self):
        values=logits_for()
        self.assertEqual(module.greedy_text(values,CHARSET,BLANK),"34ABC123")
        aligned=module.reference_alignment(values,"34ABC123",CHARSET,BLANK)
        self.assertEqual(aligned["characters"][3]["targetRankAtAlignment"],1)

    def test_repeated_characters_need_intervening_blank(self):
        values=np.full((4,len(CHARSET)+1),-7.0,dtype=np.float32)
        values[:,BLANK]=5.0
        values[0,CHARSET.index("1")]=8.0
        values[2,CHARSET.index("1")]=8.0
        self.assertEqual(module.greedy_text(values,CHARSET,BLANK),"11")
        aligned=module.reference_alignment(values,"11",CHARSET,BLANK)
        self.assertLess(aligned["characters"][0]["bestAlignedTimestep"],
                        aligned["characters"][1]["bestAlignedTimestep"])
        with self.assertRaises(ValueError):
            module.reference_alignment(np.ones((2,34)), "11", CHARSET, BLANK)

    def test_invalid_logits_and_characters_fail_closed(self):
        with self.assertRaises(ValueError):
            module.reference_alignment(np.zeros((2,34)),"ABC",CHARSET,BLANK)
        with self.assertRaises(ValueError):
            module.reference_alignment(np.zeros((24,34)),"Q",CHARSET,BLANK)
        with self.assertRaises(ValueError):
            module.reference_alignment(np.full((24,34),np.nan),"1",CHARSET,BLANK)

    def test_edits_include_insertion_and_substitution(self):
        ops=module.edit_operations("34ABC123","334ABC123")
        self.assertEqual(ops[0]["type"],"insertion")
        ops=module.edit_operations("34ABC123","34ABC128")
        self.assertEqual(ops[-1]["type"],"substitution")

if __name__=="__main__":
    unittest.main()
