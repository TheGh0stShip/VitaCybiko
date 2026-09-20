"""Host checks for the opt-in ARM trace decoder; no firmware is required."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "motion_trace", Path(__file__).resolve().parents[1] / "tools/inspect_motion_trace.py")
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class MotionTraceTests(unittest.TestCase):
    def test_empty_or_invalid_metadata_produces_no_evidence(self):
        for count, phase, flags, width in ((0, 0, 0, 480), (1, 257, 0, 480),
                                            (1, 0, 8, 480), (1, 0, 0, 160)):
            with self.subTest(count=count, phase=phase, flags=flags, width=width):
                with tempfile.TemporaryDirectory() as root:
                    source, output = Path(root) / "trace.bin", Path(root) / "decoded"
                    record = struct.pack("<IIIiiIII", 956, 40, phase, 0, 0, flags, width, 300) + bytes(176000)
                    source.write_bytes(b"VCMT0001" + struct.pack("<I", count) + record * count)
                    with self.assertRaises(ValueError):
                        trace.inspect(source, output)
                    self.assertFalse(output.exists())

    def test_native_rejection_and_dimensions(self):
        with tempfile.TemporaryDirectory() as root:
            source, output = Path(root) / "trace.bin", Path(root) / "decoded"
            before = bytes(range(160)) * 100
            after = bytes([17]) * 16000
            expanded = b"".join(bytes(v for v in before[y*160:(y+1)*160] for _ in range(3)) * 3
                                for y in range(100))
            source.write_bytes(b"VCMT0001" + struct.pack("<I", 1) +
                               struct.pack("<IIIiiIII", 956, 40, 80, -5, 0, 4, 480, 300) +
                               before + after + expanded)
            self.assertEqual(trace.inspect(source, output), 0)
            self.assertEqual((output / "000-output.pgm").read_bytes(), b"P5\n480 300\n255\n" + expanded)
            self.assertIn("True", (output / "records.csv").read_text())

    def test_truncated_trace_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as root:
            source, output = Path(root) / "trace.bin", Path(root) / "decoded"
            source.write_bytes(b"VCMT0001" + struct.pack("<I", 1))
            with self.assertRaises(ValueError):
                trace.inspect(source, output)
            self.assertFalse(output.exists())

    def test_bad_rejected_flow_output_fails(self):
        with tempfile.TemporaryDirectory() as root:
            source, output = Path(root) / "trace.bin", Path(root) / "decoded"
            source.write_bytes(b"VCMT0001" + struct.pack("<I", 1) +
                               struct.pack("<IIIiiIII", 956, 40, 80, 0, 0, 4, 480, 300) +
                               bytes(32000) + bytes([255]) * 144000)
            self.assertEqual(trace.inspect(source, output), 1)


if __name__ == "__main__":
    unittest.main()
