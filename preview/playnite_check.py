import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("steam_sync", Path(__file__).parents[1] / "scripts/steam_sync.py")
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)


class PlayniteTests(unittest.TestCase):
    def test_detection_and_managed_entry(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            executable = root / "Playnite" / "Playnite.FullscreenApp.exe"
            executable.parent.mkdir()
            executable.touch()
            with patch.dict(sync.os.environ, {"LOCALAPPDATA": folder}):
                self.assertEqual(sync.find_playnite(), executable.resolve())
            self.assertEqual(sync.find_playnite(executable.parent), executable.resolve())
            original = {"apps": [{"name": "Desktop", "cmd": "", "extra": 17}]}
            merged = sync.merge_playnite(original, executable)
            self.assertEqual(len(merged["apps"]), 2)
            self.assertEqual(merged["apps"][0], original["apps"][0])
            self.assertEqual(merged["apps"][1]["cmd"], f'"{executable}"')
            self.assertEqual(sync.merge_playnite(merged, executable), merged)
            self.assertEqual(sync.merge_playnite(merged, None), original)
            executable.unlink()
            with self.assertRaises(ValueError):
                sync.find_playnite(executable)

    def test_manual_entry_is_preserved_and_not_duplicated(self):
        original = {"apps": [{"name": "My library", "cmd": '"D:\\Apps\\Playnite.FullscreenApp.exe"', "extra": 42}]}
        self.assertEqual(sync.merge_playnite(original, Path("Playnite.FullscreenApp.exe")), original)
        self.assertEqual(sync.merge_playnite(original, None), original)
        with self.assertRaises(ValueError):
            sync.merge_playnite({"apps": []}, Path('bad"path/Playnite.FullscreenApp.exe'))


if __name__ == "__main__":
    unittest.main()
