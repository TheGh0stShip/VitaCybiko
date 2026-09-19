import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_firmware as firmware


class FirmwareTests(unittest.TestCase):
    def test_identify_stage_and_preserve(self):
        with tempfile.TemporaryDirectory() as root:
            source, target = Path(root) / "source", Path(root) / "destination"
            source.mkdir()
            data = b"synthetic firmware test fixture"
            (source / "arbitrary-name.dat").write_bytes(data)
            images = {(len(data), hashlib.sha1(data).hexdigest()): "classic-v2/roms/boot.bin"}
            with patch.object(firmware, "IMAGES", images):
                found = firmware.plan(source, target)
                self.assertEqual(len(found), 1)
                self.assertFalse(target.exists())
                firmware.stage(found)
                firmware.stage(found)
                output = target / "classic-v2/roms/boot.bin"
                self.assertEqual(output.read_bytes(), data)
                output.write_bytes(b"preserve me")
                with self.assertRaises(ValueError):
                    firmware.plan(source, target)
                self.assertEqual(output.read_bytes(), b"preserve me")

    def test_unknown_data_not_copied(self):
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / "source"
            source.mkdir()
            (source / "boot.bin").write_bytes(bytes(32768))
            self.assertEqual(firmware.plan(source, Path(root) / "target"), {})
