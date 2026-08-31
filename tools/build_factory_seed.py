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
WORKSPACE_ROOT = ROOT.parents[2]
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
IDF_IMAGE = ROOT / ".pio" / "build" / "watchy_v2" / "littlefs.bin"
DEFAULT_IMAGE = ROOT / "build" / "factory-seed" / "littlefs.bin"
STAGING_SENTINEL = ROOT / ".factory_seed.watchy-owned"
IMAGE_SENTINEL = DEFAULT_IMAGE.parent / ".watchy-factory-image-owned"
OWNER_BYTES = b"watchy-factory-seed-v1\n"

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


def _staged_files(source_dir: Path) -> tuple[dict[str, bytes], StageResult]:
    packages = _audited_packages(source_dir)
    catalog = bytearray(HEADER.pack(MAGIC, VERSION, COUNT))
    files: dict[str, bytes] = {}
    for name, data in packages:
        catalog.extend(RECORD.pack(name.encode("ascii"), hashlib.sha256(data).digest()))
        files[f"factory/{name}"] = data
    files["factory/seed.bin"] = bytes(catalog)
    return files, StageResult(
        content_bytes=sum(len(data) for data in files.values()),
        catalog_sha256=hashlib.sha256(catalog).hexdigest(),
    )


def _write_new_staging_tree(source_dir: Path,
                            staging_dir: Path) -> StageResult:
    source_dir, staging_dir = Path(source_dir), Path(staging_dir)
    if staging_dir.exists() or staging_dir.is_symlink() or not staging_dir.parent.is_dir():
        raise SeedError(f"new staging destination is unsafe or already exists: {staging_dir}")
    files, result = _staged_files(source_dir)
    staging_dir.mkdir()
    (staging_dir / "factory").mkdir()
    for relative, data in sorted(files.items()):
        (staging_dir / relative).write_bytes(data)
    return result


def _sentinel_valid(path: Path) -> bool:
    return (not path.is_symlink() and path.is_file() and
            path.read_bytes() == OWNER_BYTES)


def _validate_existing_staging(staging_dir: Path,
                               sentinel: Path,
                               source_dir: Path) -> None:
    staging_dir, sentinel = Path(staging_dir), Path(sentinel)
    if sentinel.exists() or sentinel.is_symlink():
        if not _sentinel_valid(sentinel):
            raise SeedError(f"staging ownership sentinel is unsafe: {sentinel}")
        if staging_dir.is_symlink() or (staging_dir.exists() and not staging_dir.is_dir()):
            raise SeedError(f"owned staging path is unsafe: {staging_dir}")
        return
    if not staging_dir.exists():
        return
    if staging_dir.is_symlink() or not staging_dir.is_dir():
        raise SeedError(f"default staging path is unsafe: {staging_dir}")
    expected, _ = _staged_files(Path(source_dir))
    actual: dict[str, bytes] = {}
    directories: set[str] = set()
    for path in staging_dir.rglob("*"):
        if path.is_symlink():
            raise SeedError(f"default staging tree is not tool-owned: {staging_dir}")
        relative = path.relative_to(staging_dir).as_posix()
        if path.is_dir():
            directories.add(relative)
        elif path.is_file():
            actual[relative] = path.read_bytes()
        else:
            raise SeedError(f"default staging tree is not tool-owned: {staging_dir}")
    if directories != {"factory"} or actual != expected:
        raise SeedError(f"default staging tree is not tool-owned: {staging_dir}")


def _reject_symlink_components(path: Path, label: str) -> None:
    absolute = Path(os.path.abspath(os.fspath(path)))
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current /= part
        if current.is_symlink():
            raise SeedError(f"{label} path has an unsafe symlink component: {current}")


def _require_exact_path(provided: Path, expected: Path, label: str) -> Path:
    _reject_symlink_components(Path(provided), label)
    normalized = Path(os.path.abspath(os.fspath(provided)))
    expected_normalized = Path(os.path.abspath(os.fspath(expected)))
    if normalized in (ROOT, WORKSPACE_ROOT) or normalized != expected_normalized:
        raise SeedError(f"unsupported {label} path; required: {expected_normalized}")
    return normalized


def _validate_existing_image(image_path: Path, sentinel: Path) -> None:
    output_dir = image_path.parent
    if output_dir.is_symlink() or (output_dir.exists() and not output_dir.is_dir()):
        raise SeedError(f"image output directory is unsafe: {output_dir}")
    if output_dir.exists():
        if not _sentinel_valid(sentinel):
            raise SeedError(f"image output directory is not tool-owned: {output_dir}")
        if image_path.is_symlink() or (image_path.exists() and not image_path.is_file()):
            raise SeedError(f"image output path is unsafe: {image_path}")


def _preflight_paths(source_dir: Path,
                     staging_dir: Path,
                     image_path: Path) -> tuple[Path, Path, Path]:
    source = _require_exact_path(source_dir, DEFAULT_SOURCE, "source")
    staging = _require_exact_path(staging_dir, DEFAULT_STAGING, "staging")
    image = _require_exact_path(image_path, DEFAULT_IMAGE, "image")
    _audited_packages(source)
    _validate_existing_staging(staging, STAGING_SENTINEL, source)
    _validate_existing_image(image, IMAGE_SENTINEL)
    return source, staging, image


