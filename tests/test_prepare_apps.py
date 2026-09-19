import importlib.util
from pathlib import Path
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_apps", PROJECT / "tools/prepare_apps.py")
apps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(apps)


def app_bytes():
    # One resource named x with a one-byte uncompressed marker payload.
    return bytes.fromhex("4379 0001 000c 0010 00000012 00000001 7800 00")


class PackTests(unittest.TestCase):
    def test_stage_preserves_bytes_and_rejects_existing_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            (source / "one.app").write_bytes(app_bytes())
            output = root / "output"
            manifest = apps.stage(source, output, ["one.app"])
            self.assertEqual(manifest["cfs_blocks_used"], 1)
            self.assertEqual((output / "apps/one.app").read_bytes(), app_bytes())
            with self.assertRaises(FileExistsError):
                apps.stage(source, output, ["one.app"])

    def test_failure_leaves_no_partial_pack(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "good.app").write_bytes(app_bytes())
            (root / "bad.app").write_bytes(b"Cy\xff\xff\xff\xff")
            with self.assertRaises(ValueError):
                apps.stage(root, root / "output", ["good.app", "bad.app"])
            self.assertFalse((root / "output").exists())
            with self.assertRaises(ValueError):
                apps.stage(root, root / "output", ["../good.app"])
            # Valid archives that together exceed available CFS blocks.
            (root / "second.app").write_bytes(app_bytes())
            with self.assertRaises(ValueError):
                apps.stage(root, root / "output", ["good.app", "second.app"], 1999)
            self.assertFalse((root / "output").exists())


if __name__ == "__main__":
    unittest.main()
