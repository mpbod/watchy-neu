import contextlib
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIRST_PARTY = ROOT / "build" / "first-party"

EXPECTED_DIGESTS = {
    "grid-01.wpk": "0a224c7b7aeb574b84918c206bf117a98d2c742399d8dcfcb8e59a9b5fee5475",
    "grid-02.wpk": "a8bba8a3fc1055ee2dc5eff545d02b6e37330d2c70714bbfe173c0b56b363a2c",
    "grid-03.wpk": "8525387dfadf8bc4907a9e00b2cc07c86b274a11d234f8ac99d0da3c1ad84b57",
    "orbit.wpk": "2dc17e53213d28fc9c36a8d2e44ebed396af25c79d944fa64ac0e5cdaa9b17ae",
    "slab.wpk": "bb6e9aec7b5952fb3b1128ee703366e171d269870fb3d1b88af1008faf4d107d",
    "term-01.wpk": "96b2ac789bed3ecffca32bbe2e6077d47c25d499d0b75150d31571b3b7af9ca5",
    "term-02.wpk": "47a9f9f3df367e406794afbad34c23c2ee9b615b0c9f2e5e81823b203465cca5",
    "term-03.wpk": "e91801f680b22aba1191f5b14a8d7202757fb4141c25b9881de432d2cf8a3090",
}


def copy_packages(destination: Path) -> None:
    destination.mkdir(parents=True)
    for name in EXPECTED_DIGESTS:
        shutil.copyfile(FIRST_PARTY / name, destination / name)


