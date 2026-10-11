"""Pinned lmp-a3 sources, safe LED startup and build aliases; no ARM tools needed."""
import hashlib
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_lmp import LED_FILES, patch_led, patch_connector


class LmpBuildTest(unittest.TestCase):
    def test_pinned_sources(self):
        for name, expected in LED_FILES.items():
            text = (ROOT / "firmware/runtime-lmp" / name).read_text(encoding="utf-8").rstrip()+"\n"
            self.assertEqual(hashlib.sha256(text.encode()).hexdigest(), expected)

    def test_safe_startup(self):
        patched=patch_led((ROOT / "firmware/runtime-lmp/LED.h").read_text(encoding="utf-8"))
        constructor=patched[patched.index("LED(uint8_t"):patched.index("void on(")]
        self.assertLess(constructor.index("GPIOA->BSRR ="),constructor.index("GPIOA->MODER ="))
        self.assertNotIn("off();",constructor)
        self.assertIn("tjc_lmp::startupHigh(ledPin - 1)",constructor)
        connector=patch_connector((ROOT / "firmware/runtime-lmp/Connector.hpp").read_text(encoding="utf-8"))
        self.assertNotIn("auto&& init",connector)
        self.assertNotIn("GPIOA->MODER",connector)

    def test_real_led_startup_on_gpio_model(self):
        compiler=os.environ.get("CXX") or shutil.which("g++")
        if not compiler:
            self.skipTest("set CXX or install g++ for native GPIO check")
        with tempfile.TemporaryDirectory(prefix="lmp-led-") as temp:
            folder=Path(temp)
            led=folder / "LED.h"
            led.write_text(patch_led((ROOT / "firmware/runtime-lmp/LED.h").read_text(encoding="utf-8")),encoding="utf-8")
            (folder / "PWM.hpp").write_text("#pragma once\n",encoding="utf-8")
            binary=folder / "led.exe"
            subprocess.run([compiler,"-std=c++14","-Wall","-Wextra","-Werror",
                            "-I",str(ROOT / "firmware/lmp"),
                            '-DLMP_LED_HEADER="'+led.as_posix()+'"',
                            str(ROOT / "tests/lmp_led_test.cpp"),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_build_prepare_and_legacy_alias(self):
        for target in ("lmp","lmp8"):
            with tempfile.TemporaryDirectory(prefix="lmp test ") as temp:
                out=Path(temp)/"output"
                subprocess.run([sys.executable,str(ROOT / "scripts/build.py"),
                                "--target",target,"--prepare-only","--out",str(out)],
                               cwd=temp,check=True)
                platform=out / "lmp/platform"
                self.assertTrue((platform / "LED.h").is_file())
                self.assertFalse((platform / "LEDController.hpp").exists())
                self.assertFalse((out / "lmp8").exists())
                self.assertIn("takeOverflow",(platform / "UART.hpp").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
