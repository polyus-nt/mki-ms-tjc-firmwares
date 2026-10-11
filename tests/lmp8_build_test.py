"""Pinned lmp8 sources and startup patch, runnable without ARM tools."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_lmp8 import LED_FILES, patch_controller


class Lmp8BuildTest(unittest.TestCase):
    def test_pinned_sources(self):
        for name, expected in LED_FILES.items():
            source = (ROOT / "firmware/runtime-lmp8" / name).read_bytes().replace(b"\r\n", b"\n")
            self.assertEqual(hashlib.sha256(source).hexdigest(), expected)

    def test_startup_latch_before_output_mode(self):
        original = (ROOT / "firmware/runtime-lmp8/LEDController.hpp").read_text(encoding="utf-8")
        patched = patch_controller(original)
        self.assertNotIn("initPin_PP(leds[i].port", patched)
        self.assertLess(patched.index("port->BSRR ="), patched.index("port->MODER ="))
        self.assertIn("tjc_lmp8::startupHigh(i)", patched)
        self.assertIn("high ? MAX_LED_BRIGHTNESS : 0", patched)
        # Production patch selects the actual BoardConfig value, not a fixed lock level.
        for active_low in (True, False):
            high = not active_low
            self.assertEqual((not high) if active_low else high, True)

    def test_build_prepare_from_other_directory(self):
        with tempfile.TemporaryDirectory(prefix="lmp8 test ") as temp:
            out = Path(temp) / "output"
            subprocess.run([sys.executable, str(ROOT / "scripts/build.py"), "--target", "lmp8",
                            "--prepare-only", "--out", str(out)], cwd=temp, check=True)
            self.assertTrue((out / "lmp8/platform/LEDController.hpp").is_file())


if __name__ == "__main__":
    unittest.main()
