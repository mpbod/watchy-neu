#!/usr/bin/env python3
"""Generate reproducible, one-bit Watchy font strikes from vendored OTFs."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Iterable

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
CONFIG_RELATIVE_PATH = Path("tools/font_strikes.json")
PROVENANCE_LOCK_RELATIVE_PATH = Path("tools/font_provenance.lock.json")
GENERATOR_RELATIVE_PATH = Path("tools/generate_fonts.py")
HEADER_RELATIVE_PATH = Path("sdk/ui/generated/watchy_fonts.h")
SOURCE_RELATIVE_PATH = Path("sdk/ui/generated/watchy_fonts.c")
IBM_FONT_DIRECTORY = Path("assets/fonts/ibm-plex-mono")
HEROS_FONT_DIRECTORY = Path("assets/fonts/tex-gyre-heros")
APPROVED_FONT_PATHS = (
    Path("assets/fonts/ibm-plex-mono/IBMPlexMono-Bold.otf"),
    Path("assets/fonts/ibm-plex-mono/IBMPlexMono-Medium.otf"),
    Path("assets/fonts/ibm-plex-mono/IBMPlexMono-Regular.otf"),
    Path("assets/fonts/ibm-plex-mono/IBMPlexMono-SemiBold.otf"),
    Path("assets/fonts/tex-gyre-heros/texgyreheros-bold.otf"),
    Path("assets/fonts/tex-gyre-heros/texgyreheros-regular.otf"),
)
APPROVED_LICENSE_PATHS = (
    Path("assets/fonts/ibm-plex-mono/OFL.txt"),
    Path("assets/fonts/tex-gyre-heros/GUST-FONT-LICENSE.txt"),
    Path("assets/fonts/tex-gyre-heros/LPPL-1.3c.txt"),
)
FONT_PATHS_BY_NAME = {path.name: path for path in APPROVED_FONT_PATHS}
UINT8_MAX = (1 << 8) - 1
UINT16_MAX = (1 << 16) - 1
UINT32_MAX = (1 << 32) - 1
INT8_MIN, INT8_MAX = -(1 << 7), (1 << 7) - 1
INT16_MIN, INT16_MAX = -(1 << 15), (1 << 15) - 1


@dataclass(frozen=True)
class Glyph:
    codepoint: int
    bitmap_offset: int
    bearing_x: int
    bearing_y: int
    width: int
    height: int
    advance: int
    stride: int


@dataclass(frozen=True)
class RenderedOutputs:
    header: bytes
    source: bytes


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_config(root: Path) -> dict:
    return json.loads((root / CONFIG_RELATIVE_PATH).read_text(encoding="utf-8"))


def load_provenance_lock(root: Path) -> dict:
    return json.loads((root / PROVENANCE_LOCK_RELATIVE_PATH).read_text(encoding="utf-8"))


def resolve_regular_vendored_file(root: Path, relative: Path, allowlist: tuple[Path, ...], kind: str) -> Path:
    if relative not in allowlist:
        raise ValueError(f"{kind} path is not in the approved allowlist: {relative}")
    if root.is_symlink() or not root.is_dir():
        raise ValueError(f"trusted repository root must be a regular directory: {root}")
    trusted_root = root.resolve(strict=True)
    candidate = root
    for component in relative.parts:
        candidate /= component
        if candidate.is_symlink():
            raise ValueError(f"{kind} path must not contain a symlink: {relative}")
    path = root / relative
    expected_directory = root / relative.parent
    if not path.is_file():
        raise ValueError(f"{kind} path must be a regular file in its vendored directory: {relative}")
    resolved = path.resolve(strict=True)
    expected = trusted_root.joinpath(*relative.parts)
    if resolved != expected or resolved.parent != expected_directory.resolve(strict=True):
        raise ValueError(f"{kind} path must resolve inside its exact vendored directory: {relative}")
    return path


def resolve_locked_font(root: Path, entry: dict) -> Path:
    return resolve_regular_vendored_file(root, Path(entry["path"]), APPROVED_FONT_PATHS, "font")


def resolve_locked_license(root: Path, entry: dict) -> Path:
    return resolve_regular_vendored_file(root, Path(entry["path"]), APPROVED_LICENSE_PATHS, "license")


def verify_provenance_lock(root: Path) -> dict:
    lock = load_provenance_lock(root)
    entries = lock.get("fonts", [])
    if lock.get("version") != 1:
        raise ValueError("font provenance lock version must be 1")
    if [Path(entry.get("path", "")) for entry in entries] != list(APPROVED_FONT_PATHS):
        raise ValueError("font provenance lock must contain the exact approved allowlist")
    discovered = {
        path.relative_to(root)
        for directory in (root / IBM_FONT_DIRECTORY, root / HEROS_FONT_DIRECTORY)
        for path in directory.glob("*.otf")
    }
    if discovered != set(APPROVED_FONT_PATHS):
        raise ValueError("vendored font directories must contain the exact approved OTF allowlist")
    for entry in entries:
        path = resolve_locked_font(root, entry)
        if sha256(path) != entry.get("sha256"):
            raise ValueError(f"font hash mismatch: {entry['path']}")
        source = entry.get("source", {})
        if not source.get("url", "").startswith("https://"):
            raise ValueError(f"font source must use HTTPS: {entry['path']}")
        if not (source.get("revision") or (source.get("archive_sha256") and source.get("archive_member"))):
            raise ValueError(f"font source must be immutable or archive-pinned: {entry['path']}")
    return lock


def canonicalize_license_text(text: str) -> bytes:
    """Normalize only line ending, trailing horizontal whitespace, and final newline."""
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    normalized = "\n".join(line.rstrip(" \t") for line in normalized.split("\n"))
    return (normalized.rstrip("\n") + "\n").encode("utf-8")


def verify_license_lock(root: Path) -> dict:
    lock = load_provenance_lock(root)
    entries = lock.get("licenses", [])
    if [Path(entry.get("path", "")) for entry in entries] != list(APPROVED_LICENSE_PATHS):
        raise ValueError("license lock must contain the exact approved allowlist")
    for entry in entries:
        path = resolve_locked_license(root, entry)
        digest = hashlib.sha256(canonicalize_license_text(path.read_text(encoding="utf-8"))).hexdigest()
        if digest != entry.get("canonical_sha256"):
            raise ValueError(f"canonical license hash mismatch: {entry['path']}")
    return lock


def source_path(root: Path, filename: str) -> Path:
    try:
        relative = FONT_PATHS_BY_NAME[filename]
    except KeyError as error:
        raise ValueError(f"font is not a vendored source: {filename}") from error
    return resolve_locked_font(root, {"path": str(relative)})


def source_hashes(root: Path) -> dict[str, str]:
    lock = verify_provenance_lock(root)
    return {Path(entry["path"]).name: entry["sha256"] for entry in lock["fonts"]}


def strike_codepoints(config: dict, strike_id: str) -> list[int]:
    strike = next(strike for strike in config["strikes"] if strike["id"] == strike_id)
    return sorted({ord(character) for character in strike.get("glyphs", config["glyphs"])})


def validate_configured_codepoints(root: Path) -> None:
    config = load_config(root)
    by_font: dict[str, set[int]] = {}
    strike_ids: dict[str, list[str]] = {}
    for strike in config["strikes"]:
        by_font.setdefault(strike["font"], set()).update(strike_codepoints(config, strike["id"]))
        strike_ids.setdefault(strike["font"], []).append(strike["id"])
    for filename, codepoints in sorted(by_font.items()):
        with TTFont(source_path(root, filename), lazy=True) as font:
            cmap = font.getBestCmap() or {}
        missing = sorted(codepoint for codepoint in codepoints if codepoint not in cmap)
        if missing:
            rendered = ", ".join(f"U+{codepoint:04X}" for codepoint in missing)
            raise ValueError(f"{filename} missing cmap codepoints for {', '.join(strike_ids[filename])}: {rendered}")


def require_range(strike_id: str, field: str, value: int, minimum: int, maximum: int,
                  codepoint: int | None = None) -> None:
    if not minimum <= value <= maximum:
        glyph = "" if codepoint is None else f" U+{codepoint:04X}"
        raise ValueError(f"{strike_id}{glyph} {field}={value} is outside [{minimum}, {maximum}]")


def validate_glyph_abi_range(strike_id: str, glyph: Glyph, bitmap_length: int) -> None:
    require_range(strike_id, "codepoint", glyph.codepoint, 0, UINT32_MAX, glyph.codepoint)
    require_range(strike_id, "bitmap_offset", glyph.bitmap_offset, 0, UINT32_MAX, glyph.codepoint)
    require_range(strike_id, "bearing_x", glyph.bearing_x, INT16_MIN, INT16_MAX, glyph.codepoint)
    require_range(strike_id, "bearing_y", glyph.bearing_y, INT16_MIN, INT16_MAX, glyph.codepoint)
    require_range(strike_id, "width", glyph.width, 0, UINT16_MAX, glyph.codepoint)
    require_range(strike_id, "height", glyph.height, 0, UINT16_MAX, glyph.codepoint)
    require_range(strike_id, "advance", glyph.advance, 0, UINT16_MAX, glyph.codepoint)
    require_range(strike_id, "stride", glyph.stride, 0, UINT16_MAX, glyph.codepoint)
    end = glyph.bitmap_offset + glyph.stride * glyph.height
    if end > bitmap_length:
        raise ValueError(f"{strike_id} U+{glyph.codepoint:04X} bitmap extent {end} exceeds {bitmap_length}")


def validate_strike_abi_range(strike_id: str, glyphs: list[Glyph], bitmap_length: int,
                              px: int, ascent: int, descent: int, line_height: int) -> None:
    require_range(strike_id, "glyph_count", len(glyphs), 0, UINT16_MAX)
    require_range(strike_id, "bitmap_length", bitmap_length, 0, UINT32_MAX)
    require_range(strike_id, "px", px, 0, UINT8_MAX)
    require_range(strike_id, "ascent", ascent, INT8_MIN, INT8_MAX)
    require_range(strike_id, "descent", descent, INT8_MIN, INT8_MAX)
    require_range(strike_id, "line_height", line_height, 0, UINT8_MAX)
    for glyph in glyphs:
        validate_glyph_abi_range(strike_id, glyph, bitmap_length)


def pack_rows_msb_first(mask: Image.Image, threshold: int) -> tuple[bytes, int]:
    width, height = mask.size
    stride = (width + 7) // 8
    packed = bytearray(stride * height)
    pixels = mask.load()
    for y in range(height):
        for x in range(width):
            if pixels[x, y] >= threshold:
                packed[y * stride + x // 8] |= 0x80 >> (x % 8)
    return bytes(packed), stride


def rasterize(font: ImageFont.FreeTypeFont, codepoint: int, threshold: int) -> tuple[Glyph, bytes]:
    character = chr(codepoint)
    left, top, right, bottom = font.getbbox(character, anchor="ls")
    width, height = max(0, right - left), max(0, bottom - top)
    advance = round(font.getlength(character))
    if not width or not height:
        return Glyph(codepoint, 0, left, -top, 0, 0, advance, 0), b""
    mask = Image.new("L", (width, height), 0)
    ImageDraw.Draw(mask).text((-left, -top), character, fill=255, font=font, anchor="ls")
    bitmap, stride = pack_rows_msb_first(mask, threshold)
    return Glyph(codepoint, 0, left, -top, width, height, advance, stride), bitmap


def c_bytes(data: bytes) -> Iterable[str]:
    for offset in range(0, len(data), 12):
        yield ", ".join(f"0x{value:02x}" for value in data[offset:offset + 12])


def provenance_lines(root: Path) -> list[str]:
    config_hash = sha256(root / CONFIG_RELATIVE_PATH)
    generator_hash = sha256(root / GENERATOR_RELATIVE_PATH)
    lock_hash = sha256(root / PROVENANCE_LOCK_RELATIVE_PATH)
    return [
        f" * generator sha256: {generator_hash}",
        f" * config sha256: {config_hash}",
        f" * provenance lock sha256: {lock_hash}",
        *(f" * {name} sha256: {digest}" for name, digest in source_hashes(root).items()),
    ]


def render_header(root: Path, strike_ids: list[str]) -> str:
    exports = "\n".join(f"extern watchy_font_t watchy_font_{strike_id};" for strike_id in strike_ids)
    banner = "\n".join(("/* Generated by tools/generate_fonts.py; do not edit.", *provenance_lines(root), " */"))
    return f"""{banner}
