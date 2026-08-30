#!/usr/bin/env python3
"""Build both ESP32 shared objects, package them, and verify their WPKs."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SAMPLES = (("digital-watchface", "digital_watchface.so", "digital.wpk"),
           ("hardware-demo", "hardware_demo.so", "hardware-demo.wpk"))


def run(command, *, environment=None):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run([str(part) for part in command], cwd=ROOT, env=environment, check=True)


def build_with_idf(sample: Path) -> Path:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise RuntimeError("IDF_PATH is not set")
    idf = Path(idf_path) / "tools" / "idf.py"
    run([sys.executable, idf, "-C", sample, "build", "so"])
    return sample / "build"


def build_with_platformio(sample: Path) -> Path:
    pio = shutil.which("platformio") or str(Path.home() / ".platformio" / "penv" / "bin" / "platformio")
    if not Path(pio).exists():
        raise RuntimeError("ESP-IDF is unavailable and PlatformIO was not found")
    run([pio, "run", "-d", sample])
    pio_home = Path.home() / ".platformio" / "packages"
    build = sample / ".pio" / "build" / "package"
    environment = os.environ.copy()
    environment["IDF_PATH"] = str(pio_home / "framework-espidf")
    environment["ESP_ROM_ELF_DIR"] = str(pio_home / "tool-esp-rom-elfs")
    bins = [pio_home / "toolchain-xtensa-esp-elf" / "bin",
            pio_home / "tool-cmake" / "bin", pio_home / "tool-ninja"]
    environment["PATH"] = os.pathsep.join(map(str, bins)) + os.pathsep + environment.get("PATH", "")
    ninja = pio_home / "tool-ninja" / "ninja"
    run([ninja, "-C", build, "so"], environment=environment)
    return build


def main(arguments=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build" / "samples")
    args = parser.parse_args(arguments)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for directory, so_name, output_name in SAMPLES:
        sample = ROOT / "samples" / directory
        (sample / "assets").mkdir(exist_ok=True)
        build = build_with_idf(sample) if os.environ.get("IDF_PATH") else build_with_platformio(sample)
        elf = build / so_name
        output = args.output_dir / output_name
        run([sys.executable, ROOT / "tools" / "watchy_pkg.py", "audit-elf", "--elf", elf])
        run([sys.executable, ROOT / "tools" / "watchy_pkg.py", "build",
             "--manifest", sample / "manifest.json", "--elf", elf,
             "--assets", sample / "assets", "--output", output])
        run([sys.executable, ROOT / "tools" / "watchy_pkg.py", "verify", output])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
