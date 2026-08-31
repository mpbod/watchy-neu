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
    "grid-01.wpk": "465a34f57d867c580075052d56f3c608c86242528118fb5e4e06c91b87356dc8",
    "grid-02.wpk": "abb2a52cb898b293c3fbe6da8c0fa432f8f7883c82fb05736c61b460418d2674",
    "grid-03.wpk": "2c3b978153d6b57bad454a811f877ecab17cde949cfc3a17c9c3d83daccb5687",
    "orbit.wpk": "8b5c20086e75ab11d9a988cd37f8fb923347f44125ea2ed8177adeaac21e8efa",
    "slab.wpk": "e1ca5d417205108a38bdc238b65eee180885da02a2d272ec644f506385855ea8",
    "term-01.wpk": "838f7253e86a71e990f0224b4512a24f321342592a36411f0675f1c6790318c8",
    "term-02.wpk": "f57c05f0faa807be7a1f67e443971cff1d2da8d0c1859645f9386704993fdf88",
    "term-03.wpk": "f7365c4bacef2a8d89dc9e88a825ebaf0c9eab3fc6e896bf2aadb2a350da57c4",
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
            one = seed.stage_seed(FIRST_PARTY, first)
            two = seed.stage_seed(FIRST_PARTY, second)
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

            result = seed.build_factory_seed(
                source_dir=FIRST_PARTY, staging_dir=root / "stage",
                image_path=root / "littlefs.bin", reproducible=True,
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
                seed.build_factory_seed(
                    source_dir=FIRST_PARTY, staging_dir=root / "stage",
                    image_path=root / "littlefs.bin", reproducible=True,
                    image_builder=changing_builder)

    def test_builder_rejects_oversize_or_missing_image_output(self):
        import tools.build_factory_seed as seed
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def oversized(_staging, image):
                with image.open("wb") as output:
                    output.truncate(seed.LITTLEFS_PARTITION_SIZE + 1)
            with self.assertRaisesRegex(seed.SeedError, "exceeds"):
                seed.build_factory_seed(FIRST_PARTY, root / "stage", root / "image.bin",
                                        image_builder=oversized)
            with self.assertRaisesRegex(seed.SeedError, "did not produce"):
                seed.build_factory_seed(FIRST_PARTY, root / "stage", root / "image.bin",
                                        image_builder=lambda _stage, _image: None)


class FactoryFlashSafetyTests(unittest.TestCase):
    def write_partition_table(self, root: Path, offset="0x1d0000", size="0x230000") -> Path:
        table = root / "partitions.csv"
        table.write_text(
            "# Name,Type,SubType,Offset,Size,Flags\n"
            f"littlefs,data,littlefs,{offset},{size},\n", encoding="utf-8")
        return table

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

    def test_fake_serial_run_constructs_verified_firmware_then_littlefs_commands(self):
        import tools.flash_factory as flash
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            table = self.write_partition_table(root)
            image = root / "littlefs.bin"
            image.write_bytes(b"seed-image")
            commands = []
            messages = []

            def fake_runner(command, **kwargs):
                commands.append([str(item) for item in command])
                if command[1:4] == ["device", "list", "--json-output"]:
                    stdout = json.dumps([{"port": "/dev/fake-watchy", "description": "USB Serial",
                                          "hwid": "USB VID:PID=1A86:55D4 SER=ABC"}])
                elif "chip-id" in command:
                    stdout = "Chip type: ESP32-D0WD-V3 (revision v3.1)"
                else:
                    stdout = ""
                return subprocess.CompletedProcess(command, 0, stdout=stdout, stderr="")

            result = flash.flash_factory(
                port="/dev/fake-watchy", image=image, partition_table=table,
                runner=fake_runner, printer=messages.append,
                platformio="platformio", python="python3")
            self.assertEqual(result, hashlib.sha256(b"seed-image").hexdigest())
            self.assertIn(result, messages[0])  # digest is emitted before any write command
            self.assertEqual(commands[0], ["platformio", "device", "list", "--json-output"])
            self.assertIn("chip-id", commands[1])
            self.assertEqual(commands[2], ["platformio", "run", "-e", "watchy_v2", "-t", "upload",
                                           "--upload-port", "/dev/fake-watchy"])
            self.assertIn("chip-id", commands[3])
            self.assertEqual(commands[4][-3:],
                             ["write-flash", "0x1d0000", str(image.resolve())])

    def test_flash_rejects_oversize_undiscovered_or_wrong_chip_before_upload(self):
        import tools.flash_factory as flash
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            table = self.write_partition_table(root)
            image = root / "littlefs.bin"
            with image.open("wb") as output:
                output.truncate(0x230001)
            called = []
            with self.assertRaisesRegex(flash.FlashSafetyError, "exceeds"):
                flash.flash_factory("/dev/fake", image, table, runner=lambda *a, **k: called.append(a))
            self.assertEqual(called, [])

            image.write_bytes(b"ok")
            def no_device(command, **_kwargs):
                return subprocess.CompletedProcess(command, 0, stdout="[]", stderr="")
            with self.assertRaisesRegex(flash.FlashSafetyError, "not discovered"):
                flash.flash_factory("/dev/fake", image, table, runner=no_device)

            commands = []
            def wrong_chip(command, **_kwargs):
                commands.append(command)
                stdout = (json.dumps([{"port": "/dev/fake", "description": "USB", "hwid": "USB SER=X"}])
                          if "device" in command else "Chip type: ESP32-S3")
                return subprocess.CompletedProcess(command, 0, stdout=stdout, stderr="")
            with self.assertRaisesRegex(flash.FlashSafetyError, "ESP32 target"):
                flash.flash_factory("/dev/fake", image, table, runner=wrong_chip)
            self.assertFalse(any("upload" in command for command in commands))

    def test_dry_run_prints_safe_commands_and_executes_nothing(self):
        import tools.flash_factory as flash
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            table = self.write_partition_table(root)
            image = root / "littlefs.bin"
            image.write_bytes(b"ok")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(flash.main(["--port", "/dev/fake-watchy", "--image", str(image),
                                             "--partition-table", str(table), "--dry-run"]), 0)
            rendered = output.getvalue()
            self.assertIn(hashlib.sha256(b"ok").hexdigest(), rendered)
            self.assertIn("platformio run -e watchy_v2 -t upload", rendered)
            self.assertIn("write-flash 0x1d0000", rendered)


if __name__ == "__main__":
    unittest.main()
