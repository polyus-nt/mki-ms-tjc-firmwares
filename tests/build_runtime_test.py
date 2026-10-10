"""Local runtime integrity, CRLF compatibility and offline build entry point."""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_gateways", ROOT / "scripts/build_gateways.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
RUNTIME = ROOT / "firmware/runtime"


class RuntimeTests(unittest.TestCase):
    def test_pinned_local_files_and_patches(self):
        for name, expected in builder.FILES.items():
            content = (RUNTIME / name).read_bytes().replace(b"\r\n", b"\n")
            self.assertEqual(hashlib.sha256(content).hexdigest(), expected, name)
        with tempfile.TemporaryDirectory() as tmp:
            builder.prepare_runtime(RUNTIME, Path(tmp))
            uart = (Path(tmp) / "UART.hpp").read_text(encoding="utf-8")
            self.assertIn("SIZESimpleBus = 64", uart)
            self.assertIn("bool takeOverflow()", uart)
            self.assertIn("__set_PRIMASK(saved)", uart)
            self.assertNotIn("*reinterpret_cast<uint16_t* const>(&interruptBuffer[0])", uart)
            self.assertIn("Preserve the latched failure", (Path(tmp) / "ArduinoAdapter.hpp").read_text(encoding="utf-8"))

    def test_crlf_accepted_but_actual_modification_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, dest = Path(tmp) / "source", Path(tmp) / "out"
            shutil.copytree(RUNTIME, source)
            for name in builder.FILES:
                path = source / name
                path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
            builder.prepare_runtime(source, dest)
            self.assertNotIn(b"\r\n", (dest / "UART.hpp").read_bytes())
            uart = source / "UART.hpp"
            uart.write_bytes(uart.read_bytes().replace(b"SIZESimpleBus = 5", b"SIZESimpleBus = 6"))
            with self.assertRaisesRegex(ValueError, "UART.hpp"):
                builder.prepare_runtime(source, dest)

    def test_all_targets_prepare_without_external_compiler(self):
        with tempfile.TemporaryDirectory(prefix="runtime build ") as tmp:
            result = subprocess.run([sys.executable, str(ROOT / "scripts/build.py"),
                                     "--prepare-only", "--out", tmp],
                                    cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((Path(tmp) / "hub/platform/UART.hpp").is_file())
            self.assertFalse((Path(tmp) / "hub/platform/arduino_link.c").exists())
            self.assertNotIn("hub_test::uart_rx_count", (Path(tmp) / "hub/platform/UART.hpp").read_text(encoding="utf-8"))
            self.assertTrue((Path(tmp) / "gateways/platform/arduino_link.c").is_file())
            self.assertTrue((Path(tmp) / "buttons/platform/Button.hpp").is_file())
            self.assertFalse((Path(tmp) / "buttons/platform/arduino_link.c").exists())
            self.assertNotIn("hub_test::uart_rx_count", (Path(tmp) / "gateways/platform/UART.hpp").read_text(encoding="utf-8"))
            self.assertFalse((Path(tmp) / "hub-test").exists())

    def test_buttons_target_and_pinned_button_class(self):
        expected = "3df782c57ce6687d804208585ec54145407f285e58b678fc7a39b4f40a82913a"
        content = (RUNTIME / "Button.hpp").read_bytes().replace(b"\r\n", b"\n")
        self.assertEqual(hashlib.sha256(content).hexdigest(), expected)
        with tempfile.TemporaryDirectory(prefix="buttons build ") as tmp:
            result = subprocess.run([sys.executable, str(ROOT / "scripts/build.py"),
                                     "--target", "buttons", "--prepare-only", "--out", tmp],
                                    cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            prepared = (Path(tmp) / "buttons/platform/Button.hpp").read_bytes()
            self.assertEqual(prepared, content)
            self.assertFalse((Path(tmp) / "hub").exists())

    def test_hub_test_is_separate_opt_in_target(self):
        with tempfile.TemporaryDirectory(prefix="hub test build ") as tmp:
            result = subprocess.run([sys.executable, str(ROOT / "scripts/build.py"),
                                     "--target", "hub-test", "--prepare-only", "--out", tmp],
                                    cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((Path(tmp) / "hub-test/platform/UART.hpp").is_file())
            self.assertTrue((Path(tmp) / "hub-test/platform/LED.h").is_file())
            self.assertTrue((Path(tmp) / "hub-test/platform/PWMInit.hpp").is_file())
            uart = (Path(tmp) / "hub-test/platform/UART.hpp").read_text(encoding="utf-8")
            self.assertEqual(uart.count("++hub_test::uart_rx_count"), 1)
            handler = uart[uart.index("void UARTInsertion__(USART, _IRQHandlerSimpleBus)"):]
            self.assertLess(handler.index("USART_ISR_RXNE_RXFNE"), handler.index("-> RDR"))
            self.assertLess(handler.index("++hub_test::uart_rx_count"), handler.index("// save to buffer"))
            self.assertFalse((Path(tmp) / "hub").exists())
            self.assertFalse((Path(tmp) / "hub-test/platform/ArduinoAdapter.hpp").exists())

    def test_hub_test_with_cp1251_default_encoding(self):
        spec = importlib.util.spec_from_file_location("build_hub", ROOT / "scripts/build_hub.py")
        hub_builder = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"build_gateways": builder}):
            spec.loader.exec_module(hub_builder)
        original_read = Path.read_text

        def cp1251_read(path, encoding=None, **kwargs):
            return original_read(path, encoding=encoding or "cp1251", **kwargs)

        with tempfile.TemporaryDirectory() as tmp:
            with patch.object(Path, "read_text", cp1251_read), patch.object(sys, "argv",
                    ["build_hub.py", "--test-echo", "--prepare-only", "--out", tmp]):
                hub_builder.main()
            uart = (Path(tmp) / "platform/UART.hpp").read_text(encoding="utf-8")
            self.assertIn("++hub_test::uart_rx_count", uart)
            self.assertIn("Здесь реализация модулей UART1", uart)

    def test_windows_exe_tool_names_and_missing_tools(self):
        with tempfile.TemporaryDirectory(prefix="ARM GCC ") as tmp:
            directory = Path(tmp)
            for tool in ("arm-none-eabi-g++", "arm-none-eabi-objcopy", "arm-none-eabi-size"):
                (directory / (tool + ".exe")).write_bytes(b"")
            self.assertTrue(all(path.endswith(".exe") for path in builder.find_tools(directory).values()))
        with tempfile.TemporaryDirectory() as tmp, self.assertRaisesRegex(SystemExit,"Missing ARM target tool"):
            builder.find_tools(Path(tmp))


if __name__ == "__main__":
    unittest.main()
