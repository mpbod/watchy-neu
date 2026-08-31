#!/usr/bin/env python3
"""Build the first-party face matrix when renderer projects are present."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CAPABILITIES = 0x203  # Canvas, Clock, System
FACES = tuple({
    "id": f"watchy.firstparty.{slug}", "output": f"{slug}.wpk", "name": name,
    "abi_major": 1, "abi_minor": 2, "type": "watchface", "version": "1.0.0",
    "capabilities": caps, "max_runtime_bytes": 49152, "assets": []
} for slug, name, caps in (
    ("grid01", "Grid 01", 787),
    ("grid02", "Grid 02", CAPABILITIES),
    ("grid03", "Grid 03", CAPABILITIES),
    ("orbit", "Orbit", CAPABILITIES),
    ("slab", "Slab", CAPABILITIES),
    ("term01", "Term 01", CAPABILITIES),
    ("term02", "Term 02", CAPABILITIES),
    ("term03", "Term 03", CAPABILITIES),
))


def run(command, cwd=ROOT):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run([str(x) for x in command], cwd=cwd, check=True)


def manifest(face):
    return {key: face[key] for key in ("abi_major", "abi_minor", "assets", "capabilities", "id", "max_runtime_bytes", "name", "type", "version")}


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build" / "first-party")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args(argv)
    if args.list:
        for face in FACES:
            print(face["id"], face["output"])
        return 0
    import tools.watchy_pkg as watchy_pkg
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for face in FACES:
        project = ROOT / "first_party" / "watchfaces" / face["output"][:-4]
        if not project.is_dir():
            raise RuntimeError(f"renderer project is not present yet: {project}")
        elf = project / "build" / f"{face['output'][:-4]}.so"
        manifest_path = project / "manifest.json"
        manifest_path.write_text(json.dumps(manifest(face), sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
        # Each project is rebuilt in two clean output directories; byte equality is required.
        artifacts = []
        for repeat in ("repro-a", "repro-b"):
            build_dir = project / "build" / repeat
            if build_dir.exists(): shutil.rmtree(build_dir)
            build_dir.mkdir(parents=True)
            run([sys.executable, ROOT / "tools" / "watchy_pkg.py", "audit-elf", "--elf", elf])
            out = args.output_dir / f"{face['output']}.{repeat}"
            run([sys.executable, ROOT / "tools" / "watchy_pkg.py", "build", "--manifest", manifest_path,
                 "--elf", elf, "--assets", project / "assets", "--output", out])
            run([sys.executable, ROOT / "tools" / "watchy_pkg.py", "verify", out])
            artifacts.append(out.read_bytes())
        if artifacts[0] != artifacts[1]:
            raise RuntimeError(f"non-deterministic output for {face['id']}")
        final = args.output_dir / face["output"]
        final.write_bytes(artifacts[0])
        if len(artifacts[0]) > watchy_pkg.MAX_WPK:
            raise RuntimeError(f"WPK exceeds 80 KiB: {final}")
        for repeat in ("repro-a", "repro-b"):
            (args.output_dir / f"{face['output']}.{repeat}").unlink()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
