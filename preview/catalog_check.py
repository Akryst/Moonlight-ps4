import importlib.util
import json
from pathlib import Path
import tempfile
import struct
import unittest
import zlib

spec = importlib.util.spec_from_file_location("exporter", Path(__file__).parents[1] / "scripts/export_preview_catalog.py")
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


class ExportCheck(unittest.TestCase):
    def test_embedded_landscape_and_invalid_chunk(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cover = root / "cover.png"
            hero = b"\x89PNG\r\n\x1a\nexample"
            chunk = b"mlBg" + hero
            data = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", len(hero)) + chunk + struct.pack(">I", zlib.crc32(chunk))
            cover.write_bytes(data)
            result = Path(exporter.landscape_cover(str(cover), root / "cache"))
            self.assertEqual(result.read_bytes(), hero)
            self.assertEqual(cover.read_bytes(), data)
            cover.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
            self.assertEqual(exporter.landscape_cover(str(cover), root / "cache"), str(cover))
            cover.write_bytes(data[:15])
            self.assertEqual(exporter.landscape_cover(str(cover), root / "cache"), str(cover))

    def test_readonly_export_and_missing_cover(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "config/apps.json"
            source.parent.mkdir()
            (root / "assets").mkdir()
            (root / "assets/desktop.png").touch()
            source.write_text(json.dumps({"apps": [
                {"name": 'Titulo & "Beta"', "image-path": "desktop.png", "cmd": "never run"},
                {"name": "No artwork", "image-path": "missing.png"}]}), encoding="utf-8")
            original = source.read_bytes()
            output = root / "preview.cfg"
            self.assertEqual(exporter.export_catalog(source, output), 2)
            result = output.read_text(encoding="utf-8")
            self.assertIn('Titulo & \\"Beta\\"', result)
            self.assertIn(str(root / "assets/desktop.png"), result)
            self.assertIn('cover = ""', result)
            self.assertNotIn("never run", result)
            self.assertEqual(original, source.read_bytes())

    def test_bad_catalog_preserves_existing_export(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "apps.json"
            output = Path(directory) / "preview.cfg"
            output.write_text("previous good export")
            for document in [{}, {"apps": "bad"}, {"apps": [{"name": "bad\nname"}]},
                             {"apps": [{"name": "a" * 513}]}, {"apps": [None]}]:
                source.write_text(json.dumps(document))
                with self.assertRaises(ValueError): exporter.export_catalog(source, output)
                self.assertEqual(output.read_text(), "previous good export")


if __name__ == "__main__": unittest.main()
