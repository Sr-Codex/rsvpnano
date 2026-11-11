"""The committed Phosphor icon masks must match what the generator produces from the vendored SVGs."""
from __future__ import annotations

import unittest

import generate_phosphor_icons as gen


class PhosphorIconsUpToDate(unittest.TestCase):
    def test_generated_files_are_current(self):
        h, c = gen.gerar()
        self.assertEqual(gen.OUT_H.read_text(), h)
        self.assertEqual(gen.OUT_CPP.read_text(), c)


if __name__ == "__main__":
    unittest.main()
