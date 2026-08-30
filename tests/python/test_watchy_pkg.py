import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("watchy_pkg", ROOT / "tools" / "watchy_pkg.py")
watchy_pkg = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(watchy_pkg)


def make_xtensa_so(*, machine=94, elf_type=3, export="watchy_package_entry",
                   undefined_relocation=False):
    """Build a small ELF32 fixture independently of the package implementation."""
    text = b"\x00\x00\x00\x00"
    dynstr = b"\0" + export.encode("ascii") + b"\0"
    if undefined_relocation:
        dynstr += b"memset\0"
    shstr = b"\0.text\0.dynstr\0.dynsym\0"
    if undefined_relocation:
        shstr += b".rela.dyn\0"
    shstr += b".shstrtab\0"
    eh_size, ph_size, sh_size = 52, 32, 40
    text_off = eh_size + ph_size
    dynstr_off = text_off + len(text)
    dynsym_off = (dynstr_off + len(dynstr) + 3) & ~3
    dynsym = bytes(16) + struct.pack("<IIIBBH", 1, 0x1000, 4, 0x12, 0, 1)
    relocation = b""
    if undefined_relocation:
        memset_offset = dynstr.index(b"memset")
        dynsym += struct.pack("<IIIBBH", memset_offset, 0, 0, 0x10, 0, 0)
        relocation = struct.pack("<IIi", 0x1000, (2 << 8) | 3, 0)
    relocation_off = dynsym_off + len(dynsym)
    shstr_off = relocation_off + len(relocation)
    shoff = (shstr_off + len(shstr) + 3) & ~3
    section_count = 6 if undefined_relocation else 5
    blob = bytearray(shoff + section_count * sh_size)
    blob[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into(
        "<HHIIIIIHHHHHH", blob, 16, elf_type, machine, 1, 0x1000,
        eh_size, shoff, 0, eh_size, ph_size, 1, sh_size, section_count,
        section_count - 1,
    )
    struct.pack_into("<IIIIIIII", blob, eh_size, 1, text_off, 0x1000, 0x1000,
                     len(text), len(text), 5, 4)
    blob[text_off:text_off + len(text)] = text
    blob[dynstr_off:dynstr_off + len(dynstr)] = dynstr
    blob[dynsym_off:dynsym_off + len(dynsym)] = dynsym
    blob[relocation_off:relocation_off + len(relocation)] = relocation
    blob[shstr_off:shstr_off + len(shstr)] = shstr
    names = {name: shstr.index(name.encode("ascii"))
             for name in (".text", ".dynstr", ".dynsym", ".shstrtab")}
    sections = [
        (names[".text"], 1, 6, 0x1000, text_off, len(text), 0, 0, 4, 0),
        (names[".dynstr"], 3, 0, 0, dynstr_off, len(dynstr), 0, 0, 1, 0),
        (names[".dynsym"], 11, 0, 0, dynsym_off, len(dynsym), 2, 1, 4, 16),
    ]
    if undefined_relocation:
        sections.append((shstr.index(b".rela.dyn"), 4, 0, 0, relocation_off,
                         len(relocation), 3, 0, 4, 12))
    sections.append((names[".shstrtab"], 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0))
    for index, values in enumerate(sections, 1):
        struct.pack_into("<IIIIIIIIII", blob, shoff + index * sh_size, *values)
    return bytes(blob)


def make_wpk(manifest, elf, assets):
    manifest_bytes = json.dumps(
        manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    total = watchy_pkg.HEADER_SIZE + len(manifest_bytes) + len(elf) + len(assets)
    header = watchy_pkg.HEADER.pack(
        watchy_pkg.MAGIC, watchy_pkg.FORMAT_VERSION, watchy_pkg.HEADER_SIZE, total,
        watchy_pkg.HEADER_SIZE, len(manifest_bytes),
        watchy_pkg.HEADER_SIZE + len(manifest_bytes), len(elf),
        watchy_pkg.HEADER_SIZE + len(manifest_bytes) + len(elf), len(assets), bytes(32),
    )
    package = bytearray(header + manifest_bytes + elf + assets)
    package[watchy_pkg.DIGEST_OFFSET:watchy_pkg.DIGEST_OFFSET + 32] = hashlib.sha256(package).digest()
    return bytes(package)


def base_manifest(assets=None):
    return {
        "name": "Fixture",
        "version": "1.2.3",
        "id": "test.fixture",
        "type": "app",
        "abi_minor": 1,
        "abi_major": 1,
        "capabilities": 1,
        "max_runtime_bytes": 4096,
        "assets": assets or [],
    }


class PackageToolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.elf = self.dir / "package.so"
        self.elf.write_bytes(make_xtensa_so())
        self.assets = self.dir / "assets"
        self.assets.mkdir()
        self.manifest = self.dir / "manifest.json"

    def tearDown(self):
        self.temp.cleanup()

    def write_manifest(self, value):
        self.manifest.write_text(json.dumps(value, indent=2), encoding="utf-8")

    def build(self, output):
        return watchy_pkg.build_package(self.manifest, self.elf, self.assets, output)

    def test_build_is_deterministic_and_digest_zeros_digest_field(self):
        self.write_manifest(base_manifest())
        first, second = self.dir / "a.wpk", self.dir / "b.wpk"
        self.build(first)
        self.build(second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        package = bytearray(first.read_bytes())
        stored = bytes(package[36:68])
        package[36:68] = bytes(32)
        self.assertEqual(stored, hashlib.sha256(package).digest())

    def test_corrupt_header_and_digest_are_rejected(self):
        self.write_manifest(base_manifest())
        output = self.dir / "ok.wpk"
        self.build(output)
        corrupt = bytearray(output.read_bytes())
        corrupt[0] ^= 1
        with self.assertRaisesRegex(watchy_pkg.PackageError, "header"):
            watchy_pkg.verify_package(bytes(corrupt))
        corrupt = bytearray(output.read_bytes())
        corrupt[-1] ^= 1
        with self.assertRaisesRegex(watchy_pkg.PackageError, "digest"):
            watchy_pkg.verify_package(bytes(corrupt))

    def test_wrong_elf_architecture_and_type_are_rejected(self):
        self.write_manifest(base_manifest())
        for content in (make_xtensa_so(machine=3), make_xtensa_so(elf_type=2)):
            self.elf.write_bytes(content)
            with self.assertRaisesRegex(watchy_pkg.PackageError, "elf"):
                self.build(self.dir / "bad.wpk")

    def test_finalize_elf_sets_entry_to_the_only_export(self):
        self.elf.write_bytes(make_xtensa_so())
        original = bytearray(self.elf.read_bytes())
        struct.pack_into("<I", original, 24, 0)
        self.elf.write_bytes(original)
        watchy_pkg.finalize_elf(self.elf)
        self.assertEqual(struct.unpack_from("<I", self.elf.read_bytes(), 24)[0], 0x1000)
        watchy_pkg.validate_elf(self.elf.read_bytes(), 4096)

    def test_elf_runtime_estimate_matches_firmware_tlsf_accounting(self):
        info = watchy_pkg.validate_elf(make_xtensa_so(), 4096)
        # 4-byte text + 28-byte TLSF allocation for the 21-byte export name
        # + 16-byte TLSF allocation for one 8-byte esp_symtab entry.
        self.assertEqual(info["runtime_bytes"], 48)

    def test_malformed_elf_structures_rejected_by_firmware_are_rejected(self):
        section_zero = bytearray(make_xtensa_so())
        section_offset = struct.unpack_from("<I", section_zero, 32)[0]
        section_zero[section_offset] = 1
        with self.assertRaisesRegex(watchy_pkg.PackageError, "elf"):
            watchy_pkg.validate_elf(bytes(section_zero), 4096)

        non_readable_load = bytearray(make_xtensa_so())
        program_offset = struct.unpack_from("<I", non_readable_load, 28)[0]
        struct.pack_into("<I", non_readable_load, program_offset + 24, 1)
        with self.assertRaisesRegex(watchy_pkg.PackageError, "elf"):
            watchy_pkg.validate_elf(bytes(non_readable_load), 4096)

    def test_elf_rejects_relocations_to_undefined_symbols(self):
        with self.assertRaisesRegex(watchy_pkg.PackageError, "elf"):
            watchy_pkg.validate_elf(make_xtensa_so(undefined_relocation=True), 4096)

    def test_symbol_audit_reports_only_the_package_entry_export(self):
        audit = watchy_pkg.audit_elf_symbols(make_xtensa_so())
        self.assertEqual(audit["defined_global_functions"], ["watchy_package_entry"])
        self.assertEqual(audit["undefined_symbols"], [])
        self.assertEqual(audit["symbol_relocations"], [])

    def test_traversal_and_symlink_assets_are_rejected(self):
        self.write_manifest(base_manifest([{"path": "../secret", "size": 1}]))
        with self.assertRaisesRegex(watchy_pkg.PackageError, "path"):
            self.build(self.dir / "bad.wpk")

        target = self.dir / "target"
        target.write_bytes(b"secret")
        link = self.assets / "link.bin"
        try:
            link.symlink_to(target)
        except (OSError, NotImplementedError):
            self.skipTest("symlinks unavailable")
        self.write_manifest(base_manifest([{"path": "link.bin", "size": 6}]))
        with self.assertRaisesRegex(watchy_pkg.PackageError, "symlink"):
            self.build(self.dir / "bad.wpk")

    def test_assets_are_sorted_and_concatenated_in_manifest_order(self):
        (self.assets / "z.bin").write_bytes(b"Z")
        (self.assets / "a.bin").write_bytes(b"AA")
        declared = [{"path": "z.bin", "size": 1}, {"path": "a.bin", "size": 2}]
        self.write_manifest(base_manifest(declared))
        output = self.dir / "sorted.wpk"
        info = self.build(output)
        self.assertEqual([a["path"] for a in info["manifest"]["assets"]], ["a.bin", "z.bin"])
        parsed = watchy_pkg.verify_package(output.read_bytes())
        self.assertEqual(parsed["asset_bytes"], b"AAZ")

    def test_verify_rejects_digest_correct_wpk_with_unsorted_assets(self):
        manifest = base_manifest([
            {"path": "z.bin", "size": 1},
            {"path": "a.bin", "size": 2},
        ])
        package = make_wpk(manifest, make_xtensa_so(), b"ZAA")
        with self.assertRaisesRegex(watchy_pkg.PackageError, "manifest"):
            watchy_pkg.verify_package(package)

    def test_failure_is_atomic_and_leaves_no_temporary_sibling(self):
        output = self.dir / "kept.wpk"
        output.write_bytes(b"keep")
        self.write_manifest(base_manifest())
        self.elf.write_bytes(b"not an elf")
        with self.assertRaises(watchy_pkg.PackageError):
            self.build(output)
        self.assertEqual(output.read_bytes(), b"keep")
        self.assertEqual(list(self.dir.glob(".kept.wpk.*.tmp")), [])

    def test_inspect_is_machine_readable_and_never_extracts(self):
        self.write_manifest(base_manifest())
        output = self.dir / "ok.wpk"
        self.build(output)
        before = set(self.dir.iterdir())
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            self.assertEqual(watchy_pkg.main(["inspect", str(output), "--json"]), 0)
        value = json.loads(stream.getvalue())
        self.assertEqual(value["id"], "test.fixture")
        self.assertEqual(value["abi"], "1.1")
        self.assertEqual(before, set(self.dir.iterdir()))


if __name__ == "__main__":
    unittest.main()
