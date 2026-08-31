import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class FirstPartyBuildMatrixTests(unittest.TestCase):
    def test_matrix_has_eight_sorted_watchfaces(self):
        import tools.build_first_party as builder
        self.assertEqual(len(builder.FACES), 8)
        ids = [face["id"] for face in builder.FACES]
        outputs = [face["output"] for face in builder.FACES]
        self.assertEqual(ids, sorted(ids))
        self.assertEqual(outputs, sorted(outputs))
        self.assertEqual(len(set(ids)), 8)
        self.assertEqual(len(set(outputs)), 8)
        for face in builder.FACES:
            self.assertEqual(face["abi_major"], 1)
            self.assertEqual(face["abi_minor"], 2)
            self.assertEqual(face["type"], "watchface")
            self.assertEqual(face["capabilities"] & 0x203, 0x203)
            self.assertEqual(face["capabilities"] & ~0x3ff, 0)

    def test_manifest_is_canonical_and_builder_does_not_claim_missing_renderers(self):
        import tools.build_first_party as builder
        for face in builder.FACES:
            self.assertTrue(face["id"].startswith("watchy.firstparty."))
            self.assertTrue(face["output"].endswith(".wpk"))
            self.assertEqual(face["assets"], [])


if __name__ == "__main__":
    unittest.main()
