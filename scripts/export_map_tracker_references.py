"""Export local outdoor minimap references from an extracted SoH O2R (stdlib only)."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def c_array(text: str, name: str) -> list[int]:
    match = re.search(r"\b" + re.escape(name) + r"\s*\[[^;=]+?=\s*\{(.*?)\};", text, re.S)
    if not match:
        raise ValueError(f"Missing C array: {name}")
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", match[1], flags=re.S)
    return [int(value) for value in re.findall(r"-?\d+", body)]


def decode_texture(data: bytes) -> tuple[int, int, bytes]:
    if len(data) < 80 or data[0] not in (0, 1):
        raise ValueError("Invalid binary resource header")
    endian = ">" if data[0] == 1 else "<"
    version = struct.unpack_from(endian + "I", data, 8)[0]
    kind, width, height = struct.unpack_from(endian + "III", data, 64)
    size_offset = {0: 76, 1: 88}.get(version)
    if size_offset is None or not (0 < width <= 4096 and 0 < height <= 4096):
        raise ValueError("Unsupported texture version/dimensions")
    if len(data) < size_offset + 4:
        raise ValueError("Truncated texture header")
    size = struct.unpack_from(endian + "I", data, size_offset)[0]
    raw = data[size_offset + 4: size_offset + 4 + size]
    pixels = width * height
    required = {5: (pixels + 1) // 2, 6: pixels, 7: (pixels + 1) // 2, 8: pixels, 9: pixels * 2}.get(kind)
    if required is None or len(raw) != size or size < required:
        raise ValueError(f"Unsupported/truncated minimap texture type {kind}")
    rgb = bytearray()
    for i in range(pixels):
        if kind in (5, 7):
            nibble = (raw[i // 2] >> (4 if i % 2 == 0 else 0)) & 15
            intensity = nibble * 17 if kind == 5 else (nibble >> 1) * 255 // 7 * (nibble & 1)
        elif kind == 6:
            intensity = raw[i]
        elif kind == 8:
            intensity = (raw[i] >> 4) * 17 * (raw[i] & 15) // 15
        else:
            intensity = raw[2 * i] * raw[2 * i + 1] // 255
        # Opaque cyan on a dark background stays readable in an editor without compositing support.
        rgb.extend((8, max(16, intensity), max(20, intensity)))
    return width, height, bytes(rgb)


def png_bytes(width: int, height: int, rgb: bytes) -> bytes:
    if len(rgb) != width * height * 3:
        raise ValueError("Incorrect pixel buffer size")
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def export_references(archive: Path, output: Path) -> dict:
    map_data = (ROOT / "soh/src/code/z_map_data.c").read_text(encoding="utf-8")
    map_code = (ROOT / "soh/src/code/z_map_exp.c").read_text(encoding="utf-8")
    textures_header = (ROOT / "soh/assets/textures/map_grand_static/map_grand_static.h").read_text(encoding="utf-8")
    scene_table = (ROOT / "soh/include/tables/scene_table.h").read_text(encoding="utf-8")
    identity_code = (ROOT / "soh/soh/Enhancements/randomizer/map_tracker_player_position.cpp").read_text(encoding="utf-8")
    ids_header = (ROOT / "soh/soh/Enhancements/randomizer/map_tracker_link_resolver.h").read_text(encoding="utf-8")
    ids = dict(re.findall(r'\b(\w+)\s*=\s*"([^"]+)"', ids_header))
    scene_ids = {name: int(number, 16) for number, name in re.findall(
        r"/\* 0x([\dA-Fa-f]+) \*/ DEFINE_SCENE\([^,]+, [^,]+, (SCENE_\w+),", scene_table)}
    map_ids = {scene: ids[name] for scene, name in re.findall(r"case (SCENE_\w+): return (\w+);", identity_code)}
    symbols_match = re.search(r"const char\* minimapTableOW\[\] = \{(.*?)\};", map_code, re.S)
    if not symbols_match:
        raise ValueError("Outdoor minimap table missing")
    symbols = re.findall(r"\bg\w+Tex\b", symbols_match[1])
    paths = dict(re.findall(r'#define d(g\w+) "__OTR__([^"]+)"', textures_header))
    xs, ys, compass = (c_array(map_data, name) for name in ("sOwMinimapPosX", "sOwMinimapPosY", "sOwCompassInfo"))
    if len(symbols) != 24 or len(xs) != 24 or len(ys) != 24 or len(compass) != 96:
        raise ValueError("Outdoor table layout changed: exporter must be updated")
    scenes = {number - scene_ids["SCENE_HYRULE_FIELD"]: name for name, number in scene_ids.items()
              if scene_ids["SCENE_HYRULE_FIELD"] <= number <= scene_ids["SCENE_OUTSIDE_GANONS_CASTLE"]}
    variants = {20: (2, "shadow_explored"), 21: (6, "drained"), 22: (9, "broken_bridge"), 23: (12, "membership")}
    references = []
    with zipfile.ZipFile(archive) as source:
        for index, symbol in enumerate(symbols):
            base, variant = variants.get(index, (index, "default"))
            scene = scenes[base]
            map_id = map_ids[scene]
            reference_id = f"{map_id}.{variant}"
            resource = paths[symbol]
            width, height, rgb = decode_texture(source.read(resource))
            sx, sy, ox, oy = compass[base * 4:base * 4 + 4]
            if sx <= 0 or sy <= 0:
                raise ValueError(f"Invalid compass scale for {scene}")
            image = f"images/{reference_id}.png"
            target = output / image
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(png_bytes(width, height, rgb))
            references.append({
                "id": reference_id, "map_id": map_id, "scene_id": scene_ids[scene], "scene_name": scene,
                "room_id": None, "setup_ids": None, "layout": "any", "variant": variant,
                "image": image, "image_size": [width, height], "texture_resource": resource,
                "world_to_image": [[1 / (10 * sx), 0.0, 160 + ox / 10 - xs[base]],
                                   [0.0, 1 / (10 * sy), 120 - oy / 10 - ys[base]]],
                "samples": [],
            })
    manifest = {"version": 1, "source": {"archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                "scope": "outdoor", "coordinates": "canonical world X/Z to native top-left image pixels"},
                "references": references}
    output.mkdir(parents=True, exist_ok=True)
    (output / "position_reference.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=ROOT / "x64/Debug/oot.o2r")
    parser.add_argument("--output", type=Path, default=ROOT / "build/map_tracker_references")
    args = parser.parse_args()
    archive, output = args.archive.resolve(), args.output.resolve()
    if output == archive.parent or archive.is_relative_to(output):
        parser.error("Output must not contain the input archive")
    try:
        manifest = export_references(archive, output)
    except (OSError, ValueError, KeyError, struct.error, zipfile.BadZipFile) as error:
        parser.exit(1, f"Export failed: {error}\n")
    print(f"Exported {len(manifest['references'])} outdoor minimaps to {output}")
    print("Import position_reference.json in the pack editor. Do not publish these local game images.")


if __name__ == "__main__":
    main()
