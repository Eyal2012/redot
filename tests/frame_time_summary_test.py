"""Arithmetic regression only; no gameplay capture or input."""
import unittest
from summarize_game_frame_times import metrics

class FrameTimes(unittest.TestCase):
    def test_constant(self):
        m=metrics([5.0]*2000)
        self.assertEqual(m['fps'],200)
        self.assertEqual(m['low'],200)
        self.assertEqual(m['p99'],5)
    def test_slow_tail(self):
        m=metrics([5.0]*1980+[20.0]*20)
        self.assertAlmostEqual(m['fps'],1000/5.15)
        self.assertEqual(m['low'],50)
    def test_small_sample(self):
        self.assertIsNone(metrics([10.0]*100)['low'])

if __name__=='__main__':unittest.main()
