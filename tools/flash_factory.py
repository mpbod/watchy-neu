#!/usr/bin/env python3
"""Explicitly flash firmware and the audited factory LittleFS image.

No operation in this module auto-selects a serial port. The caller supplies a
port that must also appear in PlatformIO's live USB serial discovery result.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
from typing import Callable, Iterable, Sequence


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_OFFSET = 0x1D0000
EXPECTED_SIZE = 0x230000
DEFAULT_IMAGE = ROOT / ".pio" / "build" / "watchy_v2" / "littlefs.bin"
DEFAULT_PARTITIONS = ROOT / "partitions.csv"


class FlashSafetyError(Exception):
    """A preflight or target-identity failure that forbids flashing."""


Runner = Callable[..., subprocess.CompletedProcess]


def _parse_number(value: str) -> int:
    try:
        return int(value.strip(), 0)
    except ValueError as error:
        raise FlashSafetyError(f"invalid partition number: {value!r}") from error


def validate_partition_table(path: Path) -> tuple[int, int]:
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise FlashSafetyError(f"partition table is not a regular file: {path}")
    rows = []
    try:
        for raw in path.read_text(encoding="utf-8").splitlines():
            if not raw.strip() or raw.lstrip().startswith("#"):
                continue
            row = next(csv.reader(io.StringIO(raw), skipinitialspace=True))
            if len(row) < 5:
                raise FlashSafetyError("partition row has fewer than five fields")
            if row[0].strip() == "littlefs":
                rows.append(row)
    except (OSError, UnicodeError, csv.Error) as error:
        raise FlashSafetyError(f"cannot read partition table: {error}") from error
    if len(rows) != 1:
        raise FlashSafetyError("partition table must contain exactly one littlefs partition")
    row = rows[0]
    if row[1].strip() != "data" or row[2].strip() != "littlefs":
        raise FlashSafetyError("littlefs partition type/subtype is not data/littlefs")
    offset, size = _parse_number(row[3]), _parse_number(row[4])
    if offset != EXPECTED_OFFSET:
        raise FlashSafetyError(f"littlefs offset is 0x{offset:x}, expected 0x{EXPECTED_OFFSET:x}")
    if size != EXPECTED_SIZE:
        raise FlashSafetyError(f"littlefs size is 0x{size:x}, expected 0x{EXPECTED_SIZE:x}")
    return offset, size


def validate_image(path: Path) -> tuple[int, str]:
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise FlashSafetyError(f"LittleFS image is not a regular file: {path}")
    size = path.stat().st_size
    if size <= 0:
        raise FlashSafetyError("LittleFS image is empty")
    if size > EXPECTED_SIZE:
        raise FlashSafetyError(f"LittleFS image exceeds 0x{EXPECTED_SIZE:x}: {size}")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return size, digest.hexdigest()


def commands_for(port: str, image: Path, *, platformio: str, python: str) -> list[list[str]]:
    if not port or any(character.isspace() for character in port) or not port.startswith("/dev/"):
        raise FlashSafetyError("--port must be one explicit /dev/ serial path without whitespace")
    image = Path(image).resolve()
    chip = [python, "-m", "esptool", "--chip", "esp32", "--port", port, "chip-id"]
    return [
        [platformio, "device", "list", "--json-output"],
        chip,
        [platformio, "run", "-e", "watchy_v2", "-t", "upload", "--upload-port", port],
        chip.copy(),
        [python, "-m", "esptool", "--chip", "esp32", "--port", port,
         "write-flash", f"0x{EXPECTED_OFFSET:x}", str(image)],
    ]


def _run(runner: Runner, command: Sequence[str]) -> subprocess.CompletedProcess:
    try:
        result = runner(command, cwd=ROOT, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise FlashSafetyError(f"command failed: {shlex.join(command)}: {error}") from error
    if result is None or result.returncode != 0:
        raise FlashSafetyError(f"command failed: {shlex.join(command)}")
    return result


def _validate_discovery(output: str, port: str) -> None:
    try:
        devices = json.loads(output)
    except json.JSONDecodeError as error:
        raise FlashSafetyError("PlatformIO serial discovery returned invalid JSON") from error
    matches = [device for device in devices if isinstance(device, dict) and device.get("port") == port]
    if len(matches) != 1:
        raise FlashSafetyError(f"serial port was not discovered exactly once: {port}")
    hwid = matches[0].get("hwid")
    if not isinstance(hwid, str) or not hwid or hwid.lower() == "n/a" or "USB" not in hwid.upper():
        raise FlashSafetyError(f"discovered target is not an identified USB serial device: {port}")


def _validate_chip(output: str) -> None:
    match = re.search(r"Chip type:\s*([^\r\n]+)", output, re.IGNORECASE)
    chip = match.group(1).strip() if match else ""
    if not (chip.upper() == "ESP32" or chip.upper().startswith("ESP32-D")):
        raise FlashSafetyError(f"serial target is not the classic ESP32 target: {chip or 'unknown'}")


def flash_factory(port: str,
                  image: Path = DEFAULT_IMAGE,
                  partition_table: Path = DEFAULT_PARTITIONS,
                  *, runner: Runner = subprocess.run,
                  printer: Callable[[str], None] = print,
                  platformio: str | None = None,
                  python: str | None = None) -> str:
    offset, _ = validate_partition_table(partition_table)
    image = Path(image).resolve()
    size, digest = validate_image(image)
    platformio = platformio or shutil.which("platformio") or "platformio"
    python = python or sys.executable
    commands = commands_for(port, image, platformio=platformio, python=python)
    printer(f"LittleFS image size={size} sha256={digest} offset=0x{offset:x}")
    discovery = _run(runner, commands[0])
    _validate_discovery(discovery.stdout, port)
    _validate_chip(_run(runner, commands[1]).stdout)
    _run(runner, commands[2])  # firmware-only PlatformIO upload
    _validate_chip(_run(runner, commands[3]).stdout)  # re-open and re-identify after reset
    _run(runner, commands[4])
    return digest


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="flash Watchy firmware and factory LittleFS seed")
    parser.add_argument("--port", required=True,
                        help="explicit serial device; never auto-selected")
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    parser.add_argument("--partition-table", type=Path, default=DEFAULT_PARTITIONS)
    parser.add_argument("--dry-run", action="store_true",
                        help="validate local inputs and print commands without opening a device")
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    try:
        args = make_parser().parse_args(arguments)
        offset, _ = validate_partition_table(args.partition_table)
        size, digest = validate_image(args.image)
        platformio = shutil.which("platformio") or "platformio"
        commands = commands_for(args.port, args.image, platformio=platformio, python=sys.executable)
        if args.dry_run:
            print(f"DRY RUN LittleFS image size={size} sha256={digest} offset=0x{offset:x}")
            for command in commands:
                print(shlex.join(command))
            return 0
        flash_factory(args.port, args.image, args.partition_table,
                      platformio=platformio, python=sys.executable)
        return 0
    except (FlashSafetyError, OSError) as error:
        print(f"watchy-factory-flash: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
