"""Opt-in Windows game scenarios in disposable runtimes, never ordinary saves."""

import hashlib
import json
import os
import shutil
import zipfile
from pathlib import Path

from export_map_tracker_references import png_bytes

SCENARIOS = ("pause-3d", "pause-2d", "exit-loading-3d", "exit-loading-2d", "pack-invalid")


def remove_archive_links(report_directory):
    # Only generated scenario paths in this invocation, never source game archives.
    for scenario in SCENARIOS:
        for name in ("soh.o2r", "oot.o2r"):
            (report_directory / scenario / name).unlink(missing_ok=True)


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def make_pack(directory, invalid=False):
    pack_directory = directory / "mods/check_tracker_map_pack"
    pack_directory.mkdir(parents=True, exist_ok=True)
    path = pack_directory / "validation-pack.zip"
    maps = [{"id": "kokiri_forest", "name": "Validation Kokiri", "group": "Hyrule", "img": "images/map.png"},
            {"id": "overworld", "name": "Validation Overworld", "group": "Hyrule", "img": "images/map.png"}]
    checks = [{"name": name, "soh_id": identifier, "hint": "Synthetic validation hint",
               "map_locations": [{"map_id": "kokiri_forest", "x": 64, "y": 64}]} for name, identifier in (
                   ("Sword Chest", "kf_kokiri_sword_chest"), ("Token A", "kf_gs_know_it_all_house"),
                   ("Token B", "kf_gs_house_of_twins"))]
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("maps.json", json.dumps({"invalid": True} if invalid else maps))
        archive.writestr("areas/validation.json", json.dumps({"checks": checks}))
        archive.writestr("images/map.png", png_bytes(128, 128, bytes((70, 80, 40)) * (128 * 128)))
    return path


def prepare_runtime(executable, directory, assets):
    directory.mkdir()
    # The boot path checks this folder even when existing archives need no extraction.
    (directory / "assets").mkdir()
    staged = directory / "soh.exe"
    shutil.copy2(executable, staged)
    sources = {}
    # Hard links reference existing archives without duplicating/exporting ROM data.
    # No archive is modified by the scenarios; verify hashes again afterwards.
    for name in ("soh.o2r", "oot.o2r"):
        source = assets / name
        if not source.is_file():
            raise FileNotFoundError(f"Required local game archive: {source}")
        sources[name] = {"path": str(source), "sha256": digest(source)}
        os.link(source, directory / name)
    make_pack(directory)
    (directory / "shipofharkinian.json").write_text(json.dumps({
        "CVars": {"gSettings": {"Fps": 60}, "gTrackers": {"MapTracker": {"ShowDebugInfo": 0}},
                  "gDeveloperTools": {"LogLevel": 2},
                  "gWindows": {"MapTracker": 0, "MapTrackerScreen": 0}},
        "Window": {"Width": 1280, "Height": 720, "Fullscreen": {"Enabled": False}}}), encoding="utf-8")
    return staged, sources


def run_scenarios(validation, executable, assets, modes=None):
    if os.name != "nt":
        validation.add("game-e2e", "BLOCKED", "Initial isolated runtime supports Windows portable builds only")
        return
    executable = executable.resolve()
    assets = assets.resolve()
    if not executable.is_file() or any(not (assets / name).is_file() for name in ("soh.o2r", "oot.o2r")):
        validation.add("game-e2e", "BLOCKED", "Provide a validation-enabled Debug EXE and local soh.o2r/oot.o2r archives")
        return
    for mode in modes or SCENARIOS:
        directory = validation.directory / mode
        try:
            staged, sources = prepare_runtime(executable, directory, assets)
        except OSError as error:
            validation.add(mode, "BLOCKED", f"Cannot stage isolated runtime without copying game archives: {error}")
            continue
        if mode == "pack-invalid":
            make_pack(directory, invalid=True)
        outcomes = []
        # Refuse an incomplete fixture before reading any game config/save files.
        code, _ = validation.command(f"{mode}-missing-manifest", [staged, "--quality-validation"], timeout=15,
                                     cwd=directory, env=dict(os.environ, SHIP_HOME=str(directory)))
        validation.add(f"{mode}-isolation-guard", "PASSED" if code == 2 else "FAILED")
        if code != 2:
            continue
        scenarios = (mode, "persist") if mode.startswith("pause-") else (mode,)
        for scenario in scenarios:
            manifest = {"version": 1, "disposable": True, "scenario": scenario}
            (directory / "validation-runtime.json").write_text(json.dumps(manifest), encoding="utf-8")
            result_path = directory / "validation-result.json"
            result_path.unlink(missing_ok=True)
            environment = dict(os.environ, SHIP_HOME=str(directory))
            name = f"{mode}-{scenario}"
            code, _ = validation.command(name, [staged, "--quality-validation"], timeout=90,
                                         cwd=directory, env=environment)
            if not result_path.is_file():
                validation.add(name, "FAILED", "Missing scenario result: unsupported build, crash or early exit",
                               runtime=str(directory))
                outcomes.append(False)
                break
            result = json.loads(result_path.read_text(encoding="utf-8"))
            shutil.copy2(result_path, validation.directory / f"{name}.json")
            passed = code == 0 and result.get("status") == "PASSED" and result.get("shutdown_complete") is True
            passed = passed and bool(result.get("checks")) and all(check.get("passed") is True for check in result["checks"])
            validation.add(name, "PASSED" if passed else "FAILED", result.get("reason"), runtime=str(directory),
                           checks=result.get("checks"))
            outcomes.append(passed)
            if not passed:
                break
        unchanged = all(digest(Path(source["path"])) == source["sha256"] for source in sources.values())
        validation.add(f"{mode}-assets-read-only", "PASSED" if unchanged else "FAILED")
        (directory / "assets-manifest.json").write_text(json.dumps(sources, indent=2), encoding="utf-8")
        # Never retain links to private game data in reports, including failed runs.
        for name in sources:
            (directory / name).unlink()
        if all(outcomes) and unchanged:
            staged.unlink()
