#!/usr/bin/env python3
"""Assemble a distributable SoH Map Tracker archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("windows", "linux", "macos"), required=True)
    parser.add_argument("--package", type=Path, required=True, help="Native CPack output")
    parser.add_argument("--output", type=Path, required=True, help="Final ZIP path")
    parser.add_argument("--config", type=Path, required=True, help="Default shipofharkinian.json")
    parser.add_argument("--map-pack", type=Path, required=True, help="Map resource pack ZIP")
    parser.add_argument("--readme", type=Path, required=True)
    parser.add_argument("--soh-otr", type=Path, required=True)
    parser.add_argument("--verify-pause-texture", type=Path,
                        help="Require the archived pause texture to match this source PNG")
    return parser.parse_args()


def copy_file(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(f"Required release file is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def copy_native_package(package: Path, platform: str, staging: Path) -> None:
    if not package.is_file():
        raise FileNotFoundError(f"Native package is missing: {package}")

    if platform == "windows":
        with zipfile.ZipFile(package) as archive:
            for entry in archive.infolist():
                path = Path(entry.filename.replace("\\", "/"))
                if path.is_absolute() or ".." in path.parts or ":" in entry.filename:
                    raise ValueError(f"Unsafe native package entry: {entry.filename}")
                if path.name.lower() in {"oot.o2r", "oot-mq.o2r", "oot.otr", "oot-mq.otr", "ap-client-uuid"} or \
                        path.suffix.lower() in {".z64", ".n64", ".v64", ".sav", ".bak"} or \
                        any(part.lower() in {"save", "logs"} for part in path.parts):
                    raise ValueError(f"Private/game-data entry in native package: {entry.filename}")
            archive.extractall(staging)
        return

    destination_name = {
        "linux": "soh.appimage",
        "macos": "SoH.dmg",
    }[platform]
    copy_file(package, staging / destination_name)


def create_archive(staging: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    # Preserve the last good release if writing its replacement fails.
    with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".zip", delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        with zipfile.ZipFile(temporary_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(staging.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(staging).as_posix())
        os.replace(temporary_path, output)
    finally:
        temporary_path.unlink(missing_ok=True)


def verify_archive_asset(archive_path: Path, source: Path, entry: str) -> None:
    with zipfile.ZipFile(archive_path) as archive:
        actual = hashlib.sha256(archive.read(entry)).digest()
    if actual != hashlib.sha256(source.read_bytes()).digest():
        raise ValueError(f"Outdated resource in {archive_path}: {entry}")


def assemble_release(args: argparse.Namespace) -> None:
    for path in (args.package, args.config, args.map_pack, args.readme, args.soh_otr):
        if not path.is_file():
            raise FileNotFoundError(f"Required release file is missing: {path}")
    config = json.loads(args.config.read_text(encoding="utf-8"))
    if not isinstance(config, dict) or not isinstance(config.get("CVars"), dict):
        raise ValueError("Release configuration must be an object containing CVars")
    if "Archipelago" in config:
        raise ValueError("Release configuration contains private connection profiles")
    for archive_path in (args.map_pack, args.soh_otr):
        with zipfile.ZipFile(archive_path) as archive:
            if archive.testzip() is not None:
                raise ValueError(f"Corrupt resource archive: {archive_path}")
    if args.verify_pause_texture is not None:
        verify_archive_asset(args.soh_otr, args.verify_pause_texture, "textures/map_tracker/pause_map_tracker.png")
    with tempfile.TemporaryDirectory(prefix="soh-map-tracker-release-") as temporary_directory:
        staging = Path(temporary_directory)
        copy_native_package(args.package, args.platform, staging)
        copy_file(args.readme, staging / "readme.txt")
        copy_file(args.config, staging / "shipofharkinian.json")
        copy_file(
            args.map_pack,
            staging / "mods" / "check_tracker_map_pack" / args.map_pack.name,
        )
        copy_file(args.soh_otr, staging / "soh.o2r")
        create_archive(staging, args.output)


def main() -> None:
    assemble_release(parse_args())


if __name__ == "__main__":
    main()