def _write_sentinel(path: Path) -> None:
    if path.exists() or path.is_symlink():
        if not _sentinel_valid(path):
            raise SeedError(f"ownership sentinel is unsafe: {path}")
        return
    path.write_bytes(OWNER_BYTES)


def _publish_owned_staging(candidate: Path,
                           destination: Path,
                           sentinel: Path) -> None:
    if not _sentinel_valid(sentinel):
        raise SeedError("staging ownership was lost before publication")
    with tempfile.TemporaryDirectory(prefix=".factory-seed-backup-",
                                     dir=destination.parent) as backup_root_name:
        backup = Path(backup_root_name) / "previous"
        moved_old = False
        try:
            if destination.exists():
                os.replace(destination, backup)
                moved_old = True
            os.replace(candidate, destination)
        except OSError as error:
            if moved_old and backup.exists() and not destination.exists():
                os.replace(backup, destination)
            raise SeedError(f"cannot publish owned staging tree: {error}") from error


def _prepare_owned_destinations(staging_dir: Path,
                                image_path: Path,
                                staging_sentinel: Path,
                                image_sentinel: Path) -> None:
    staging_dir.parent.mkdir(parents=True, exist_ok=True)
    _write_sentinel(staging_sentinel)
    image_path.parent.mkdir(parents=True, exist_ok=True)
    _write_sentinel(image_sentinel)


def _run_idf_image_builder(staging_dir: Path, image_path: Path) -> None:
    if staging_dir != DEFAULT_STAGING:
        raise SeedError("native IDF image target requires the default staging path")
    _reject_symlink_components(IDF_IMAGE, "IDF intermediate image")
    if IDF_IMAGE.is_symlink():
        raise SeedError(f"IDF intermediate image is unsafe: {IDF_IMAGE}")
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
    if IDF_IMAGE.is_symlink() or not IDF_IMAGE.is_file():
        raise SeedError(f"ESP-IDF target did not produce its controlled image: {IDF_IMAGE}")
    with image_path.open("xb") as output, IDF_IMAGE.open("rb") as source:
        shutil.copyfileobj(source, output, 65536)


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _build_factory_seed_owned(source_dir: Path,
                              staging_dir: Path,
                              image_path: Path,
                              staging_sentinel: Path,
                              image_sentinel: Path,
                              *, reproducible: bool = False,
                              image_builder: ImageBuilder | None = None) -> BuildResult:
    source_dir, staging_dir, image_path, staging_sentinel, image_sentinel = map(
        Path, (source_dir, staging_dir, image_path, staging_sentinel, image_sentinel))
    if image_builder is None:
        image_builder = _run_idf_image_builder
    rounds = 2 if reproducible else 1
    prior_size: int | None = None
    prior_digest: str | None = None
    stage_result: StageResult | None = None
    _prepare_owned_destinations(staging_dir, image_path, staging_sentinel, image_sentinel)
    for round_index in range(rounds):
        with tempfile.TemporaryDirectory(prefix=".factory-seed-stage-",
                                         dir=staging_dir.parent) as stage_root_name:
            candidate = Path(stage_root_name) / "factory_seed"
            stage_result = _write_new_staging_tree(source_dir, candidate)
            _publish_owned_staging(candidate, staging_dir, staging_sentinel)
        with tempfile.TemporaryDirectory(prefix=".factory-seed-image-",
                                         dir=image_path.parent) as image_root_name:
            candidate_image = Path(image_root_name) / "littlefs.bin"
            image_builder(staging_dir, candidate_image)
            if candidate_image.is_symlink() or not candidate_image.is_file():
                raise SeedError(f"LittleFS builder did not produce a regular image: {candidate_image}")
            size = candidate_image.stat().st_size
            if size == 0:
                raise SeedError("LittleFS builder produced an empty image")
            if size > LITTLEFS_PARTITION_SIZE:
                raise SeedError(f"LittleFS image exceeds 0x{LITTLEFS_PARTITION_SIZE:x}: {size}")
            digest = _file_sha256(candidate_image)
            if round_index == 0:
                prior_size, prior_digest = size, digest
            elif prior_size != size or prior_digest != digest:
                raise SeedError("LittleFS image is not reproducible across two clean staging rounds")
            if round_index + 1 == rounds:
                if not _sentinel_valid(image_sentinel):
                    raise SeedError("image ownership was lost before publication")
                os.replace(candidate_image, image_path)
    assert stage_result is not None and prior_size is not None and prior_digest is not None
    return BuildResult(stage_result.content_bytes, stage_result.catalog_sha256,
                       prior_size, prior_digest)


def build_factory_seed(source_dir: Path = DEFAULT_SOURCE,
                       staging_dir: Path = DEFAULT_STAGING,
                       image_path: Path = DEFAULT_IMAGE,
                       *, reproducible: bool = False,
                       image_builder: ImageBuilder | None = None) -> BuildResult:
    source, staging, image = _preflight_paths(source_dir, staging_dir, image_path)
    return _build_factory_seed_owned(
        source, staging, image, STAGING_SENTINEL, IMAGE_SENTINEL,
        reproducible=reproducible, image_builder=image_builder)


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