#ifndef WATCHY_FONTS_H
#define WATCHY_FONTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

typedef struct {{
    uint32_t codepoint;
    uint32_t bitmap_offset;
    int16_t bearing_x;
    int16_t bearing_y;
    uint16_t width;
    uint16_t height;
    uint16_t advance;
    uint16_t stride;
}} watchy_font_glyph_t;

typedef struct {{
    const watchy_font_glyph_t *glyphs;
    const uint8_t *bitmap;
    uint16_t glyph_count;
    uint8_t px;
    int8_t ascent;
    int8_t descent;
    uint8_t line_height;
}} watchy_font_t;

{exports}

const watchy_font_glyph_t *watchy_font_find_glyph(const watchy_font_t *font, uint32_t codepoint);

#ifdef __cplusplus
}}
#endif

#endif
"""


def render_source(root: Path, config: dict) -> str:
    lines = [
        "/* Generated by tools/generate_fonts.py; do not edit.",
        *provenance_lines(root),
        " */",
        "#include \"watchy_fonts.h\"",
        "",
    ]
    for strike in sorted(config["strikes"], key=lambda item: item["id"]):
        strike_id = strike["id"]
        font = ImageFont.truetype(source_path(root, strike["font"]), strike["px"], layout_engine=ImageFont.Layout.BASIC)
        glyphs: list[Glyph] = []
        bitmap = bytearray()
        for codepoint in sorted({ord(character) for character in strike.get("glyphs", config["glyphs"])}):
            glyph, packed = rasterize(font, codepoint, strike["threshold"])
            glyphs.append(Glyph(codepoint, len(bitmap), glyph.bearing_x, glyph.bearing_y,
                                glyph.width, glyph.height, glyph.advance, glyph.stride))
            bitmap.extend(packed)
        ascent, descent = font.getmetrics()
        validate_strike_abi_range(strike_id, glyphs, len(bitmap), strike["px"], ascent, -descent,
                                  ascent + descent)
        lines.append(f"static const uint8_t bitmap_{strike_id}[] = {{")
        lines.extend(f"    {row}," for row in c_bytes(bitmap))
        lines.append("};")
        lines.append(f"static const watchy_font_glyph_t glyphs_{strike_id}[] = {{")
        lines.extend(
            "    {%d, %d, %d, %d, %d, %d, %d, %d}," % (
                glyph.codepoint, glyph.bitmap_offset, glyph.bearing_x, glyph.bearing_y,
                glyph.width, glyph.height, glyph.advance, glyph.stride,
            ) for glyph in glyphs
        )
        lines.append("};")
        lines.append(
            f"watchy_font_t watchy_font_{strike_id} = {{glyphs_{strike_id}, bitmap_{strike_id}, "
            f"{len(glyphs)}, {strike['px']}, {ascent}, {-descent}, {ascent + descent}}};"
        )
        lines.append("")
    lines.extend((
        "const watchy_font_glyph_t *watchy_font_find_glyph(const watchy_font_t *font, uint32_t codepoint) {",
        "    size_t low = 0;",
        "    size_t high = font->glyph_count;",
        "    while (low < high) {",
        "        const size_t middle = low + (high - low) / 2;",
        "        const uint32_t candidate = font->glyphs[middle].codepoint;",
        "        if (candidate == codepoint) return &font->glyphs[middle];",
        "        if (candidate < codepoint) low = middle + 1; else high = middle;",
        "    }",
        "    return NULL;",
        "}",
        "",
    ))
    return "\n".join(lines)


def render_outputs(root: Path) -> RenderedOutputs:
    verify_provenance_lock(root)
    verify_license_lock(root)
    validate_configured_codepoints(root)
    config = load_config(root)
    strike_ids = [strike["id"] for strike in sorted(config["strikes"], key=lambda item: item["id"])]
    return RenderedOutputs(render_header(root, strike_ids).encode("ascii"), render_source(root, config).encode("ascii"))


def generated_paths(root: Path) -> tuple[Path, Path]:
    return root / HEADER_RELATIVE_PATH, root / SOURCE_RELATIVE_PATH


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if checked-in artifacts differ")
    arguments = parser.parse_args()
    outputs = render_outputs(REPOSITORY_ROOT)
    header_path, source_path = generated_paths(REPOSITORY_ROOT)
    if arguments.check:
        if header_path.read_bytes() == outputs.header and source_path.read_bytes() == outputs.source:
            print("font strikes are up to date")
            return 0
        print("font strikes differ; run tools/generate_fonts.py", file=sys.stderr)
        return 1
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_bytes(outputs.header)
    source_path.write_bytes(outputs.source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
