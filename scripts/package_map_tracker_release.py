#!/usr/bin/env python3
"""Assemble a distributable SoH Map Tracker archive."""

from __future__ import annotations

import argparse
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
    parser.add_argument("--soh-otr", type=Path)
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
            archive.extractall(staging)
        return

    destination_name = {
        "linux": "soh.appimage",
        "macos": "SoH.dmg",
    }[platform]
    copy_file(package, staging / destination_name)


def create_archive(staging: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(staging.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(staging).as_posix())


def main() -> None:
    args = parse_args()
    with tempfile.TemporaryDirectory(prefix="soh-map-tracker-release-") as temporary_directory:
        staging = Path(temporary_directory)
        copy_native_package(args.package, args.platform, staging)
        copy_file(args.readme, staging / "readme.txt")
        copy_file(args.config, staging / "shipofharkinian.json")
        copy_file(
            args.map_pack,
            staging / "mods" / "check_tracker_map_pack" / args.map_pack.name,
        )
        if args.soh_otr is not None:
            copy_file(args.soh_otr, staging / "soh.o2r")
        create_archive(staging, args.output)


if __name__ == "__main__":
    main()
