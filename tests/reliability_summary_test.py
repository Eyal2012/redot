"""Measurement arithmetic tests; no application or device interaction."""
import importlib.util
import unittest
from pathlib import Path

path=Path(__file__).resolve().parents[1]/'tools/summarize_reliability.py'
spec=importlib.util.spec_from_file_location('summary',path)
summary=importlib.util.module_from_spec(spec);spec.loader.exec_module(summary)

class Arithmetic(unittest.TestCase):
    def test_linear_slope_and_plateau(self):
        rows=[{'seconds':str(i),'private_bytes':str(100+17*i)} for i in range(5)]
        self.assertEqual(summary.fit(rows,'private_bytes'),17)
        self.assertEqual(summary.fit([dict(r,private_bytes='400') for r in rows],'private_bytes'),0)
    def test_insufficient_is_unknown(self):
        self.assertIsNone(summary.fit([],'private_bytes'))
        self.assertIsNone(summary.fit([{'seconds':'0','private_bytes':'12'}],'private_bytes'))
    def test_percentile_interpolation(self):
        self.assertEqual(summary.quantile([0,10,20,30],.5),15)
        self.assertIsNone(summary.quantile([], .95))

if __name__=='__main__':unittest.main()
