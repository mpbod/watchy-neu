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
PROVENANCE_LOCK_PATH = ROOT / "tools" / "font_provenance.lock.json"

APPROVED_GLYPHS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz:$%./_#?!+-→—·°"
APPROVED_STRIKES = [
    ("plex_8_semibold", "IBMPlexMono-SemiBold.otf", 8, 128, APPROVED_GLYPHS),
    ("plex_9_semibold", "IBMPlexMono-SemiBold.otf", 9, 128, APPROVED_GLYPHS),
    ("plex_10_semibold", "IBMPlexMono-SemiBold.otf", 10, 128, APPROVED_GLYPHS),
    ("plex_11_semibold", "IBMPlexMono-SemiBold.otf", 11, 128, APPROVED_GLYPHS),
    ("plex_11_regular", "IBMPlexMono-Regular.otf", 11, 128, APPROVED_GLYPHS),
    ("plex_13_semibold", "IBMPlexMono-SemiBold.otf", 13, 128, APPROVED_GLYPHS),
    ("plex_13_regular", "IBMPlexMono-Regular.otf", 13, 128, APPROVED_GLYPHS),
    ("plex_13_medium", "IBMPlexMono-Medium.otf", 13, 128, APPROVED_GLYPHS),
    ("plex_15_medium", "IBMPlexMono-Medium.otf", 15, 128, APPROVED_GLYPHS),
    ("plex_22_bold", "IBMPlexMono-Bold.otf", 22, 128, " 0123456789%"),
    ("plex_30_bold", "IBMPlexMono-Bold.otf", 30, 128, " 0123456789:%"),
    ("plex_32_bold", "IBMPlexMono-Bold.otf", 32, 128, " 0123456789:"),
    ("plex_38_bold", "IBMPlexMono-Bold.otf", 38, 128, " 0123456789:"),
    ("plex_44_bold", "IBMPlexMono-Bold.otf", 44, 128, " 0123456789"),
    ("heros_13_bold", "texgyreheros-bold.otf", 13, 128, APPROVED_GLYPHS),
    ("heros_15_bold", "texgyreheros-bold.otf", 15, 128, APPROVED_GLYPHS),
    ("heros_17_bold", "texgyreheros-bold.otf", 17, 128, APPROVED_GLYPHS),
    ("heros_20_bold", "texgyreheros-bold.otf", 20, 128, APPROVED_GLYPHS),
    ("heros_40_regular", "texgyreheros-regular.otf", 40, 150, " 0123456789:"),
    ("heros_46_regular", "texgyreheros-regular.otf", 46, 150, " 0123456789:"),
    ("heros_60_bold", "texgyreheros-bold.otf", 60, 128, " 0123456789:"),
    ("heros_62_bold", "texgyreheros-bold.otf", 62, 128, " 0123456789:"),
    ("heros_72_bold", "texgyreheros-bold.otf", 72, 128, " 0123456789:"),
    ("heros_82_bold", "texgyreheros-bold.otf", 82, 128, " 0123456789"),
]


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

    def test_config_matches_the_approved_strike_contract(self) -> None:
        config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        actual = [
            (strike["id"], strike["font"], strike["px"], strike["threshold"],
             strike.get("glyphs", config["glyphs"]))
            for strike in config["strikes"]
        ]
        self.assertEqual(config["glyphs"], APPROVED_GLYPHS)
        self.assertEqual(actual, APPROVED_STRIKES)

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

    def test_immutable_provenance_lock_covers_only_the_approved_otfs(self) -> None:
        lock = json.loads(PROVENANCE_LOCK_PATH.read_text(encoding="utf-8"))
        self.assertEqual(lock["version"], 1)
        self.assertEqual(
            [entry["path"] for entry in lock["fonts"]],
            [
                "assets/fonts/ibm-plex-mono/IBMPlexMono-Bold.otf",
                "assets/fonts/ibm-plex-mono/IBMPlexMono-Medium.otf",
                "assets/fonts/ibm-plex-mono/IBMPlexMono-Regular.otf",
                "assets/fonts/ibm-plex-mono/IBMPlexMono-SemiBold.otf",
                "assets/fonts/tex-gyre-heros/texgyreheros-bold.otf",
                "assets/fonts/tex-gyre-heros/texgyreheros-regular.otf",
            ],
        )
        self.generator.verify_provenance_lock(ROOT)
        for entry in lock["fonts"]:
            path = ROOT / entry["path"]
            self.assertTrue(path.is_file())
            self.assertFalse(path.is_symlink())
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), entry["sha256"])
            self.assertTrue(entry["source"]["url"].startswith("https://"))
            self.assertTrue(entry["source"].get("revision") or entry["source"].get("archive_member"))

    def test_provenance_rejects_paths_outside_exact_allowlist(self) -> None:
        with self.assertRaisesRegex(ValueError, "allowlist"):
            self.generator.resolve_locked_font(
                ROOT,
                {"path": "assets/fonts/ibm-plex-mono/unapproved.otf", "sha256": "0" * 64},
            )

    def test_provenance_rejects_a_symlinked_ancestor_directory(self) -> None:
        approved = {"path": "assets/fonts/ibm-plex-mono/IBMPlexMono-Bold.otf"}
        self.assertEqual(
            self.generator.resolve_locked_font(ROOT, approved),
            ROOT / approved["path"],
        )
        with tempfile.TemporaryDirectory() as temporary:
            fixture_root = Path(temporary) / "root"
            outside = Path(temporary) / "outside"
            (outside / "fonts/ibm-plex-mono").mkdir(parents=True)
            (outside / "fonts/ibm-plex-mono/IBMPlexMono-Bold.otf").write_bytes(b"not-a-font")
            fixture_root.mkdir()
            (fixture_root / "assets").symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "symlink"):
                self.generator.resolve_locked_font(fixture_root, approved)

    def test_license_hashes_use_narrow_canonicalization_and_include_lppl(self) -> None:
        self.assertEqual(
            self.generator.canonicalize_license_text("alpha \t\r\nbeta\r\ngamma\t\n"),
            b"alpha\nbeta\ngamma\n",
        )
        self.assertEqual(
            self.generator.canonicalize_license_text("inside\ttext\n"),
            b"inside\ttext\n",
        )
        lock = json.loads(PROVENANCE_LOCK_PATH.read_text(encoding="utf-8"))
        self.assertEqual(
            {entry["path"] for entry in lock["licenses"]},
            {
                "assets/fonts/ibm-plex-mono/OFL.txt",
                "assets/fonts/tex-gyre-heros/GUST-FONT-LICENSE.txt",
                "assets/fonts/tex-gyre-heros/LPPL-1.3c.txt",
            },
        )
        self.generator.verify_license_lock(ROOT)
        lppl = (ROOT / "assets/fonts/tex-gyre-heros/LPPL-1.3c.txt").read_text(encoding="utf-8")
        self.assertIn("LPPL Version 1.3c", lppl)

    def test_configured_codepoints_are_covered_by_each_source_cmap(self) -> None:
        self.generator.validate_configured_codepoints(ROOT)

    def test_abi_range_validator_reports_strike_and_glyph(self) -> None:
        invalid = self.generator.Glyph(0x41, 1 << 32, 0, 0, 1, 1, 1, 1)
        with self.assertRaisesRegex(ValueError, r"test_strike U\+0041 bitmap_offset"):
            self.generator.validate_glyph_abi_range("test_strike", invalid, 1 << 32)
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
