import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/package_map_tracker_release.py"
spec = importlib.util.spec_from_file_location("release_packaging", SCRIPT)
packaging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packaging)


class ReleasePackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="soh-validation-package-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.config = self.root / "defaults.json"
        self.config.write_text(json.dumps({"CVars": {"gTrackers": {"MapTracker": {"DisplayMode": 0}}}}))
        self.readme = self.root / "README.md"
        self.readme.write_text("Synthetic validation fixture")
        self.pack = self.root / "map-pack.zip"
        self.otr = self.root / "soh.o2r"
        with zipfile.ZipFile(self.pack, "w") as archive:
            archive.writestr("soh-map-tracker/maps.json", "[]")
        with zipfile.ZipFile(self.otr, "w") as archive:
            archive.writestr("textures/map_tracker/pause_map_tracker.png", b"synthetic texture")
        self.native = self.root / "native.zip"
        with zipfile.ZipFile(self.native, "w") as archive:
            archive.writestr("soh.exe", b"synthetic executable")
            archive.writestr("soh.o2r", b"outdated resource")
        self.args = argparse.Namespace(platform="windows", package=self.native,
                                      output=self.root / "SoH_AP_Map_Windows.zip", config=self.config,
                                      map_pack=self.pack, readme=self.readme, soh_otr=self.otr,
                                      verify_pause_texture=None)

    def test_windows_flat_package_overrides_old_resources(self):
        packaging.assemble_release(self.args)
        with zipfile.ZipFile(self.args.output) as archive:
            self.assertEqual(set(archive.namelist()), {"soh.exe", "soh.o2r", "readme.txt",
                                                    "shipofharkinian.json", "mods/check_tracker_map_pack/map-pack.zip"})
            self.assertEqual(archive.read("soh.o2r"), self.otr.read_bytes())
            self.assertEqual(archive.read("shipofharkinian.json"), self.config.read_bytes())
            self.assertEqual(archive.read("mods/check_tracker_map_pack/map-pack.zip"), self.pack.read_bytes())

    def test_linux_and_macos_contract(self):
        for platform, filename in (("linux", "soh.appimage"), ("macos", "SoH.dmg")):
            with self.subTest(platform=platform):
                self.args.platform = platform
                self.args.package = self.root / filename
                self.args.package.write_bytes(b"synthetic native package")
                packaging.assemble_release(self.args)
                with zipfile.ZipFile(self.args.output) as archive:
                    self.assertIn(filename, archive.namelist())
                    self.assertIn("soh.o2r", archive.namelist())
                    self.assertNotIn("native.zip", archive.namelist())

    def test_each_required_file_missing_fails(self):
        for field in ("package", "config", "map_pack", "readme", "soh_otr"):
            with self.subTest(field=field):
                args = argparse.Namespace(**vars(self.args))
                setattr(args, field, self.root / "missing")
                with self.assertRaises(FileNotFoundError):
                    packaging.assemble_release(args)
                self.assertFalse(args.output.exists())

    def test_invalid_config_and_profiles_fail(self):
        for content in ("not JSON", "[]", '{"CVars":[],"Archipelago":{}}',
                        '{"CVars":{},"Archipelago":{"Connections":{}}}'):
            with self.subTest(content=content):
                self.config.write_text(content)
                with self.assertRaises(ValueError):
                    packaging.assemble_release(self.args)

    def test_invalid_resource_archive_fails(self):
        self.pack.write_bytes(b"not a ZIP")
        with self.assertRaises(zipfile.BadZipFile):
            packaging.assemble_release(self.args)

    def test_private_and_unsafe_native_entries_fail(self):
        for name in ("../escape.txt", "C:/escape.txt", "Save/file1.sav", "oot.o2r", "logs/log.txt",
                     "ap-client-uuid", "game.z64"):
            with self.subTest(name=name):
                with zipfile.ZipFile(self.native, "w") as archive:
                    archive.writestr(name, "private/synthetic")
                with self.assertRaises(ValueError):
                    packaging.assemble_release(self.args)

    def test_texture_source_must_match(self):
        source = self.root / "pause.png"
        source.write_bytes(b"synthetic texture")
        self.args.verify_pause_texture = source
        packaging.assemble_release(self.args)
        previous_release = self.args.output.read_bytes()
        source.write_bytes(b"new texture")
        with self.assertRaisesRegex(ValueError, "Outdated resource"):
            packaging.assemble_release(self.args)
        self.assertEqual(self.args.output.read_bytes(), previous_release)

    def test_cli_requires_resources(self):
        result = subprocess.run([sys.executable, str(SCRIPT), "--platform", "windows", "--package", str(self.native),
                                 "--output", str(self.args.output), "--config", str(self.config), "--map-pack",
                                 str(self.pack), "--readme", str(self.readme)], capture_output=True, timeout=15)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"--soh-otr", result.stderr)

    def test_committed_defaults(self):
        config = json.loads((ROOT / "packaging/shipofharkinian.json").read_text())
        tracker = config["CVars"]["gTrackers"]["MapTracker"]
        self.assertEqual(tracker["DisplayMode"], 0)
        self.assertEqual(tracker["NativeCustomBackground"], 1)
        self.assertEqual(tracker["ShowDebugInfo"], 0)
        self.assertLessEqual(tracker["FontSize"], 1.3)
        self.assertNotIn("Archipelago", config)


if __name__ == "__main__":
    unittest.main()
