import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR_PATH = ROOT / "tools" / "generate_fonts.py"
CONFIG_PATH = ROOT / "tools" / "font_strikes.json"
HEADER_PATH = ROOT / "sdk" / "ui" / "generated" / "watchy_fonts.h"
SOURCE_PATH = ROOT / "sdk" / "ui" / "generated" / "watchy_fonts.c"


class DeterministicFontTests(unittest.TestCase):
    """Integration contracts for the font handoff consumed by watchfaces."""

    @classmethod
    def setUpClass(cls) -> None:
        spec = importlib.util.spec_from_file_location("generate_fonts", GENERATOR_PATH)
        cls.generator = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        sys.modules[spec.name] = cls.generator
        spec.loader.exec_module(cls.generator)

    def test_configured_strikes_emit_sorted_unique_codepoints(self) -> None:
        config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        expected_ids = [strike["id"] for strike in config["strikes"]]
        self.assertEqual(len(expected_ids), len(set(expected_ids)))
        generated = self.generator.render_outputs(ROOT)
        for strike_id in expected_ids:
            codepoints = self.generator.strike_codepoints(config, strike_id)
            self.assertEqual(codepoints, sorted(set(codepoints)), strike_id)
            self.assertIn(f"watchy_font_{strike_id}".encode("ascii"), generated.source, strike_id)

    def test_generated_files_record_inputs_and_are_byte_stable(self) -> None:
        first = self.generator.render_outputs(ROOT)
        second = self.generator.render_outputs(ROOT)
        self.assertEqual(first.header, second.header)
        self.assertEqual(first.source, second.source)
        self.assertEqual(HEADER_PATH.read_bytes(), first.header)
        self.assertEqual(SOURCE_PATH.read_bytes(), first.source)
        generator_hash = self.generator.sha256(GENERATOR_PATH)
        config_hash = self.generator.sha256(CONFIG_PATH)
        for artifact in (first.header.decode("ascii"), first.source.decode("ascii")):
            self.assertIn(f"generator sha256: {generator_hash}", artifact)
            self.assertIn(f"config sha256: {config_hash}", artifact)
            self.assertNotRegex(artifact, r"20\d\d[-T :]")
        source_hashes = self.generator.source_hashes(ROOT)
        for filename, digest in source_hashes.items():
            self.assertIn(f"{filename} sha256: {digest}", first.source.decode("ascii"))
            self.assertIn(f"{filename} sha256: {digest}", first.header.decode("ascii"))
        self.assertNotIn(str(ROOT), first.source.decode("ascii"))
        self.assertNotIn(str(ROOT), first.header.decode("ascii"))
        self.assertNotIn("Generated on", first.source.decode("ascii"))

    def test_sources_are_resolved_only_from_vendored_font_directories(self) -> None:
        config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        source_paths = {
            self.generator.source_path(ROOT, strike["font"]).relative_to(ROOT)
            for strike in config["strikes"]
        }
        self.assertEqual(
            source_paths,
            {
                Path("assets/fonts/ibm-plex-mono/IBMPlexMono-Bold.otf"),
                Path("assets/fonts/ibm-plex-mono/IBMPlexMono-Medium.otf"),
                Path("assets/fonts/ibm-plex-mono/IBMPlexMono-Regular.otf"),
                Path("assets/fonts/ibm-plex-mono/IBMPlexMono-SemiBold.otf"),
                Path("assets/fonts/tex-gyre-heros/texgyreheros-bold.otf"),
                Path("assets/fonts/tex-gyre-heros/texgyreheros-regular.otf"),
            },
        )
    def test_generator_check_and_generated_c_compile(self) -> None:
        check = subprocess.run(
            [sys.executable, str(GENERATOR_PATH), "--check"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(check.returncode, 0, check.stdout + check.stderr)
        with tempfile.TemporaryDirectory() as temporary:
            object_file = Path(temporary) / "watchy_fonts.o"
            compile_result = subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-c", str(SOURCE_PATH), "-o", str(object_file)],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)

    def test_licenses_are_upstream_license_texts(self) -> None:
        ibm_license = (ROOT / "assets/fonts/ibm-plex-mono/OFL.txt").read_text(encoding="utf-8")
        gust_license = (ROOT / "assets/fonts/tex-gyre-heros/GUST-FONT-LICENSE.txt").read_text(encoding="utf-8")
        self.assertIn("SIL OPEN FONT LICENSE", ibm_license.upper())
        self.assertIn("GUST FONT LICENSE", gust_license.upper())


if __name__ == "__main__":
    unittest.main()
