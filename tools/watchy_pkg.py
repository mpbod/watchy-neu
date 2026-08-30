#!/usr/bin/env python3
"""Build, inspect, and verify deterministic Watchy WPK1 packages.

This tool intentionally uses only the Python 3.11 standard library.  Its wire
constants mirror the firmware's public WPK and package SDK contracts.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import tempfile
from typing import Any, Iterable


MAGIC = b"WPK1"
FORMAT_VERSION = 1
HEADER = struct.Struct("<4sHHIIIIIII32s")
HEADER_SIZE = HEADER.size
DIGEST_OFFSET = 36
MAX_WPK = 80 * 1024
MAX_ELF = 64 * 1024
MAX_ASSETS = 32 * 1024
MAX_MANIFEST = 16 * 1024
MAX_RUNTIME = 80 * 1024
MAX_ASSET_COUNT = 64
MAX_ASSET_PATH = 96
KNOWN_CAPABILITIES = (1 << 10) - 1
ABI_MAJOR = 1
ABI_MINOR = 1
ENTRY_POINT = "watchy_package_entry"


class PackageError(Exception):
    """A validation or I/O failure with a stable machine-readable code."""

    def __init__(self, code: str, message: str):
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


def fail(code: str, message: str) -> None:
    raise PackageError(code, message)


def canonical_json(value: Any) -> bytes:
    try:
        result = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        encoded = result.encode("utf-8")
    except (TypeError, UnicodeError, ValueError) as error:
        fail("manifest", f"cannot encode canonical JSON: {error}")
    if any(ord(character) < 0x20 for character in result):
        fail("manifest", "control characters are unsupported")
    return encoded


def _bounded_string(value: Any, field: str, maximum: int) -> str:
    if not isinstance(value, str) or not value or len(value.encode("utf-8")) > maximum:
        fail("manifest", f"{field} must be a non-empty string of at most {maximum} bytes")
    if any(ord(character) < 0x20 or ord(character) == 0x7F for character in value):
        fail("manifest", f"{field} contains a control character")
    return value


def _valid_id(identifier: str) -> bool:
    return (len(identifier) <= 48 and identifier[0].isalnum() and
            all("a" <= c <= "z" or "0" <= c <= "9" or c in "._-" for c in identifier))


def _valid_version(version: str) -> bool:
    raw = version.encode("utf-8")
    return (0 < len(raw) <= 32 and not version.startswith(".") and
            not version.startswith("watchy-") and
            all(ord(c) >= 0x21 and ord(c) != 0x7F and c not in "/\\@" for c in version))


def _valid_asset_path(path: str) -> bool:
    try:
        raw = path.encode("utf-8")
    except UnicodeError:
        return False
    if not raw or len(raw) > MAX_ASSET_PATH or path.startswith("/") or path.endswith("/") or "\\" in path:
        return False
    parts = path.split("/")
    if any(not part or part in (".", "..") or part.startswith(".watchy-") for part in parts):
        return False
    if any(any(ord(c) < 0x20 or ord(c) == 0x7F for c in part) for part in parts):
        return False
    first = parts[0]
    return (not first.startswith(".") and first not in {"package.so", "manifest.json", "state", ".new"}
            and not first.startswith("watchy-"))


def _paths_collide(lhs: str, rhs: str) -> bool:
    return lhs == rhs or lhs.startswith(rhs + "/") or rhs.startswith(lhs + "/")


def validate_manifest(value: Any, actual_assets: list[dict[str, Any]] | None = None,
                      *, require_sorted: bool = False) -> dict[str, Any]:
    fields = {"abi_major", "abi_minor", "assets", "capabilities", "id",
              "max_runtime_bytes", "name", "type", "version"}
    if not isinstance(value, dict) or set(value) != fields:
        fail("manifest", "manifest must contain exactly the nine WPK1 fields")
    if type(value["abi_major"]) is not int or type(value["abi_minor"]) is not int:
        fail("manifest", "ABI fields must be integers")
    if value["abi_major"] != ABI_MAJOR or not 0 <= value["abi_minor"] <= ABI_MINOR:
        fail("abi", f"package ABI {value['abi_major']}.{value['abi_minor']} is incompatible with {ABI_MAJOR}.{ABI_MINOR}")
    identifier = _bounded_string(value["id"], "id", 48)
    if not _valid_id(identifier):
        fail("manifest", "id is not a valid package identifier")
    _bounded_string(value["name"], "name", 64)
    version = _bounded_string(value["version"], "version", 32)
    if not _valid_version(version):
        fail("path", "version is unsafe for package storage")
    if value["type"] not in ("watchface", "app"):
        fail("manifest", "type must be watchface or app")
    if type(value["capabilities"]) is not int or value["capabilities"] < 0 or value["capabilities"] & ~KNOWN_CAPABILITIES:
        fail("capability", "capabilities contains an unsupported bit")
    runtime = value["max_runtime_bytes"]
    if type(runtime) is not int or not 0 < runtime <= MAX_RUNTIME:
        fail("limit", f"max_runtime_bytes must be in 1..{MAX_RUNTIME}")
    assets = value["assets"]
    if not isinstance(assets, list) or len(assets) > MAX_ASSET_COUNT:
        fail("limit", f"assets must be a list with at most {MAX_ASSET_COUNT} entries")
    normalized: list[dict[str, Any]] = []
    total = 0
    for entry in assets:
        if not isinstance(entry, dict) or set(entry) != {"path", "size"}:
            fail("manifest", "each asset requires exactly path and size")
        path = entry["path"]
        size = entry["size"]
        if not isinstance(path, str) or not _valid_asset_path(path):
            fail("path", f"invalid asset path: {path!r}")
        if type(size) is not int or size < 0:
            fail("manifest", f"invalid asset size for {path}")
        if any(_paths_collide(path, prior["path"]) for prior in normalized):
            fail("collision", f"asset path collides: {path}")
        total += size
        if total > MAX_ASSETS:
            fail("limit", f"aggregate assets exceed {MAX_ASSETS} bytes")
        normalized.append({"path": path, "size": size})
    if require_sorted and normalized != sorted(
            normalized, key=lambda entry: entry["path"].encode("utf-8")):
        fail("manifest", "asset paths must be strictly sorted by UTF-8 bytes")
    normalized.sort(key=lambda entry: entry["path"].encode("utf-8"))
    if actual_assets is not None and normalized != actual_assets:
        fail("assets", "declared assets do not exactly match the asset directory")
    result = dict(value)
    result["assets"] = normalized
    return result


def collect_assets(directory: Path) -> tuple[list[dict[str, Any]], bytes]:
    if not directory.is_dir():
        fail("io", f"asset directory does not exist: {directory}")
    files: list[tuple[str, Path]] = []
    for root, directories, names in os.walk(directory, followlinks=False):
        root_path = Path(root)
        for name in list(directories) + list(names):
            candidate = root_path / name
            if candidate.is_symlink():
                fail("symlink", f"symlink assets are forbidden: {candidate}")
        for name in names:
            candidate = root_path / name
            if not candidate.is_file():
                fail("path", f"asset is not a regular file: {candidate}")
            relative = candidate.relative_to(directory).as_posix()
            if PurePosixPath(relative).as_posix() != relative or not _valid_asset_path(relative):
                fail("path", f"invalid asset path: {relative}")
            files.append((relative, candidate))
    files.sort(key=lambda item: item[0].encode("utf-8"))
    entries: list[dict[str, Any]] = []
    payload = bytearray()
    for path, candidate in files:
        content = candidate.read_bytes()
        entries.append({"path": path, "size": len(content)})
        payload.extend(content)
        if len(entries) > MAX_ASSET_COUNT or len(payload) > MAX_ASSETS:
            fail("limit", "asset count or aggregate size exceeds firmware limits")
    return entries, bytes(payload)


def _cstring(table: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(table):
        fail("elf", "string table offset is out of range")
    end = table.find(b"\0", offset)
    if end < 0:
        fail("elf", "unterminated ELF string")
    try:
        return table[offset:end].decode("ascii")
    except UnicodeDecodeError:
        fail("elf", "non-ASCII ELF symbol or section name")


def _range_fits(file_size: int, offset: int, size: int) -> bool:
    return offset <= file_size and size <= file_size - offset


def _add_u32(lhs: int, rhs: int) -> int | None:
    result = lhs + rhs
    return result if result <= 0xFFFFFFFF else None


def _ranges_overlap(lhs: int, lhs_size: int, rhs: int, rhs_size: int) -> bool:
    lhs_end = _add_u32(lhs, lhs_size)
    rhs_end = _add_u32(rhs, rhs_size)
    return (lhs_size != 0 and rhs_size != 0 and lhs_end is not None and rhs_end is not None
            and lhs < rhs_end and rhs < lhs_end)


def _tlsf_allocation(requested: int) -> int:
    if requested <= 0 or requested > 0xFFFFFFFC:
        fail("limit", "ELF loader allocation overflows")
    payload = max(12, (requested + 3) & ~3)
    return payload + 4


def _loader_section_kind(name: str, section: tuple[int, ...]) -> int:
    section_type, flags = section[1], section[2]
    if name == ".text" and section_type == 1 and flags & 0x6 == 0x6 and not flags & 0x1:
        return 1
    if name == ".data" and section_type == 1 and flags & 0x3 == 0x3 and not flags & 0x4:
        return 2
    if name == ".rodata" and section_type == 1 and flags & 0x2 and not flags & 0x5:
        return 3
    if name == ".data.rel.ro" and section_type == 1 and flags & 0x2 and not flags & 0x4:
        return 4
    if name == ".bss" and section_type == 8 and flags & 0x3 == 0x3 and not flags & 0x4:
        return 5
    return 0


def _loader_metadata_section(name: str, section_type: int) -> bool:
    return ((section_type == 11 and name == ".dynsym") or
            (section_type == 3 and name == ".dynstr") or
            (section_type == 5 and name == ".hash") or
            (section_type == 6 and name == ".dynamic") or
            (section_type == 4 and name.startswith(".rela")))


def _section_in_load(section: tuple[int, ...], programs: list[tuple[int, ...]]) -> bool:
    section_type, flags, address, offset, size = section[1:6]
    section_end = _add_u32(address, size)
    if section_end is None:
        return False
    for program in programs:
        program_type, file_offset, virtual_address, _physical, file_size, memory_size, permissions, _align = program
        memory_end = _add_u32(virtual_address, memory_size)
        if (program_type != 1 or memory_end is None or address < virtual_address or
                section_end > memory_end or flags & 0x4 and not permissions & 0x1 or
                flags & 0x1 and not permissions & 0x2 or not permissions & 0x4):
            continue
        if section_type == 8:
            return True
        file_end = _add_u32(file_offset, file_size)
        if file_end is not None and offset >= file_offset and offset <= file_end and size <= file_end - offset:
            return True
    return False


def validate_elf(elf: bytes, declared_runtime: int) -> dict[str, Any]:
    if not elf or len(elf) > MAX_ELF:
        fail("limit", f"ELF size must be in 1..{MAX_ELF}")
    if len(elf) < 52 or elf[:7] != b"\x7fELF\x01\x01\x01":
        fail("elf", "ELF must be 32-bit little-endian version 1")
    try:
        values = struct.unpack_from("<HHIIIIIHHHHHH", elf, 16)
    except struct.error:
        fail("elf", "truncated ELF header")
    elf_type, machine, version, entry, phoff, shoff, _flags, ehsize, phentsize, phnum, shentsize, shnum, shstrndx = values
    if elf_type != 3 or machine != 94 or version != 1:
        fail("elf", "ELF must be ET_DYN for Xtensa (EM_XTENSA=94)")
    if ehsize != 52 or phentsize != 32 or shentsize != 40 or phnum == 0 or shnum < 2 or not 0 < shstrndx < shnum:
        fail("elf", "unsupported ELF table layout")
    if phoff < 52 or shoff < 52 or phoff + phnum * 32 > len(elf) or shoff + shnum * 40 > len(elf):
        fail("elf", "ELF tables are out of range")
    programs = []
    for index in range(phnum):
        try:
            programs.append(struct.unpack_from("<IIIIIIII", elf, phoff + index * 32))
        except struct.error:
            fail("elf", "truncated program table")
    sections = []
    for index in range(shnum):
        try:
            sections.append(struct.unpack_from("<IIIIIIIIII", elf, shoff + index * 40))
        except struct.error:
            fail("elf", "truncated section table")
    if any(sections[0]):
        fail("elf", "section zero must be empty")
    shstr_section = sections[shstrndx]
    shstr_off, shstr_size = shstr_section[4], shstr_section[5]
    if (shstr_section[1] != 3 or shstr_section[2] & 0x2 or shstr_size < 2 or
            not _range_fits(len(elf), shstr_off, shstr_size)):
        fail("elf", "invalid section-name table")
    shstr = elf[shstr_off:shstr_off + shstr_size]
    if shstr[0] != 0 or shstr[-1] != 0:
        fail("elf", "invalid section-name string table")
    names = [_cstring(shstr, section[0]) for section in sections]
    dynsym_indices = [index for index, (name, section) in enumerate(zip(names, sections))
                      if name == ".dynsym" and section[1] == 11]
    dynstr_indices = [index for index, (name, section) in enumerate(zip(names, sections))
                      if name == ".dynstr" and section[1] == 3]
    if (len(dynsym_indices) != 1 or len(dynstr_indices) != 1 or
            any(section[1] == 11 and name != ".dynsym" for name, section in zip(names, sections)) or
            any(name == ".dynsym" and section[1] != 11 for name, section in zip(names, sections)) or
            any(name == ".dynstr" and section[1] != 3 for name, section in zip(names, sections))):
        fail("elf", "missing .dynsym or .dynstr")
    dynsym_index = dynsym_indices[0]
    dynstr_index = dynstr_indices[0]
    if sections[dynsym_index][6] != dynstr_index:
        fail("elf", "dynamic symbol table does not link .dynstr")

    for index, program in enumerate(programs):
        program_type, file_offset, virtual_address, _physical, file_size, memory_size, _flags, align = program
        if program_type != 1:
            continue
        if (file_size > memory_size or not _range_fits(len(elf), file_offset, file_size) or
                _add_u32(virtual_address, memory_size) is None or
                align > 1 and (align & (align - 1) or file_offset % align != virtual_address % align)):
            fail("elf", "invalid load segment")
        if any(other[0] == 1 and _ranges_overlap(virtual_address, memory_size, other[2], other[5])
               for other in programs[:index]):
            fail("elf", "overlapping load segments")

    runtime = 0
    seen_loader_kinds: set[int] = set()
    text_address = 0
    text_size = 0
    loader_function_count = 0
    exports: list[tuple[str, int, int, int]] = []
    for index in range(1, shnum):
        section = sections[index]
        name = names[index]
        section_type, flags, address, offset, size, link, info, align, entry_size = section[1:]
        if (section_type != 8 and not _range_fits(len(elf), offset, size)) or _add_u32(address, size) is None:
            fail("elf", f"section {name!r} is out of range")
        if align > 1 and (align & (align - 1) or address and address % align):
            fail("elf", f"section {name!r} has invalid alignment")
        kind = _loader_section_kind(name, section)
        if flags & 0x2:
            if ((not kind and not _loader_metadata_section(name, section_type)) or not size or
                    not _section_in_load(section, programs)):
                fail("elf", f"unsupported or unmapped allocated section {name!r}")
            allocation = 0
            if kind:
                allocation = size
                if kind == 1:
                    allocation = (size + 3) & ~3
                    if not _range_fits(len(elf), offset, allocation):
                        fail("elf", ".text padding is outside the ELF")
                    text_address, text_size = address, size
                if kind in seen_loader_kinds:
                    fail("elf", f"duplicate loader section {name!r}")
                if any(_loader_section_kind(names[prior], sections[prior]) and
                       _ranges_overlap(address, size, sections[prior][3], sections[prior][5])
                       for prior in range(1, index)):
                    fail("elf", "overlapping loader sections")
                seen_loader_kinds.add(kind)
                runtime += allocation
        if section_type != 8 and size and any(
                sections[prior][1] != 8 and sections[prior][5] and
                _ranges_overlap(offset, size, sections[prior][4], sections[prior][5])
                for prior in range(1, index)):
            fail("elf", "overlapping file sections")
        if section_type == 3 and (not size or elf[offset] != 0 or elf[offset + size - 1] != 0):
            fail("elf", f"invalid string table {name!r}")
        if section_type in (2, 11):
            entries = size // 16
            if (entry_size != 16 or size % 16 or link >= shnum or info > entries or
                    sections[link][1] != 3):
                fail("elf", f"invalid symbol table {name!r}")
            string_section = sections[link]
            strings = elf[string_section[4]:string_section[4] + string_section[5]]
            for symbol_offset in range(offset, offset + size, 16):
                name_offset, value, symbol_size, symbol_info, _other, symbol_section = struct.unpack_from(
                    "<IIIBBH", elf, symbol_offset)
                symbol_name = _cstring(strings, name_offset)
                symbol_end = _add_u32(value, symbol_size)
                if (symbol_end is None or
                        symbol_section != 0 and symbol_section < 0xFF00 and symbol_section >= shnum):
                    fail("elf", "symbol references an invalid range or section")
                loader_function = section_type == 11 and symbol_info >> 4 == 1 and symbol_info & 0x0F == 2
                if symbol_section != 0 and symbol_section < 0xFF00:
                    target = sections[symbol_section]
                    target_end = _add_u32(target[3], target[5])
                    if (not target[2] & 0x2 or target_end is None or
                            value < target[3] or symbol_end > target_end):
                        fail("elf", "defined symbol lies outside its allocated section")
                    if loader_function and _loader_section_kind(names[symbol_section], target) != 1:
                        fail("elf", "global function is not defined in .text")
                if loader_function:
                    loader_function_count += 1
                    runtime += _tlsf_allocation(len(symbol_name.encode("ascii")) + 1)
                    if symbol_section != 0:
                        if symbol_section >= 0xFF00 or symbol_name != ENTRY_POINT or exports:
                            fail("elf", f"exactly one defined global function export named {ENTRY_POINT} is required")
                        exports.append((symbol_name, value, symbol_size, symbol_section))
            if section_type == 11 and loader_function_count:
                runtime += _tlsf_allocation(loader_function_count * 8)
        elif section_type == 4:
            if (entry_size != 12 or size % 12 or link >= shnum or info >= shnum or
                    info == 0 and name != ".rela.dyn" or sections[link][1] not in (2, 11) or
                    info != 0 and not sections[info][2] & 0x2):
                fail("elf", f"invalid relocation section {name!r}")
            symbol_count = sections[link][5] // 16
            for relocation_offset in range(offset, offset + size, 12):
                target, relocation_info, _addend = struct.unpack_from("<IIi", elf, relocation_offset)
                relocation_type = relocation_info & 0xFF
                symbol_index = relocation_info >> 8
                if symbol_index >= symbol_count or not 2 <= relocation_type <= 5:
                    fail("elf", "unsupported relocation")
                symbol_offset = sections[link][4] + symbol_index * 16
                symbol_section = struct.unpack_from("<H", elf, symbol_offset + 14)[0]
                if symbol_index != 0 and symbol_section == 0:
                    fail("elf", "relocation references an undefined symbol")
                if not any((info == 0 or info == target_index) and
                           _loader_section_kind(names[target_index], sections[target_index]) and
                           sections[target_index][5] >= 4 and
                           target >= sections[target_index][3] and
                           target <= sections[target_index][3] + sections[target_index][5] - 4
                           for target_index in range(1, shnum)):
                    fail("elf", "relocation target is outside a loader section")
        elif section_type == 5:
            if size < 8 or size % 4 or entry_size != 4 or link >= shnum or sections[link][1] != 11:
                fail("elf", "invalid ELF hash section")
        elif section_type == 6:
            if size % 8 or entry_size != 8 or link >= shnum or sections[link][1] != 3:
                fail("elf", "invalid dynamic section")

    if (1 not in seen_loader_kinds or not text_size or len(exports) != 1 or
            not text_address <= entry < text_address + text_size):
        fail("elf", "missing .text, export, or valid entry point")
    if runtime <= 0 or runtime > declared_runtime or runtime > MAX_RUNTIME:
        fail("limit", f"ELF runtime estimate {runtime} exceeds declared ceiling {declared_runtime}")
    return {"size": len(elf), "runtime_bytes": runtime, "exports": [ENTRY_POINT]}


def audit_elf_symbols(elf: bytes) -> dict[str, Any]:
    """Return the validated dynamic symbol and relocation surface."""
    validate_elf(elf, MAX_RUNTIME)
    shoff = struct.unpack_from("<I", elf, 32)[0]
    shnum, shstrndx = struct.unpack_from("<HH", elf, 48)
    sections = [struct.unpack_from("<IIIIIIIIII", elf, shoff + index * 40)
                for index in range(shnum)]
    shstr_section = sections[shstrndx]
    shstr = elf[shstr_section[4]:shstr_section[4] + shstr_section[5]]
    names = [_cstring(shstr, section[0]) for section in sections]
    dynsym_index = names.index(".dynsym")
    dynsym = sections[dynsym_index]
    dynstr = sections[dynsym[6]]
    strings = elf[dynstr[4]:dynstr[4] + dynstr[5]]
    symbol_names: list[str] = []
    undefined: list[str] = []
    defined_functions: list[str] = []
    for offset in range(dynsym[4], dynsym[4] + dynsym[5], 16):
        name_offset, _value, _size, info, _other, section_index = struct.unpack_from(
            "<IIIBBH", elf, offset)
        name = _cstring(strings, name_offset)
        symbol_names.append(name)
        if name and section_index == 0:
            undefined.append(name)
        if name and section_index != 0 and info >> 4 == 1 and info & 0x0F == 2:
            defined_functions.append(name)
    symbol_relocations: list[dict[str, Any]] = []
    for section_name, section in zip(names, sections):
        if section[1] != 4:
            continue
        for offset in range(section[4], section[4] + section[5], 12):
            _target, relocation_info, _addend = struct.unpack_from("<IIi", elf, offset)
            symbol_index = relocation_info >> 8
            if symbol_index:
                symbol_relocations.append({
                    "section": section_name,
                    "symbol": symbol_names[symbol_index],
                    "type": relocation_info & 0xFF,
                })
    return {
        "defined_global_functions": defined_functions,
        "undefined_symbols": undefined,
        "symbol_relocations": symbol_relocations,
    }


def _elf_export_value(elf: bytes) -> int:
    """Return the entry export address from an otherwise structural ELF32 SO."""
    if len(elf) < 52 or elf[:7] != b"\x7fELF\x01\x01\x01":
        fail("elf", "ELF must be 32-bit little-endian version 1")
    elf_type, machine = struct.unpack_from("<HH", elf, 16)
    if elf_type != 3 or machine != 94:
        fail("elf", "ELF must be ET_DYN for Xtensa")
    shoff = struct.unpack_from("<I", elf, 32)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 46)
    if shentsize != 40 or shnum < 2 or shoff + shnum * 40 > len(elf) or not 0 < shstrndx < shnum:
        fail("elf", "invalid section table")
    sections = [struct.unpack_from("<IIIIIIIIII", elf, shoff + index * 40) for index in range(shnum)]
    shstr_section = sections[shstrndx]
    shstr = elf[shstr_section[4]:shstr_section[4] + shstr_section[5]]
    names = [_cstring(shstr, section[0]) for section in sections]
    try:
        dynsym = sections[names.index(".dynsym")]
        dynstr_index = names.index(".dynstr")
        dynstr = sections[dynstr_index]
    except ValueError:
        fail("elf", "missing dynamic symbol tables")
    if dynsym[1] != 11 or dynsym[6] != dynstr_index or dynsym[9] != 16 or dynsym[5] % 16:
        fail("elf", "invalid dynamic symbol table")
    strings = elf[dynstr[4]:dynstr[4] + dynstr[5]]
    exports = []
    for offset in range(dynsym[4], dynsym[4] + dynsym[5], 16):
        name_offset, value, _size, info, _other, section_index = struct.unpack_from("<IIIBBH", elf, offset)
        if info >> 4 == 1 and info & 0x0F == 2 and section_index != 0:
            exports.append((_cstring(strings, name_offset), value))
    if len(exports) != 1 or exports[0][0] != ENTRY_POINT:
        fail("elf", f"exactly one defined global function export named {ENTRY_POINT} is required")
    return exports[0][1]


def finalize_elf(path: Path) -> None:
    """Set ET_DYN e_entry to watchy_package_entry, as required by firmware."""
    path = Path(path)
    try:
        content = bytearray(path.read_bytes())
        entry = _elf_export_value(content)
        struct.pack_into("<I", content, 24, entry)
        path.write_bytes(content)
    except OSError as error:
        fail("io", f"cannot finalize ELF: {error}")


def _parse_header(data: bytes) -> tuple[Any, ...]:
    if len(data) < HEADER_SIZE:
        fail("header", "WPK is shorter than its fixed header")
    values = HEADER.unpack_from(data)
    if values[0] != MAGIC or values[1] != FORMAT_VERSION:
        fail("header", "bad WPK magic or format version")
    if values[2] != HEADER_SIZE:
        fail("header", "unexpected fixed-header size")
    total, mo, ms, eo, es, ao, ass = values[3:10]
    if total != len(data) or mo != HEADER_SIZE or eo != mo + ms or ao != eo + es or total != ao + ass:
        fail("layout", "sections must be contiguous and consume the complete WPK")
    if not ms or ms > MAX_MANIFEST or not es or es > MAX_ELF or ass > MAX_ASSETS or total > MAX_WPK:
        fail("limit", "WPK section or total size exceeds firmware limits")
    return values


def verify_package(data: bytes) -> dict[str, Any]:
    values = _parse_header(data)
    total, manifest_offset, manifest_size, elf_offset, elf_size, assets_offset, assets_size = values[3:10]
    normalized = bytearray(data)
    normalized[DIGEST_OFFSET:DIGEST_OFFSET + 32] = bytes(32)
    digest = hashlib.sha256(normalized).digest()
    if digest != values[10]:
        fail("digest", "SHA-256 does not match the package")
    manifest_bytes = data[manifest_offset:manifest_offset + manifest_size]
    try:
        if canonical_json(json.loads(manifest_bytes.decode("utf-8"))) != manifest_bytes:
            fail("manifest", "manifest is not canonical sorted compact JSON")
        manifest = validate_manifest(json.loads(manifest_bytes.decode("utf-8")),
                                     require_sorted=True)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail("manifest", f"manifest JSON is invalid: {error}")
    elf = data[elf_offset:elf_offset + elf_size]
    elf_info = validate_elf(elf, manifest["max_runtime_bytes"])
    asset_bytes = data[assets_offset:assets_offset + assets_size]
    if sum(asset["size"] for asset in manifest["assets"]) != len(asset_bytes):
        fail("assets", "manifest asset sizes do not match payload")
    return {"manifest": manifest, "elf": elf_info, "asset_bytes": asset_bytes,
            "digest": digest.hex(), "size": total}


def _load_manifest(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail("manifest", f"cannot read manifest: {error}")


def build_package(manifest_path: Path, elf_path: Path, assets_path: Path, output_path: Path) -> dict[str, Any]:
    manifest_path, elf_path, assets_path, output_path = map(Path, (manifest_path, elf_path, assets_path, output_path))
    entries, asset_bytes = collect_assets(assets_path)
    manifest = validate_manifest(_load_manifest(manifest_path), entries)
    manifest_bytes = canonical_json(manifest)
    if len(manifest_bytes) > MAX_MANIFEST:
        fail("limit", f"canonical manifest exceeds {MAX_MANIFEST} bytes")
    try:
        elf = elf_path.read_bytes()
    except OSError as error:
        fail("io", f"cannot read ELF: {error}")
    validate_elf(elf, manifest["max_runtime_bytes"])
    total = HEADER_SIZE + len(manifest_bytes) + len(elf) + len(asset_bytes)
    if total > MAX_WPK:
        fail("limit", f"WPK exceeds {MAX_WPK} bytes")
    header = HEADER.pack(MAGIC, FORMAT_VERSION, HEADER_SIZE, total,
                         HEADER_SIZE, len(manifest_bytes), HEADER_SIZE + len(manifest_bytes), len(elf),
                         HEADER_SIZE + len(manifest_bytes) + len(elf), len(asset_bytes), bytes(32))
    package = bytearray(header + manifest_bytes + elf + asset_bytes)
    package[DIGEST_OFFSET:DIGEST_OFFSET + 32] = hashlib.sha256(package).digest()
    info = verify_package(bytes(package))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        descriptor, name = tempfile.mkstemp(prefix=f".{output_path.name}.", suffix=".tmp", dir=output_path.parent)
        temporary = Path(name)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(package)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output_path)
        temporary = None
    except OSError as error:
        fail("io", f"atomic package write failed: {error}")
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
    return info


def public_info(info: dict[str, Any]) -> dict[str, Any]:
    manifest = info["manifest"]
    return {
        "abi": f"{manifest['abi_major']}.{manifest['abi_minor']}",
        "assets": manifest["assets"],
        "capabilities": manifest["capabilities"],
        "digest": info["digest"],
        "elf_bytes": info["elf"]["size"],
        "id": manifest["id"],
        "max_runtime_bytes": manifest["max_runtime_bytes"],
        "name": manifest["name"],
        "package_bytes": info["size"],
        "runtime_bytes": info["elf"]["runtime_bytes"],
        "type": manifest["type"],
        "version": manifest["version"],
    }


def _read_package(path: Path) -> dict[str, Any]:
    try:
        return verify_package(path.read_bytes())
    except OSError as error:
        fail("io", f"cannot read package: {error}")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="watchy_pkg.py")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="build a deterministic WPK")
    build.add_argument("--manifest", required=True, type=Path)
    build.add_argument("--elf", required=True, type=Path)
    build.add_argument("--assets", required=True, type=Path)
    build.add_argument("--output", required=True, type=Path)
    inspect = commands.add_parser("inspect", help="inspect a WPK without extracting it")
    inspect.add_argument("package", type=Path)
    inspect.add_argument("--json", action="store_true")
    verify = commands.add_parser("verify", help="validate a WPK")
    verify.add_argument("package", type=Path)
    finalize = commands.add_parser("finalize-elf", help=argparse.SUPPRESS)
    finalize.add_argument("--elf", required=True, type=Path)
    audit = commands.add_parser("audit-elf", help="audit package ELF imports and exports")
    audit.add_argument("--elf", required=True, type=Path)
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    try:
        args = make_parser().parse_args(arguments)
        if args.command == "build":
            info = build_package(args.manifest, args.elf, args.assets, args.output)
            print(f"OK {info['manifest']['id']}@{info['manifest']['version']} {info['size']} bytes")
        elif args.command == "inspect":
            value = public_info(_read_package(args.package))
            if args.json:
                print(json.dumps(value, sort_keys=True, separators=(",", ":")))
            else:
                for key, value_item in value.items():
                    print(f"{key}: {value_item}")
        elif args.command == "verify":
            info = _read_package(args.package)
            print(f"OK {info['manifest']['id']}@{info['manifest']['version']} sha256={info['digest']}")
        elif args.command == "finalize-elf":
            finalize_elf(args.elf)
            print(f"OK finalized {args.elf}")
        elif args.command == "audit-elf":
            try:
                audit = audit_elf_symbols(args.elf.read_bytes())
            except OSError as error:
                fail("io", f"cannot read ELF: {error}")
            if audit["undefined_symbols"]:
                fail("elf", "undefined dynamic symbols: " + ",".join(audit["undefined_symbols"]))
            print(f"OK exports={','.join(audit['defined_global_functions'])} "
                  f"undefined=0 symbol_relocations={len(audit['symbol_relocations'])}")
        return 0
    except PackageError as error:
        print(f"watchy-pkg:{error.code}: {error.message}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
