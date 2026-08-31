import json
import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
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
        self.assertEqual(outputs, ["grid-01.wpk", "grid-02.wpk", "grid-03.wpk", "orbit.wpk", "slab.wpk", "term-01.wpk", "term-02.wpk", "term-03.wpk"])
        self.assertEqual(ids, [
            "watchy.firstparty.grid01", "watchy.firstparty.grid02",
            "watchy.firstparty.grid03", "watchy.firstparty.orbit",
            "watchy.firstparty.slab", "watchy.firstparty.term01",
            "watchy.firstparty.term02", "watchy.firstparty.term03",
        ])
        self.assertEqual(len(set(ids)), 8)
        self.assertEqual(len(set(outputs)), 8)
        expected_caps = {
            "grid01": 275, "grid02": 3, "grid03": 3,
            "term01": 259, "term02": 19, "term03": 19,
            "slab": 19, "orbit": 19,
        }
        for face in builder.FACES:
            self.assertEqual(face["abi_major"], 1)
            self.assertEqual(face["abi_minor"], 2)
            self.assertEqual(face["type"], "watchface")
            self.assertEqual(face["capabilities"], expected_caps[face["id"].rsplit(".", 1)[1]])
            self.assertEqual(face["capabilities"] & (1 << 9), 0,
                             "first-party renderers do not use the System API")

    def test_cli_only_dry_run_is_explicit_and_does_not_require_renderer_projects(self):
        import tools.build_first_party as builder
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(builder.main(["--only", "grid-01", "--dry-run"]), 0)
        self.assertIn("grid-01", output.getvalue())

    def test_missing_project_is_a_friendly_builder_error(self):
        import tools.build_first_party as builder
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(builder.BuilderError,
                                        "renderer project is not present"):
                builder.build_faces(output_dir=root / "published",
                                    only=("orbit",), root=root)

    def test_fixture_build_runs_two_clean_rounds_and_promotes_complete_set(self):
        import tools.build_first_party as builder
        from tests.python.test_watchy_pkg import make_xtensa_so
        import tools.watchy_pkg as watchy_pkg

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / "first_party" / "watchfaces" / "grid-01"
            assets = project / "assets"
            assets.mkdir(parents=True)
            tools = root / "tools"
            tools.mkdir()
            (tools / "watchy_pkg.py").write_bytes((ROOT / "tools" / "watchy_pkg.py").read_bytes())
            events = []

            def fixture_runner(command, _cwd):
                command = [str(item) for item in command]
                if "idf.py" in " ".join(command):
                    build_dir = Path(command[command.index("-B") + 1])
                    action = command[-2] if command[-1] == "so" else command[-1]
                    events.append((action, build_dir.name))
                    build_dir.mkdir(parents=True, exist_ok=True)
                    if action == "build":
                        (build_dir / "real-output-name.so").write_bytes(make_xtensa_so())
                    return
                subcommand = command[2] if len(command) > 2 else ""
                events.append((subcommand, Path(command[-1]).name))
                if subcommand == "build":
                    output = Path(command[command.index("--output") + 1])
                    manifest = Path(command[command.index("--manifest") + 1])
                    elf = Path(command[command.index("--elf") + 1])
                    watchy_pkg.build_package(manifest, elf, assets, output)

            with mock.patch.dict("os.environ", {"IDF_PATH": "/fixture-idf"}, clear=False):
                builder.build_faces(output_dir=root / "published", only=("grid-01",),
                                    reproducible=True, runner=fixture_runner, root=root)
            self.assertEqual([event[0] for event in events],
                             ["clean", "build", "audit-elf", "build", "verify",
                              "clean", "build", "audit-elf", "build", "verify"])
            self.assertEqual(sorted(path.name for path in (root / "published").iterdir()),
                             ["grid-01.wpk"])
            self.assertFalse((root / ".published.staging").exists())
            self.assertFalse((root / ".published.work").exists())

    def test_failed_second_round_keeps_previous_complete_set_and_cleans_temps(self):
        import tools.build_first_party as builder
        from tests.python.test_watchy_pkg import make_xtensa_so
        import tools.watchy_pkg as watchy_pkg

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / "first_party" / "watchfaces" / "grid-01"
            assets = project / "assets"
            assets.mkdir(parents=True)
            tools = root / "tools"
            tools.mkdir()
            (tools / "watchy_pkg.py").write_bytes((ROOT / "tools" / "watchy_pkg.py").read_bytes())
            published = root / "published"
            published.mkdir()
            (published / "prior.wpk").write_bytes(b"prior-complete-set")
            events = []

            def fixture_runner(command, _cwd):
                command = [str(item) for item in command]
                if "idf.py" in " ".join(command):
                    build_dir = Path(command[command.index("-B") + 1])
                    action = command[-2] if command[-1] == "so" else command[-1]
                    events.append(action)
                    build_dir.mkdir(parents=True, exist_ok=True)
                    if action == "build":
                        elf = bytearray(make_xtensa_so())
                        if build_dir.name == "round-2": elf[64] ^= 1
                        (build_dir / "real.so").write_bytes(elf)
                    return
                subcommand = command[2]
                if subcommand == "build":
                    output = Path(command[command.index("--output") + 1])
                    watchy_pkg.build_package(Path(command[command.index("--manifest") + 1]),
                                             Path(command[command.index("--elf") + 1]), assets, output)

            with mock.patch.dict("os.environ", {"IDF_PATH": "/fixture-idf"}, clear=False):
                with self.assertRaisesRegex(builder.BuilderError, "non-deterministic"):
                    builder.build_faces(output_dir=published, only=("grid-01",),
                                        reproducible=True, runner=fixture_runner, root=root)
            self.assertEqual((published / "prior.wpk").read_bytes(), b"prior-complete-set")
            self.assertFalse((root / ".published.staging").exists())
            self.assertFalse((root / ".published.work").exists())

    def test_first_promotion_rename_failure_preserves_prior_tree_exactly(self):
        import tools.build_first_party as builder
        from tests.python.test_watchy_pkg import make_xtensa_so

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / "first_party" / "watchfaces" / "grid-01"
            (project / "assets").mkdir(parents=True)
            (root / "tools").mkdir()
            (root / "tools" / "watchy_pkg.py").write_bytes((ROOT / "tools" / "watchy_pkg.py").read_bytes())
            published = root / "published"
            (published / "nested").mkdir(parents=True)
            (published / "prior.wpk").write_bytes(b"prior")
            (published / "nested" / "catalog").write_bytes(b"catalog")
            before = {path.relative_to(published).as_posix(): path.read_bytes()
                      for path in published.rglob("*") if path.is_file()}

            def fixture_runner(command, _cwd):
                command = [str(item) for item in command]
                if "idf.py" in " ".join(command):
                    build_dir = Path(command[command.index("-B") + 1])
                    build_dir.mkdir(parents=True, exist_ok=True)
                    if command[-2] == "build":
                        (build_dir / "real-name.so").write_bytes(make_xtensa_so())
                elif len(command) > 2 and command[2] == "build":
                    Path(command[command.index("--output") + 1]).write_bytes(b"verified-artifact")

            with mock.patch.dict("os.environ", {"IDF_PATH": "/fixture-idf"}, clear=False):
                with mock.patch.object(builder.os, "replace", side_effect=OSError("first rename failed")) as replace:
                    with self.assertRaisesRegex(builder.BuilderError, "atomic promotion failed"):
                        builder.build_faces(output_dir=published, only=("grid-01",),
                                            reproducible=True, runner=fixture_runner, root=root)
            self.assertEqual(replace.call_count, 1)
            after = {path.relative_to(published).as_posix(): path.read_bytes()
                     for path in published.rglob("*") if path.is_file()}
            self.assertEqual(after, before)
            self.assertFalse((root / ".published.staging").exists())
            self.assertFalse((root / ".published.work").exists())
            self.assertFalse(any("previous-" in path.name for path in root.iterdir()))

    def test_second_promotion_rename_failure_restores_prior_tree(self):
        import tools.build_first_party as builder

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staging = root / "staging"
            published = root / "published"
            staging.mkdir()
            published.mkdir()
            (staging / "grid-01.wpk").write_bytes(b"new")
            (published / "grid-01.wpk").write_bytes(b"old")
            real_replace = builder.os.replace
            calls = []

            def fail_second(source, destination):
                calls.append((source, destination))
                if len(calls) == 2:
                    raise OSError("staging rename failed")
                return real_replace(source, destination)

            with mock.patch.object(builder.os, "replace", side_effect=fail_second):
                with self.assertRaisesRegex(builder.BuilderError, "atomic promotion failed"):
                    builder._promote_complete_set(
                        staging, published, [{"output": "grid-01.wpk"}])
            self.assertEqual((published / "grid-01.wpk").read_bytes(), b"old")
            self.assertEqual(len(calls), 3)  # move old, fail new, restore old
            self.assertFalse(any("previous-" in path.name for path in root.iterdir()))

    def test_manifest_is_canonical_and_builder_does_not_claim_missing_renderers(self):
        import tools.build_first_party as builder
        for face in builder.FACES:
            self.assertTrue(face["id"].startswith("watchy.firstparty."))
            self.assertTrue(face["output"].endswith(".wpk"))
            self.assertEqual(face["assets"], [])


if __name__ == "__main__":
    unittest.main()