class FactorySeedCatalogTests(unittest.TestCase):
    def test_catalog_is_the_exact_sorted_fixed_wfs1_table(self):
        import tools.build_factory_seed as seed
        wire = seed.build_catalog(FIRST_PARTY)
        self.assertEqual(len(wire), 8 + 8 * 64)
        self.assertEqual(wire[:8], b"WFS1\x01\x00\x08\x00")
        expected = bytearray(wire[:8])
        for name, digest in EXPECTED_DIGESTS.items():
            expected.extend(name.encode("ascii") + bytes(32 - len(name)))
            expected.extend(bytes.fromhex(digest))
        self.assertEqual(wire, bytes(expected))
        self.assertEqual(seed.AUDITED_WPK_SHA256, EXPECTED_DIGESTS)

    def test_catalog_contains_exact_audited_wpk_hashes_and_no_time_fields(self):
        import tools.build_factory_seed as seed
        wire = seed.build_catalog(FIRST_PARTY)
        self.assertNotIn(b"2026", wire)
        for index, (name, digest) in enumerate(EXPECTED_DIGESTS.items()):
            record = wire[8 + index * 64:8 + (index + 1) * 64]
            self.assertEqual(record[:32].rstrip(b"\0").decode("ascii"), name)
            self.assertEqual(record[32:].hex(), digest)
            self.assertEqual(hashlib.sha256((FIRST_PARTY / name).read_bytes()).hexdigest(), digest)

    def test_missing_extra_symlink_and_digest_changed_packages_fail_closed(self):
        import tools.build_factory_seed as seed
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "packages"
            copy_packages(source)
            (source / "term-03.wpk").unlink()
            with self.assertRaisesRegex(seed.SeedError, "missing"):
                seed.build_catalog(source)
            shutil.copyfile(FIRST_PARTY / "term-03.wpk", source / "term-03.wpk")
            (source / "extra.wpk").write_bytes(b"extra")
            with self.assertRaisesRegex(seed.SeedError, "unexpected"):
                seed.build_catalog(source)
            (source / "extra.wpk").unlink()
            content = bytearray((source / "orbit.wpk").read_bytes())
            content[-1] ^= 1
            (source / "orbit.wpk").write_bytes(content)
            with self.assertRaisesRegex(seed.SeedError, "digest"):
                seed.build_catalog(source)
            (source / "orbit.wpk").unlink()
            (source / "orbit.wpk").symlink_to(FIRST_PARTY / "orbit.wpk")
            with self.assertRaisesRegex(seed.SeedError, "regular"):
                seed.build_catalog(source)

    def test_staging_tree_is_canonical_reproducible_and_below_partition_capacity(self):
        import tools.build_factory_seed as seed
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first"
            second = Path(directory) / "second"
            one = seed._write_new_staging_tree(FIRST_PARTY, first)
            two = seed._write_new_staging_tree(FIRST_PARTY, second)
            expected_paths = ["factory/seed.bin"] + [f"factory/{name}" for name in EXPECTED_DIGESTS]
            self.assertEqual(sorted(path.relative_to(first).as_posix()
                                    for path in first.rglob("*") if path.is_file()),
                             sorted(expected_paths))
            first_files = {path.relative_to(first).as_posix(): path.read_bytes()
                           for path in first.rglob("*") if path.is_file()}
            second_files = {path.relative_to(second).as_posix(): path.read_bytes()
                            for path in second.rglob("*") if path.is_file()}
            self.assertEqual(first_files, second_files)
            self.assertEqual(one, two)
            self.assertLess(one.content_bytes, seed.LITTLEFS_PARTITION_SIZE)
            self.assertEqual(one.catalog_sha256, hashlib.sha256(first_files["factory/seed.bin"]).hexdigest())

    def test_reproducible_builder_requires_identical_bounded_images(self):
        import tools.build_factory_seed as seed
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            calls = []

            def image_builder(staging, image):
                calls.append(staging)
                image.write_bytes(b"LFS" + (staging / "factory" / "seed.bin").read_bytes())

            result = seed._build_factory_seed_owned(
                source_dir=FIRST_PARTY, staging_dir=root / "stage",
                image_path=root / "littlefs.bin", staging_sentinel=root / "stage.owner",
                image_sentinel=root / "image.owner", reproducible=True,
                image_builder=image_builder)
            self.assertEqual(len(calls), 2)
            self.assertEqual(result.image_size, 3 + 520)
            self.assertEqual(result.image_sha256,
                             hashlib.sha256((root / "littlefs.bin").read_bytes()).hexdigest())

            calls.clear()
            def changing_builder(_staging, image):
                image.write_bytes(bytes([len(calls)]))
                calls.append(image)
            with self.assertRaisesRegex(seed.SeedError, "not reproducible"):
                seed._build_factory_seed_owned(
                    source_dir=FIRST_PARTY, staging_dir=root / "stage",
                    image_path=root / "littlefs.bin", staging_sentinel=root / "stage.owner",
                    image_sentinel=root / "image.owner", reproducible=True,
                    image_builder=changing_builder)
            self.assertEqual((root / "littlefs.bin").read_bytes(),
                             b"LFS" + (root / "stage" / "factory" / "seed.bin").read_bytes())

    def test_builder_rejects_oversize_or_missing_image_output(self):
        import tools.build_factory_seed as seed
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def oversized(_staging, image):
                with image.open("wb") as output:
                    output.truncate(seed.LITTLEFS_PARTITION_SIZE + 1)
            with self.assertRaisesRegex(seed.SeedError, "exceeds"):
                seed._build_factory_seed_owned(
                    FIRST_PARTY, root / "stage", root / "image.bin",
                    root / "stage.owner", root / "image.owner", image_builder=oversized)
            with self.assertRaisesRegex(seed.SeedError, "did not produce"):
                seed._build_factory_seed_owned(
                    FIRST_PARTY, root / "stage", root / "image.bin",
                    root / "stage.owner", root / "image.owner",
                    image_builder=lambda _stage, _image: None)

    def test_rejected_broad_arbitrary_and_symlink_staging_paths_are_untouched(self):
        import tools.build_factory_seed as seed
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arbitrary = root / "existing"
            (arbitrary / "nested").mkdir(parents=True)
            (arbitrary / "nested" / "keep.bin").write_bytes(b"must-survive")
            link = root / "staging-link"
            link.symlink_to(arbitrary, target_is_directory=True)
            repo_probe = (ROOT / "CMakeLists.txt").read_bytes()
            workspace_probe = (seed.WORKSPACE_ROOT / "watchy-fw" / "partitions.csv").read_bytes()
            arbitrary_before = {
                path.relative_to(arbitrary).as_posix(): path.read_bytes()
                for path in arbitrary.rglob("*") if path.is_file()
            }
            for unsafe in (ROOT, seed.WORKSPACE_ROOT, arbitrary, link):
                called = []
                with self.assertRaisesRegex(seed.SeedError, "unsupported|unsafe|owned"):
                    seed.build_factory_seed(
                        FIRST_PARTY, unsafe, seed.DEFAULT_IMAGE,
                        image_builder=lambda *_args: called.append(True))
                self.assertEqual(called, [])
            self.assertEqual((ROOT / "CMakeLists.txt").read_bytes(), repo_probe)
            self.assertEqual(
                (seed.WORKSPACE_ROOT / "watchy-fw" / "partitions.csv").read_bytes(),
                workspace_probe)
            self.assertTrue(link.is_symlink())
            self.assertEqual({
                path.relative_to(arbitrary).as_posix(): path.read_bytes()
                for path in arbitrary.rglob("*") if path.is_file()
            }, arbitrary_before)

    def test_rejected_existing_and_nondefault_image_paths_are_never_unlinked(self):
        import tools.build_factory_seed as seed
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            existing = root / "do-not-delete.bin"
            existing.write_bytes(b"irreplaceable")
            linked_target = root / "linked-target.bin"
            linked_target.write_bytes(b"linked-irreplaceable")
            linked = root / "image-link.bin"
            linked.symlink_to(linked_target)
            staged_seed = seed.DEFAULT_STAGING / "factory" / "seed.bin"
            staging_existed = seed.DEFAULT_STAGING.exists()
            staging_probe = staged_seed.read_bytes() if staged_seed.is_file() else None
            for unsafe in (existing, linked, root / "unsupported.bin", ROOT, seed.WORKSPACE_ROOT):
                called = []
                with self.assertRaisesRegex(seed.SeedError, "unsupported|unsafe"):
                    seed.build_factory_seed(
                        FIRST_PARTY, seed.DEFAULT_STAGING, unsafe,
                        image_builder=lambda *_args: called.append(True))
                self.assertEqual(called, [])
            self.assertEqual(existing.read_bytes(), b"irreplaceable")
            self.assertTrue(linked.is_symlink())
            self.assertEqual(linked_target.read_bytes(), b"linked-irreplaceable")
            self.assertEqual(seed.DEFAULT_STAGING.exists(), staging_existed)
            if staging_probe is not None:
                self.assertEqual(staged_seed.read_bytes(), staging_probe)

    def test_existing_default_staging_requires_owner_or_exact_canonical_content(self):
        import tools.build_factory_seed as seed
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staging = root / "factory_seed"
            staging.mkdir()
            (staging / "personal.txt").write_bytes(b"personal")
            sentinel = root / ".factory_seed.watchy-owned"
            with self.assertRaisesRegex(seed.SeedError, "not tool-owned"):
                seed._validate_existing_staging(staging, sentinel, FIRST_PARTY)
            self.assertEqual((staging / "personal.txt").read_bytes(), b"personal")

            shutil.rmtree(staging)
            seed._write_new_staging_tree(FIRST_PARTY, staging)
            seed._validate_existing_staging(staging, sentinel, FIRST_PARTY)
            self.assertFalse(sentinel.exists())  # validation itself never mutates

    def test_cli_rejects_overrides_before_mutating_any_destination(self):
        import tools.build_factory_seed as seed
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            existing = root / "existing"
            existing.mkdir()
            canary = existing / "canary"
            canary.write_bytes(b"unchanged")
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                self.assertEqual(seed.main(["--staging-dir", str(existing)]), 2)
            self.assertRegex(stderr.getvalue(), "unsupported|unsafe")
            self.assertEqual(canary.read_bytes(), b"unchanged")
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                self.assertEqual(seed.main(["--source-dir", str(existing)]), 2)
            self.assertRegex(stderr.getvalue(), "unsupported|unsafe")
            self.assertEqual(canary.read_bytes(), b"unchanged")


