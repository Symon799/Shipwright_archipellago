import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from game_validation import digest, make_pack, prepare_runtime


class GameFixtureTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="soh-fixture-safety-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_staging_does_not_import_personal_config_or_saves(self):
        source = self.root / "source"
        source.mkdir()
        (source / "soh.exe").write_bytes(b"synthetic executable")
        for name in ("soh.o2r", "oot.o2r"):
            (source / name).write_bytes(b"synthetic archive")
        (source / "shipofharkinian.json").write_text('{"PrivateProfile":"do not import"}')
        (source / "Save").mkdir()
        (source / "Save/file1.sav").write_text("personal save fixture")
        runtime = self.root / "runtime"
        executable, manifest = prepare_runtime(source / "soh.exe", runtime, source)
        self.assertEqual(executable.parent, runtime)
        self.assertFalse((runtime / "Save").exists())
        self.assertNotIn("PrivateProfile", json.loads((runtime / "shipofharkinian.json").read_text()))
        for name, info in manifest.items():
            self.assertEqual(digest(source / name), info["sha256"])
            self.assertTrue((runtime / name).samefile(source / name))
        self.assertEqual((source / "Save/file1.sav").read_text(), "personal save fixture")

    def test_runtime_directory_is_never_reused_accidentally(self):
        runtime = self.root / "runtime"
        runtime.mkdir()
        with self.assertRaises(FileExistsError):
            prepare_runtime(self.root / "missing.exe", runtime, self.root)

    def test_pack_fixture_is_own_png_and_explicit_ids(self):
        path = make_pack(self.root)
        with zipfile.ZipFile(path) as archive:
            self.assertEqual(len(json.loads(archive.read("maps.json"))), 2)
            self.assertEqual(len(json.loads(archive.read("areas/validation.json"))["checks"]), 3)
            self.assertTrue(archive.read("images/map.png").startswith(b"\x89PNG\r\n\x1a\n"))
        make_pack(self.root, invalid=True)
        with zipfile.ZipFile(path) as archive:
            self.assertIsInstance(json.loads(archive.read("maps.json")), dict)


if __name__ == "__main__":
    unittest.main()
