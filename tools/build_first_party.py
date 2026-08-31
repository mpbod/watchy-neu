#!/usr/bin/env python3
"""Build and publish the independent first-party WPK face set.

The matrix is deliberately hard-coded: renderer manifests are outputs of this
tool, not an input used to decide which packages are trusted first-party
artifacts.  Builds are serialized because ESP-IDF's generated state is not a
safe shared resource.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Callable, Iterable, Sequence

ROOT = Path(__file__).resolve().parents[1]
FACES = tuple({
    "id": f"watchy.firstparty.{slug}",
    "output": f"{project}.wpk",
    "project": project,
    "name": name,
    "abi_major": 1,
    "abi_minor": 2,
    "type": "watchface",
    "version": "1.0.0",
    "capabilities": capabilities,
    "max_runtime_bytes": 49152,
    "assets": [],
} for slug, project, name, capabilities in (
    ("grid01", "grid-01", "Grid 01", 275),
    ("grid02", "grid-02", "Grid 02", 3),
    ("grid03", "grid-03", "Grid 03", 3),
    ("orbit", "orbit", "Orbit", 19),
    ("slab", "slab", "Slab", 19),
    ("term01", "term-01", "Term 01", 259),
    ("term02", "term-02", "Term 02", 19),
    ("term03", "term-03", "Term 03", 19),
))
FACE_BY_PROJECT = {face["project"]: face for face in FACES}


class BuilderError(Exception):
    """A user-facing first-party builder failure."""


Runner = Callable[[Sequence[object], Path], object]


def run(command: Sequence[object], cwd: Path = ROOT) -> object:
    print("+", " ".join(map(str, command)), flush=True)
    return subprocess.run([str(item) for item in command], cwd=cwd, check=True)


def _watchy_pkg_module():
    try:
        from tools import watchy_pkg
        return watchy_pkg
    except (ImportError, ModuleNotFoundError):
        path = ROOT / "tools" / "watchy_pkg.py"
        spec = importlib.util.spec_from_file_location("watchy_pkg", path)
        if spec is None or spec.loader is None:
            raise BuilderError(f"cannot load package tool: {path}")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module


def _selected_faces(only: Iterable[str] | None) -> tuple[dict, ...]:
    requested = tuple(only or ())
    if not requested:
        return FACES
    unknown = sorted(set(requested) - set(FACE_BY_PROJECT))
    if unknown:
        raise BuilderError("unknown face slug(s): " + ", ".join(unknown))
    selected = {slug for slug in requested}
    return tuple(face for face in FACES if face["project"] in selected)


def manifest(face: dict) -> dict:
    return {key: face[key] for key in (
        "abi_major", "abi_minor", "assets", "capabilities", "id",
        "max_runtime_bytes", "name", "type", "version",
    )}


def _idf_command(project: Path, build_dir: Path, action: str) -> list[object]:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise BuilderError("IDF_PATH is not set; use --dry-run to inspect the matrix")
    return [sys.executable, Path(idf_path) / "tools" / "idf.py",
            "-C", project, "-B", build_dir, action] + (["so"] if action == "build" else [])


def _discover_elf(build_dir: Path, watchy_pkg) -> Path:
    """Find exactly one real package ELF by its contents, never its filename."""
    candidates: list[Path] = []
    if not build_dir.is_dir():
        raise BuilderError(f"build output directory is missing: {build_dir}")
    for path in sorted((candidate for candidate in build_dir.rglob("*") if candidate.is_file()),
                       key=lambda item: item.relative_to(build_dir).as_posix()):
        try:
            content = path.read_bytes()
        except OSError:
            continue
        if content[:7] != b"\x7fELF\x01\x01\x01":
            continue
        try:
            audit = watchy_pkg.audit_elf_symbols(content)
        except watchy_pkg.PackageError:
            continue
        if audit["defined_global_functions"] == [watchy_pkg.ENTRY_POINT] and not audit["undefined_symbols"]:
            candidates.append(path)
    if len(candidates) != 1:
        names = ", ".join(str(path.relative_to(build_dir)) for path in candidates) or "none"
        raise BuilderError(f"expected exactly one package ELF in {build_dir}; found {names}")
    return candidates[0]


def _remove_tree(path: Path) -> None:
    if path.exists() or path.is_symlink():
        shutil.rmtree(path) if path.is_dir() and not path.is_symlink() else path.unlink()


def _write_manifest(face: dict, path: Path) -> None:
    import json
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest(face), sort_keys=True, separators=(",", ":")) + "\n",
                    encoding="utf-8")


def _promote_complete_set(staging: Path, output: Path, expected: Sequence[dict]) -> None:
    expected_names = {face["output"] for face in expected}
    actual_names = {path.name for path in staging.iterdir() if path.is_file()}
    if actual_names != expected_names:
        raise BuilderError("staging set is incomplete or contains unexpected files")
    backup = output.parent / f".{output.name}.previous-{os.getpid()}"
    if backup.exists() or backup.is_symlink():
        _remove_tree(backup)
    had_output = output.exists() or output.is_symlink()
    backup_rename_succeeded = False
    staging_rename_succeeded = False
    try:
        if had_output:
            os.replace(output, backup)
            backup_rename_succeeded = True
        os.replace(staging, output)
        staging_rename_succeeded = True
    except OSError as error:
        try:
            if staging_rename_succeeded and output.exists():
                _remove_tree(output)
            if backup_rename_succeeded and backup.exists():
                os.replace(backup, output)
        except OSError as restore_error:
            raise BuilderError(f"atomic promotion failed ({error}); rollback failed ({restore_error})") from error
        raise BuilderError(f"atomic promotion failed: {error}") from error
    if backup.exists():
        _remove_tree(backup)


def build_faces(*, output_dir: Path, only: Iterable[str] | None = None,
                reproducible: bool = False, runner: Runner | None = None,
                root: Path = ROOT) -> None:
    del reproducible  # Two rounds are always required; the flag documents intent for CI/users.
    if runner is None:
        runner = run
    faces = _selected_faces(only)
    output_dir = Path(output_dir)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    staging = output_dir.parent / f".{output_dir.name}.staging"
    work_root = output_dir.parent / f".{output_dir.name}.work"
    _remove_tree(staging)
    _remove_tree(work_root)
    staging.mkdir(parents=True)
    work_root.mkdir(parents=True)
    pkg_tool = root / "tools" / "watchy_pkg.py"
    try:
        for face in faces:
            project = root / "first_party" / "watchfaces" / face["project"]
            if not project.is_dir():
                raise BuilderError(f"renderer project is not present: {project}")
            face_work = work_root / face["project"]
            manifest_path = face_work / "manifest.json"
            assets_path = project / "assets"
            if not assets_path.is_dir():
                assets_path = face_work / "assets"
                assets_path.mkdir(parents=True)
            _write_manifest(face, manifest_path)
            artifacts: list[bytes] = []
            for round_name in ("round-1", "round-2"):
                build_dir = face_work / round_name
                # The clean is deliberately delegated to the native build
                # system.  Do not replace this with Python tree deletion.
                runner(_idf_command(project, build_dir, "clean"), root)
                runner(_idf_command(project, build_dir, "build"), root)
                elf = _discover_elf(build_dir, _watchy_pkg_module())
                round_output = staging / f".{face['output']}.{round_name}.wpk"
                runner([sys.executable, pkg_tool, "audit-elf", "--elf", elf], root)
                runner([sys.executable, pkg_tool, "build", "--manifest", manifest_path,
                        "--elf", elf, "--assets", assets_path, "--output", round_output], root)
                runner([sys.executable, pkg_tool, "verify", round_output], root)
                try:
                    artifacts.append(round_output.read_bytes())
                except OSError as error:
                    raise BuilderError(f"package build produced no output: {round_output}") from error
                round_output.unlink(missing_ok=True)
            if artifacts[0] != artifacts[1]:
                raise BuilderError(f"non-deterministic output for {face['id']}")
            final = staging / face["output"]
            final.write_bytes(artifacts[0])
            if len(artifacts[0]) > _watchy_pkg_module().MAX_WPK:
                raise BuilderError(f"WPK exceeds 80 KiB: {final}")
        _promote_complete_set(staging, output_dir, faces)
    finally:
        _remove_tree(work_root)
        if staging.exists():
            _remove_tree(staging)


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="build_first_party.py",
                                     description="build audited first-party Watchy faces")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build" / "first-party")
    parser.add_argument("--list", action="store_true", help="list the fixed face matrix")
    parser.add_argument("--dry-run", action="store_true", help="show selected projects without building")
    parser.add_argument("--only", nargs="+", action="append", metavar="SLUG",
                        help="build only these hyphenated project slugs")
    parser.add_argument("--reproducible", action="store_true",
                        help="build two clean rounds and require byte-identical WPKs")
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    try:
        try:
            args = make_parser().parse_args(argv)
        except SystemExit as error:
            return int(error.code)
        only = tuple(slug for group in (args.only or ()) for slug in group)
        faces = _selected_faces(only)
        if args.list:
            for face in faces:
                print(face["id"], face["output"])
            return 0
        if args.dry_run:
            for face in faces:
                print("would-build", face["id"], ROOT / "first_party" / "watchfaces" / face["project"])
            return 0
        build_faces(output_dir=args.output_dir, only=only, reproducible=args.reproducible)
        return 0
    except Exception as error:
        # PackageError and subprocess failures are intentionally rendered as a
        # short CLI diagnostic; users should never receive a Python traceback.
        print(f"watchy-build: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