class FactoryFlashSafetyTests(unittest.TestCase):
    def write_partition_table(self, root: Path, offset="0x1d0000", size="0x230000") -> Path:
        table = root / "partitions.csv"
        table.write_text(
            "# Name,Type,SubType,Offset,Size,Flags\n"
            "nvs,data,nvs,0x9000,0x6000,\n"
            "phy_init,data,phy,0xf000,0x1000,\n"
            "factory,app,factory,0x10000,0x1c0000,\n"
            f"littlefs,data,littlefs,{offset},{size},\n", encoding="utf-8")
        return table

    def write_flash_images(self, root: Path, *, partition_bytes=None) -> Path:
        import tools.flash_factory as flash
        build = root / "watchy_v2"
        build.mkdir()
        (build / "bootloader.bin").write_bytes(b"bootloader")
        (build / "partitions.bin").write_bytes(
            flash.EXPECTED_PARTITION_BINARY if partition_bytes is None else partition_bytes)
        (build / "firmware.bin").write_bytes(b"firmware")
        return build

    def write_seed_image(self, root: Path) -> Path:
        import tools.flash_factory as flash
        image = root / "littlefs.bin"
        with image.open("wb") as output:
            output.truncate(flash.EXPECTED_SIZE)
        return image

    def test_partition_parser_requires_the_exact_named_range(self):
        import tools.flash_factory as flash
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(flash.validate_partition_table(self.write_partition_table(root)),
                             (0x1D0000, 0x230000))
            with self.assertRaisesRegex(flash.FlashSafetyError, "offset"):
                flash.validate_partition_table(self.write_partition_table(root, offset="0x1c0000"))
            with self.assertRaisesRegex(flash.FlashSafetyError, "size"):
                flash.validate_partition_table(self.write_partition_table(root, size="0x220000"))
            table = self.write_partition_table(root)
            table.write_text(table.read_text().replace("0x6000", "0x5000"), encoding="utf-8")
            with self.assertRaisesRegex(flash.FlashSafetyError, "nvs"):
                flash.validate_partition_table(table)
            table = self.write_partition_table(root)
            table.write_text(table.read_text() + "extra,data,nvs,0x400000,0x1000,\n",
                             encoding="utf-8")
            with self.assertRaisesRegex(flash.FlashSafetyError, "exactly"):
                flash.validate_partition_table(table)

    def test_fake_serial_run_builds_then_erases_only_exact_nvs_before_all_images(self):
        import tools.flash_factory as flash
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            table = self.write_partition_table(root)
            image = self.write_seed_image(root)
            build = self.write_flash_images(root)
            commands = []
            messages = []

            def fake_runner(command, **kwargs):
                commands.append([str(item) for item in command])
                if command[1:4] == ["device", "list", "--json-output"]:
                    stdout = json.dumps([{"port": "/dev/fake-watchy", "description": "USB Serial",
                                          "hwid": "USB VID:PID=1A86:55D4 SER=ABC"}])
                elif "chip-id" in command:
                    stdout = "Chip is ESP32-PICO-D4 (revision v1.0)"
                else:
                    stdout = ""
                return subprocess.CompletedProcess(command, 0, stdout=stdout, stderr="")

            result = flash.flash_factory(
                port="/dev/fake-watchy", image=image, partition_table=table,
                runner=fake_runner, printer=messages.append,
                platformio="platformio", python="python3", build_dir=build)
            self.assertEqual(result, hashlib.sha256(image.read_bytes()).hexdigest())
            self.assertIn(result, messages[0])  # digest is emitted before any write command
            self.assertEqual(commands[0], ["platformio", "run", "-e", "watchy_v2"])
            self.assertEqual(commands[1], ["platformio", "device", "list", "--json-output"])
            self.assertIn("chip-id", commands[2])
            self.assertEqual(commands[3][-3:], ["erase-region", "0x9000", "0x6000"])
            self.assertEqual(commands[4][-9:], [
                "write-flash",
                "0x1000", str((build / "bootloader.bin").resolve()),
                "0x8000", str((build / "partitions.bin").resolve()),
                "0x10000", str((build / "firmware.bin").resolve()),
                "0x1d0000", str(image.resolve()),
            ])
            self.assertFalse(any("erase-flash" in command for command in commands))

    def test_chip_identity_accepts_complete_classic_set_and_rejects_new_families(self):
        import tools.flash_factory as flash
        classics = (
            "Chip is ESP32 (revision v1.0)",
            "Chip type: ESP32-D0WDQ6 (revision v1.0)",
            "Chip type: ESP32-D0WD-V3 (revision v3.1)",
            "Chip is ESP32-PICO-D4 (revision v1.0)",
            "Chip type: ESP32-PICO-V3 (revision v3.0)",
            "Chip type: ESP32-PICO-V3-02 (revision v3.1)",
            "Chip is ESP32-U4WDH (revision v3.1)",
        )
        for identity in classics:
            flash._validate_chip(identity)
        for family in ("ESP32-S2", "ESP32-S3", "ESP32-C2", "ESP32-C3",
                       "ESP32-C6", "ESP32-H2", "ESP32-P4"):
            with self.subTest(family=family):
                with self.assertRaisesRegex(flash.FlashSafetyError, "classic ESP32"):
                    flash._validate_chip(f"Chip type: {family} (revision v1.0)")

    def test_chip_identity_rejects_every_unlisted_or_malformed_pico_name(self):
        import tools.flash_factory as flash
        unsupported = (
            "ESP32-PICO-S3",
            "ESP32-PICO-P4",
            "ESP32-PICO-C3",
            "ESP32-PICO-D4-EXTRA",
            "ESP32-PICO-V3-02-EXTRA",
            "ESP32-PICO-ARBITRARY",
            "ESP32-PICO-",
            "ESP32-PICO",
            "ESP32-PICO--",
        )
        for identity in unsupported:
            with self.subTest(identity=identity):
                with self.assertRaisesRegex(flash.FlashSafetyError, "classic ESP32"):
                    flash._validate_chip(f"Chip type: {identity} (revision v1.0)")

    def test_flash_rejects_oversize_undiscovered_or_wrong_chip_before_upload(self):
        import tools.flash_factory as flash
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            table = self.write_partition_table(root)
            build = self.write_flash_images(root)
            image = root / "littlefs.bin"
            with image.open("wb") as output:
                output.truncate(0x230001)
            called = []
            with self.assertRaisesRegex(flash.FlashSafetyError, "exceeds"):
                flash.flash_factory("/dev/fake", image, table,
                                    runner=lambda *a, **k: called.append(a), build_dir=build)
            self.assertEqual(called, [])

            image = self.write_seed_image(root)
            def no_device(command, **_kwargs):
                return subprocess.CompletedProcess(command, 0, stdout="[]", stderr="")
            with self.assertRaisesRegex(flash.FlashSafetyError, "not discovered"):
                flash.flash_factory("/dev/fake", image, table, runner=no_device,
                                    build_dir=build)

            commands = []
            def wrong_chip(command, **_kwargs):
                commands.append(command)
                stdout = (json.dumps([{"port": "/dev/fake", "description": "USB", "hwid": "USB SER=X"}])
                          if "device" in command else "Chip type: ESP32-S3")
                return subprocess.CompletedProcess(command, 0, stdout=stdout, stderr="")
            with self.assertRaisesRegex(flash.FlashSafetyError, "ESP32 target"):
                flash.flash_factory("/dev/fake", image, table, runner=wrong_chip,
                                    build_dir=build)
            self.assertFalse(any("erase-region" in command or "write-flash" in command
                                 for command in commands))

    def test_corrupt_partition_image_fails_preflight_without_device_mutation(self):
        import tools.flash_factory as flash
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            table = self.write_partition_table(root)
            image = self.write_seed_image(root)
            build = self.write_flash_images(root, partition_bytes=b"wrong partition image")
            commands = []

            def fake_runner(command, **_kwargs):
                commands.append([str(item) for item in command])
                return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

            with self.assertRaisesRegex(flash.FlashSafetyError, "partition image"):
                flash.flash_factory("/dev/fake", image, table, runner=fake_runner,
                                    build_dir=build)
            self.assertEqual(len(commands), 1)
            self.assertEqual(commands[0][1:], ["run", "-e", "watchy_v2"])
            self.assertEqual(Path(commands[0][0]).name, "platformio")
            self.assertFalse(any("erase-region" in command or "write-flash" in command
                                 for command in commands))

    def test_dry_run_prints_safe_commands_and_executes_nothing(self):
        import tools.flash_factory as flash
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            table = self.write_partition_table(root)
            image = self.write_seed_image(root)
            build = self.write_flash_images(root)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(flash.main(["--port", "/dev/fake-watchy", "--image", str(image),
                                             "--partition-table", str(table),
                                             "--build-dir", str(build), "--dry-run"]), 0)
            rendered = output.getvalue()
            self.assertIn(hashlib.sha256(image.read_bytes()).hexdigest(), rendered)
            self.assertIn("platformio run -e watchy_v2", rendered)
            self.assertIn("erase-region 0x9000 0x6000", rendered)
            self.assertIn("write-flash 0x1000", rendered)
            self.assertIn("0x1d0000", rendered)


if __name__ == "__main__":
    unittest.main()
