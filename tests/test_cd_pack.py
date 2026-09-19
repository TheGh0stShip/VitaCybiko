import hashlib
import json
from pathlib import Path
import sys
import unittest
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tools"))
from extract_cd_pack import CD_MEMBER, decode_pack


class CDTests(unittest.TestCase):
    def setUp(self):
        archive = PROJECT / "downloads/cybiko_archive/cybiko.zip"
        if not archive.exists():
            self.skipTest("local original CD archive not present")
        with zipfile.ZipFile(archive) as source:
            self.cap = source.read(CD_MEMBER)
        self.assertEqual(hashlib.sha256(self.cap).hexdigest(),
                         "e8a470a323e26c50d312ac437a3920ab3cde85ce411ee488ea8adca6f8d2661d")

    def test_original_cd_reproduces_all_files(self):
        files = decode_pack(self.cap)
        manifest = json.loads((PROJECT / "dist/xtreme-cd-pack/manifest.json").read_text())
        self.assertEqual(len(files), 15)
        self.assertEqual(sum(name.endswith(".app") for name in files), 13)
        self.assertEqual(set(name for name in files if name.endswith(".dl")),
                         {"email_dl.dl", "sound.dl"})
        for entry in manifest["files"]:
            data = files[Path(entry["name"]).name]
            self.assertEqual(len(data), entry["bytes"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"])
            self.assertEqual(data[:2], b"Cy")

    def test_corruption_and_truncation_rejected(self):
        for data in (self.cap[:100], self.cap[:-20], self.cap + b"trailing"):
            with self.assertRaises((ValueError, OSError, EOFError)):
                decode_pack(data)
        broken = bytearray(self.cap)
        broken[-20] ^= 0x80
        with self.assertRaises((ValueError, OSError, EOFError)):
            decode_pack(broken)

    def test_bad_header_lengths_rejected(self):
        for offset in (7, 11, 23):
            broken = bytearray(self.cap)
            broken[offset:offset + 4] = b"\xff" * 4
            with self.assertRaises((ValueError, OSError, EOFError)):
                decode_pack(broken)


if __name__ == "__main__":
    unittest.main()
