#!/usr/bin/env python3
"""Build the fixed, audited WFS1 factory catalog and LittleFS image."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Callable, Iterable

try:
    from tools import watchy_pkg
except ModuleNotFoundError:  # Direct execution: python3 tools/build_factory_seed.py
    import watchy_pkg  # type: ignore[no-redef]


ROOT = Path(__file__).resolve().parents[1]
MAGIC = b"WFS1"
VERSION = 1
COUNT = 8
FILENAME_BYTES = 32
RECORD = struct.Struct("<32s32s")
HEADER = struct.Struct("<4sHH")
CATALOG_BYTES = HEADER.size + COUNT * RECORD.size
LITTLEFS_PARTITION_SIZE = 0x230000
DEFAULT_SOURCE = ROOT / "build" / "first-party"
DEFAULT_STAGING = ROOT / "factory_seed"
DEFAULT_IMAGE = ROOT / ".pio" / "build" / "watchy_v2" / "littlefs.bin"

# These are the Task 9 outputs that received the package/ELF audit. Updating a
# first-party face requires a fresh audit and an explicit update to this table.
AUDITED_WPK_SHA256 = {
    "grid-01.wpk": "465a34f57d867c580075052d56f3c608c86242528118fb5e4e06c91b87356dc8",
    "grid-02.wpk": "abb2a52cb898b293c3fbe6da8c0fa432f8f7883c82fb05736c61b460418d2674",
    "grid-03.wpk": "2c3b978153d6b57bad454a811f877ecab17cde949cfc3a17c9c3d83daccb5687",
    "orbit.wpk": "8b5c20086e75ab11d9a988cd37f8fb923347f44125ea2ed8177adeaac21e8efa",
    "slab.wpk": "e1ca5d417205108a38bdc238b65eee180885da02a2d272ec644f506385855ea8",
    "term-01.wpk": "838f7253e86a71e990f0224b4512a24f321342592a36411f0675f1c6790318c8",
    "term-02.wpk": "f57c05f0faa807be7a1f67e443971cff1d2da8d0c1859645f9386704993fdf88",
    "term-03.wpk": "f7365c4bacef2a8d89dc9e88a825ebaf0c9eab3fc6e896bf2aadb2a350da57c4",
}

EXPECTED_IDS = {
    "grid-01.wpk": "watchy.firstparty.grid01",
    "grid-02.wpk": "watchy.firstparty.grid02",
    "grid-03.wpk": "watchy.firstparty.grid03",
    "orbit.wpk": "watchy.firstparty.orbit",
    "slab.wpk": "watchy.firstparty.slab",
    "term-01.wpk": "watchy.firstparty.term01",
    "term-02.wpk": "watchy.firstparty.term02",
    "term-03.wpk": "watchy.firstparty.term03",
}


class SeedError(Exception):
    """A fail-closed factory seed build error."""


@dataclass(frozen=True)
class StageResult:
    content_bytes: int
    catalog_sha256: str


@dataclass(frozen=True)
class BuildResult:
    content_bytes: int
    catalog_sha256: str
    image_size: int
    image_sha256: str


ImageBuilder = Callable[[Path, Path], None]


def _audited_packages(source_dir: Path) -> list[tuple[str, bytes]]:
    source_dir = Path(source_dir)
    if not source_dir.is_dir() or source_dir.is_symlink():
        raise SeedError(f"package directory is missing or unsafe: {source_dir}")
    actual = {path.name for path in source_dir.iterdir() if path.is_file() or path.is_symlink()}
    expected = set(AUDITED_WPK_SHA256)
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing:
        raise SeedError("missing audited WPK(s): " + ", ".join(missing))
    if unexpected:
        raise SeedError("unexpected file(s) in audited WPK set: " + ", ".join(unexpected))
    packages: list[tuple[str, bytes]] = []
    for name, expected_digest in AUDITED_WPK_SHA256.items():
        path = source_dir / name
        if path.is_symlink() or not path.is_file():
            raise SeedError(f"audited WPK is not a regular file: {path}")
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != expected_digest:
            raise SeedError(f"audited WPK digest changed for {name}: {digest}")
        try:
            verified = watchy_pkg.verify_package(data)
        except watchy_pkg.PackageError as error:
            raise SeedError(f"WPK verification failed for {name}: {error}") from error
        manifest = verified["manifest"]
        if (manifest["id"] != EXPECTED_IDS[name] or manifest["version"] != "1.0.0" or
                manifest["type"] != "watchface"):
            raise SeedError(f"unexpected first-party manifest identity for {name}")
        packages.append((name, data))
    return packages


def build_catalog(source_dir: Path = DEFAULT_SOURCE) -> bytes:
    records = bytearray(HEADER.pack(MAGIC, VERSION, COUNT))
    for name, data in _audited_packages(Path(source_dir)):
        encoded = name.encode("ascii")
        if len(encoded) >= FILENAME_BYTES:
            raise SeedError(f"catalog filename is too long: {name}")
        records.extend(RECORD.pack(encoded, hashlib.sha256(data).digest()))
    if len(records) != CATALOG_BYTES:
        raise SeedError("internal WFS1 record-size error")
    return bytes(records)


def _replace_tree(source: Path, destination: Path) -> None:
    backup = destination.parent / f".{destination.name}.previous-{os.getpid()}"
    if backup.exists():
        shutil.rmtree(backup)
    moved_old = False
    try:
        if destination.exists():
            os.replace(destination, backup)
            moved_old = True
        os.replace(source, destination)
    except OSError as error:
        if moved_old and backup.exists() and not destination.exists():
            os.replace(backup, destination)
        raise SeedError(f"cannot publish staging tree: {error}") from error
    if backup.exists():
        shutil.rmtree(backup)


def stage_seed(source_dir: Path = DEFAULT_SOURCE,
               staging_dir: Path = DEFAULT_STAGING) -> StageResult:
    source_dir, staging_dir = Path(source_dir), Path(staging_dir)
    packages = _audited_packages(source_dir)
    catalog = bytearray(HEADER.pack(MAGIC, VERSION, COUNT))
    for name, data in packages:
        catalog.extend(RECORD.pack(name.encode("ascii"), hashlib.sha256(data).digest()))
    staging_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{staging_dir.name}.staging-",
                                      dir=staging_dir.parent))
    try:
        factory = temporary / "factory"
        factory.mkdir()
        (factory / "seed.bin").write_bytes(catalog)
        for name, data in packages:
            (factory / name).write_bytes(data)
        _replace_tree(temporary, staging_dir)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return StageResult(
        content_bytes=len(catalog) + sum(len(data) for _, data in packages),
        catalog_sha256=hashlib.sha256(catalog).hexdigest(),
    )


def _run_idf_image_builder(staging_dir: Path, image_path: Path) -> None:
    if staging_dir.resolve() != DEFAULT_STAGING.resolve() or image_path.resolve() != DEFAULT_IMAGE.resolve():
        raise SeedError("native IDF image target requires the default staging and image paths")
    configure_command = [shutil.which("platformio") or "platformio", "run", "-e", "watchy_v2"]
    image_command = [shutil.which("cmake") or "cmake", "--build",
                     str(ROOT / ".pio" / "build" / "watchy_v2"),
                     "--target", "littlefs_littlefs_bin"]
    try:
        for command in (configure_command, image_command):
            print("+", " ".join(command), flush=True)
            subprocess.run(command, cwd=ROOT, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise SeedError(f"ESP-IDF LittleFS image target failed: {error}") from error


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_factory_seed(source_dir: Path = DEFAULT_SOURCE,
                       staging_dir: Path = DEFAULT_STAGING,
                       image_path: Path = DEFAULT_IMAGE,
                       *, reproducible: bool = False,
                       image_builder: ImageBuilder | None = None) -> BuildResult:
    source_dir, staging_dir, image_path = map(Path, (source_dir, staging_dir, image_path))
    if image_builder is None:
        image_builder = _run_idf_image_builder
    rounds = 2 if reproducible else 1
    prior_copy: Path | None = None
    stage_result: StageResult | None = None
    try:
        for round_index in range(rounds):
            stage_result = stage_seed(source_dir, staging_dir)
            image_path.unlink(missing_ok=True)
            image_builder(staging_dir, image_path)
            if image_path.is_symlink() or not image_path.is_file():
                raise SeedError(f"LittleFS builder did not produce a regular image: {image_path}")
            size = image_path.stat().st_size
            if size > LITTLEFS_PARTITION_SIZE:
                raise SeedError(f"LittleFS image exceeds 0x{LITTLEFS_PARTITION_SIZE:x}: {size}")
            if round_index == 0 and rounds == 2:
                handle, copy_name = tempfile.mkstemp(prefix="watchy-seed-round1-", suffix=".bin")
                os.close(handle)
                prior_copy = Path(copy_name)
                shutil.copyfile(image_path, prior_copy)
            elif prior_copy is not None and (_file_sha256(prior_copy) != _file_sha256(image_path) or
                                             prior_copy.stat().st_size != size):
                raise SeedError("LittleFS image is not reproducible across two clean staging rounds")
        assert stage_result is not None
        return BuildResult(stage_result.content_bytes, stage_result.catalog_sha256,
                           image_path.stat().st_size, _file_sha256(image_path))
    finally:
        if prior_copy is not None:
            prior_copy.unlink(missing_ok=True)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="build the audited Watchy factory LittleFS seed")
    parser.add_argument("--source-dir", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--staging-dir", type=Path, default=DEFAULT_STAGING)
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    parser.add_argument("--reproducible", action="store_true")
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    try:
        args = make_parser().parse_args(arguments)
        result = build_factory_seed(args.source_dir, args.staging_dir, args.image,
                                    reproducible=args.reproducible)
        print(f"seed content={result.content_bytes} catalog_sha256={result.catalog_sha256}")
        print(f"LittleFS image={args.image} size={result.image_size} sha256={result.image_sha256}")
        return 0
    except (SeedError, OSError) as error:
        print(f"watchy-factory-seed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
