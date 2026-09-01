#!/usr/bin/env python3
"""Explicitly reinstall firmware and the audited factory LittleFS image.

No operation in this module auto-selects a serial port. The caller supplies a
port that must also appear in PlatformIO's live USB serial discovery result.
The factory workflow erases only the exact NVS partition, resetting settings,
Wi-Fi credentials, package index/health state, and the factory-seed marker.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
import hashlib
import io
import importlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import struct
import sys
import warnings
from typing import Callable, Iterable, Sequence


ROOT = Path(__file__).resolve().parents[1]
NVS_OFFSET = 0x9000
NVS_SIZE = 0x6000
BOOTLOADER_OFFSET = 0x1000
PARTITION_TABLE_OFFSET = 0x8000
FACTORY_OFFSET = 0x10000
FACTORY_SIZE = 0x1C0000
EXPECTED_OFFSET = 0x1D0000
EXPECTED_SIZE = 0x230000
DEFAULT_IMAGE = ROOT / "build" / "factory-seed" / "littlefs.bin"
DEFAULT_PARTITIONS = ROOT / "partitions.csv"
DEFAULT_BUILD_DIR = ROOT / ".pio" / "build" / "watchy_v2"
PINNED_ESPTOOL_VERSION = "5.3.1"

EXPECTED_PARTITIONS = (
    ("nvs", "data", "nvs", NVS_OFFSET, NVS_SIZE, ""),
    ("phy_init", "data", "phy", 0xF000, 0x1000, ""),
    ("factory", "app", "factory", FACTORY_OFFSET, FACTORY_SIZE, ""),
    ("littlefs", "data", "littlefs", EXPECTED_OFFSET, EXPECTED_SIZE, ""),
)


def _make_expected_partition_binary() -> bytes:
    entry = struct.Struct("<HBBII16sI")
    type_ids = {"app": 0x00, "data": 0x01}
    subtype_ids = {"factory": 0x00, "phy": 0x01, "nvs": 0x02, "littlefs": 0x83}
    table = bytearray()
    for name, type_name, subtype_name, offset, size, _flags in EXPECTED_PARTITIONS:
        label = name.encode("ascii")
        table.extend(entry.pack(0x50AA, type_ids[type_name], subtype_ids[subtype_name],
                                offset, size, label + bytes(16 - len(label)), 0))
    table.extend(b"\xeb\xeb" + b"\xff" * 14)
    table.extend(hashlib.md5(table[:4 * entry.size]).digest())
    table.extend(b"\xff" * (0xC00 - len(table)))
    return bytes(table)


EXPECTED_PARTITION_BINARY = _make_expected_partition_binary()


class FlashSafetyError(Exception):
    """A preflight or target-identity failure that forbids flashing."""


Runner = Callable[..., subprocess.CompletedProcess]


@dataclass(frozen=True)
class FlashImage:
    label: str
    offset: int
    path: Path
    size: int
    sha256: str


def esptool_parser_vectors() -> tuple[tuple[str, ...], ...]:
    """Return complete parser-only examples for every esptool form we execute."""
    safe_existing_input = str(Path(__file__).resolve())
    common = ("--chip", "esp32", "--port", "/dev/null",
              "--before", "default-reset", "--after", "no-reset")
    return (
        common + ("chip-id",),
        common + ("erase-region", "0x9000", "0x6000"),
        common + ("write-flash", "0x1000", safe_existing_input),
        ("--chip", "esp32", "--port", "/dev/null", "--before", "no-reset",
         "--after", "hard-reset", "--no-stub", "chip-id"),
    )


def _validate_esptool_parser(module: object) -> None:
    """Parse complete commands without invoking callbacks or touching a device."""
    cli = getattr(module, "cli", None)
    required = ("make_context", "resolve_command")
    if cli is None or any(not callable(getattr(cli, name, None)) for name in required):
        raise FlashSafetyError(
            f"esptool {PINNED_ESPTOOL_VERSION} parser semantics are unsupported")

    missing = object()
    previous_esp = getattr(cli, "_esp", missing)
    try:
        # esptool's Group.__call__ normally initializes this. Parsing directly
        # deliberately avoids Group.invoke(), which would connect to a device.
        cli._esp = None
        for arguments in esptool_parser_vectors():
            with cli.make_context("esptool", list(arguments)) as root_context:
                remaining = [*root_context._protected_args, *root_context.args]
                command_name, command, command_arguments = cli.resolve_command(
                    root_context, remaining)
                if command is None:
                    raise FlashSafetyError(
                        f"esptool parser did not resolve {command_name}")
                with command.make_context(command_name, command_arguments,
                                          parent=root_context) as command_context:
                    # Click 8.3 does not register esptool's addr/file tuple with
                    # Context.close(), so close the parser-opened smoke input
                    # ourselves. The file is never read and no callback runs.
                    for _address, source in command_context.params.get(
                            "addr_filename", ()):
                        source.close()
    except FlashSafetyError:
        raise
    except (Exception, SystemExit) as error:
        raise FlashSafetyError(
            f"esptool {PINNED_ESPTOOL_VERSION} parser semantics are unsupported") from error
    finally:
        if previous_esp is missing:
            try:
                del cli._esp
            except AttributeError:
                pass
        else:
            cli._esp = previous_esp


def validate_esptool_environment() -> str:
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", DeprecationWarning)
            module = importlib.import_module("esptool")
    except (ImportError, ModuleNotFoundError) as error:
        raise FlashSafetyError(
            f"esptool {PINNED_ESPTOOL_VERSION} is required; install "
            "tools/factory-flash-requirements.txt") from error
    version = getattr(module, "__version__", None)
    if version != PINNED_ESPTOOL_VERSION or not callable(getattr(module, "main", None)):
        raise FlashSafetyError(
            f"esptool {PINNED_ESPTOOL_VERSION} is required; imported {version or 'unknown'}")
    _validate_esptool_parser(module)
    return version


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
            if len(row) != 6:
                raise FlashSafetyError("partition row must contain exactly six fields")
            rows.append(tuple(field.strip() for field in row[:6]))
    except (OSError, UnicodeError, csv.Error) as error:
        raise FlashSafetyError(f"cannot read partition table: {error}") from error
    if len(rows) != len(EXPECTED_PARTITIONS):
        raise FlashSafetyError("partition table must contain exactly the four Watchy factory partitions")
    for row, expected in zip(rows, EXPECTED_PARTITIONS):
        name, type_name, subtype_name, expected_offset, expected_size, flags = expected
        if row[:3] != (name, type_name, subtype_name):
            raise FlashSafetyError(f"{name} partition identity is not exact")
        offset, size = _parse_number(row[3]), _parse_number(row[4])
        if offset != expected_offset:
            raise FlashSafetyError(
                f"{name} offset is 0x{offset:x}, expected 0x{expected_offset:x}")
        if size != expected_size:
            raise FlashSafetyError(
                f"{name} size is 0x{size:x}, expected 0x{expected_size:x}")
        if row[5] != flags:
            raise FlashSafetyError(f"{name} partition flags are not exact")
    return EXPECTED_OFFSET, EXPECTED_SIZE


def validate_image(path: Path) -> tuple[int, str]:
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise FlashSafetyError(f"LittleFS image is not a regular file: {path}")
    size = path.stat().st_size
    if size <= 0:
        raise FlashSafetyError("LittleFS image is empty")
    if size > EXPECTED_SIZE:
        raise FlashSafetyError(f"LittleFS image exceeds 0x{EXPECTED_SIZE:x}: {size}")
    if size != EXPECTED_SIZE:
        raise FlashSafetyError(
            f"LittleFS image must be exactly 0x{EXPECTED_SIZE:x} bytes: {size}")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return size, digest.hexdigest()


def _validate_binary(path: Path, label: str, maximum: int,
                     expected: bytes | None = None) -> FlashImage:
    raw_path = Path(path)
    if raw_path.is_symlink() or not raw_path.is_file():
        raise FlashSafetyError(f"{label} image is not a regular file: {raw_path}")
    path = raw_path.resolve()
    data = path.read_bytes()
    if not data or len(data) > maximum:
        raise FlashSafetyError(f"{label} image size is outside its partition: {len(data)}")
    if expected is not None and data != expected:
        raise FlashSafetyError(f"{label} image does not match the exact Watchy partition image")
    return FlashImage(label, 0, path, len(data), hashlib.sha256(data).hexdigest())


def validate_flash_images(build_dir: Path, image: Path) -> tuple[FlashImage, ...]:
    raw_build_dir = Path(build_dir)
    if raw_build_dir.is_symlink() or not raw_build_dir.is_dir():
        raise FlashSafetyError(
            f"firmware build directory is not a regular directory: {raw_build_dir}")
    build_dir = raw_build_dir.resolve()
    bootloader = _validate_binary(build_dir / "bootloader.bin", "bootloader",
                                  PARTITION_TABLE_OFFSET - BOOTLOADER_OFFSET)
    partitions = _validate_binary(build_dir / "partitions.bin", "partition image",
                                  NVS_OFFSET - PARTITION_TABLE_OFFSET,
                                  EXPECTED_PARTITION_BINARY)
    firmware = _validate_binary(build_dir / "firmware.bin", "factory application",
                                FACTORY_SIZE)
    littlefs_size, littlefs_digest = validate_image(image)
    return (
        FlashImage(bootloader.label, BOOTLOADER_OFFSET, bootloader.path,
                   bootloader.size, bootloader.sha256),
        FlashImage(partitions.label, PARTITION_TABLE_OFFSET, partitions.path,
                   partitions.size, partitions.sha256),
        FlashImage(firmware.label, FACTORY_OFFSET, firmware.path,
                   firmware.size, firmware.sha256),
        FlashImage("LittleFS", EXPECTED_OFFSET, Path(image).resolve(),
                   littlefs_size, littlefs_digest),
    )


def validate_port(port: str) -> str:
    if not port or any(character.isspace() for character in port) or not port.startswith("/dev/"):
        raise FlashSafetyError("--port must be one explicit /dev/ serial path without whitespace")
    return port


def commands_for(port: str, image: Path, *, build_dir: Path = DEFAULT_BUILD_DIR,
                 platformio: str) -> list[list[str]]:
    validate_port(port)
    image = Path(image).resolve()
    build_dir = Path(build_dir).resolve()
    esptool = [sys.executable, "-m", "esptool", "--chip", "esp32", "--port", port,
               "--before", "default-reset"]
    chip = esptool + ["--after", "no-reset", "chip-id"]
    return [
        [platformio, "run", "-e", "watchy_v2"],
        [platformio, "device", "list", "--json-output"],
        chip,
        esptool + ["--after", "no-reset", "erase-region",
                   f"0x{NVS_OFFSET:x}", f"0x{NVS_SIZE:x}"],
        esptool + ["--after", "no-reset", "write-flash",
         f"0x{BOOTLOADER_OFFSET:x}", str(build_dir / "bootloader.bin"),
         f"0x{PARTITION_TABLE_OFFSET:x}", str(build_dir / "partitions.bin"),
         f"0x{FACTORY_OFFSET:x}", str(build_dir / "firmware.bin"),
         f"0x{EXPECTED_OFFSET:x}", str(image)],
        [sys.executable, "-m", "esptool", "--chip", "esp32", "--port", port,
         "--before", "no-reset", "--after", "hard-reset", "--no-stub",
         "chip-id"],
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


def preflight_factory(port: str, *, runner: Runner | None = None,
                      platformio: str | None = None) -> str:
    """Validate tooling and read-only serial discovery before expensive builds."""
    validate_port(port)
    version = validate_esptool_environment()
    runner = runner or subprocess.run
    platformio = platformio or shutil.which("platformio") or "platformio"
    discovery = _run(runner, [platformio, "device", "list", "--json-output"])
    _validate_discovery(discovery.stdout, port)
    return version


def _validate_chip(output: str) -> None:
    match = re.search(r"Chip\s+(?:type:\s*|is\s+)([^\r\n]+)", output, re.IGNORECASE)
    chip = match.group(1).strip() if match else ""
    identity = chip.split("(", 1)[0].strip().upper()
    pico_identities = {"ESP32-PICO-D4", "ESP32-PICO-V3", "ESP32-PICO-V3-02"}
    classic = (identity == "ESP32" or identity.startswith("ESP32-D") or
               identity in pico_identities or identity == "ESP32-U4WDH")
    if not classic:
        raise FlashSafetyError(f"serial target is not the classic ESP32 target: {chip or 'unknown'}")


def flash_factory(port: str,
                  image: Path = DEFAULT_IMAGE,
                  partition_table: Path = DEFAULT_PARTITIONS,
                  *, runner: Runner = subprocess.run,
                  printer: Callable[[str], None] = print,
                  platformio: str | None = None,
                  build_dir: Path = DEFAULT_BUILD_DIR) -> str:
    validate_port(port)
    validate_esptool_environment()
    offset, _ = validate_partition_table(partition_table)
    image = Path(image).resolve()
    validate_image(image)
    platformio = platformio or shutil.which("platformio") or "platformio"
    commands = commands_for(port, image, build_dir=build_dir,
                            platformio=platformio)
    _run(runner, commands[0])
    images = validate_flash_images(build_dir, image)
    digest = images[-1].sha256
    printer("; ".join(
        f"{item.label} size={item.size} sha256={item.sha256} offset=0x{item.offset:x}"
        for item in images))
    discovery = _run(runner, commands[1])
    _validate_discovery(discovery.stdout, port)
    _validate_chip(_run(runner, commands[2]).stdout)
    _run(runner, commands[3])  # exact NVS reset removes factory_seed and settings/index
    _run(runner, commands[4])
    # Success-only identity read followed by RTS hard reset into the complete image.
    _run(runner, commands[5])
    return digest


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="factory-reinstall Watchy firmware, NVS state, and LittleFS seed")
    parser.add_argument("--port", required=True,
                        help="explicit serial device; never auto-selected")
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    parser.add_argument("--partition-table", type=Path, default=DEFAULT_PARTITIONS)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR,
                        help="prebuilt watchy_v2 image directory")
    parser.add_argument("--dry-run", action="store_true",
                        help="validate local inputs and print commands without opening a device")
    parser.add_argument("--preflight-only", action="store_true",
                        help="validate port syntax and pinned esptool without build/device access")
    return parser


def main(arguments: Iterable[str] | None = None) -> int:
    try:
        args = make_parser().parse_args(arguments)
        if args.preflight_only:
            esptool_version = preflight_factory(args.port)
            print(f"PREFLIGHT port={args.port} esptool={esptool_version}")
            return 0
        validate_port(args.port)
        validate_esptool_environment()
        offset, _ = validate_partition_table(args.partition_table)
        size, digest = validate_image(args.image)
        platformio = shutil.which("platformio") or "platformio"
        commands = commands_for(args.port, args.image, build_dir=args.build_dir,
                                platformio=platformio)
        if args.dry_run:
            images = validate_flash_images(args.build_dir, args.image)
            print("DRY RUN " + "; ".join(
                f"{item.label} size={item.size} sha256={item.sha256} offset=0x{item.offset:x}"
                for item in images))
            for command in commands:
                print(shlex.join(command))
            return 0
        flash_factory(args.port, args.image, args.partition_table,
                      platformio=platformio,
                      build_dir=args.build_dir)
        return 0
    except (FlashSafetyError, OSError) as error:
        print(f"watchy-factory-flash: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
