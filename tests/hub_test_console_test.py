"""SimpleBus byte test; simulated serial is not a hardware check."""
from pathlib import Path
import sys
import unittest
import io
import os
import select
import threading
from contextlib import redirect_stdout
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import hub_test_console as console


class HubTestConsoleTests(unittest.TestCase):
    def test_target_commands(self):
        for line, expected in (("3", (12, 3)), ("hub 0xAA", (12, 170)),
                               ("12 255", (12, 255)), ("buttons 0", (15, 0)),
                               ("15 0xFF", (15, 255))):
            self.assertEqual(console.parse_command(line), expected)
        for line in ("buttons", "13 3", "hub 256", "buttons 1 extra", ""):
            with self.assertRaises(ValueError):
                console.parse_command(line)

    def test_independent_targets_on_same_port(self):
        for address, expected in ((12, 0xAA), (15, 0xBB)):
            sent, trace = [], []
            chunks = [console.envelope(3, address), console.envelope(expected, 99)]
            port = SimpleNamespace(send=sent.append, read=lambda timeout: chunks.pop(0))
            self.assertEqual(console.exchange_byte(port, 3, trace.append, address), expected)
            self.assertEqual(sent, [console.envelope(3, address)])
        with self.assertRaises(ValueError):
            console.exchange_byte(port, 3, trace.append, 13)

    def test_main_uses_9600_and_selects_target(self):
        port = SimpleNamespace(quiet=lambda trace: None, close=lambda: None)
        output = io.StringIO()
        with patch.object(sys, "argv", ["hub_test_console.py", "COM15"]), \
                patch.object(console.os, "name", "nt"), \
                patch.object(console, "WindowsPort", return_value=port) as factory, \
                patch.object(console, "exchange_byte", side_effect=[0xAA, 0xBB]) as exchange, \
                patch("builtins.input", side_effect=["hub 3", "buttons 3", "exit"]), \
                redirect_stdout(output):
            console.main()
        factory.assert_called_once_with("COM15", baudrate=9600)
        self.assertEqual([call.kwargs["address"] for call in exchange.call_args_list], [12, 15])
        self.assertIn("HUB_OK", output.getvalue())
        self.assertIn("BUTTONS_OK", output.getvalue())

    def test_byte_values(self):
        for text, value in (("0", 0), ("255", 255), ("0xAA", 170), ("0X0D", 13)):
            self.assertEqual(console.parse_byte(text), value)
        for text in ("-1", "256", "0x100", "temp 0", "1.5", ""):
            with self.assertRaises(ValueError):
                console.parse_byte(text)

    @unittest.skipUnless(os.name == "posix", "PTY requires POSIX")
    def test_both_endpoints_over_same_9600_port(self):
        import termios
        import tty
        master, slave = os.openpty()
        tty.setraw(slave)
        port = console.Port(os.ttyname(slave), baudrate=9600)
        self.assertEqual(termios.tcgetattr(port.fd)[4:6], [termios.B9600, termios.B9600])
        errors = []
        def adapter():
            try:
                for address, answer in ((12, 0xAA), (15, 0xBB)):
                    wire = bytearray()
                    deadline = console.time.monotonic() + 3
                    while len(wire) < 5 and console.time.monotonic() < deadline:
                        if select.select([master], [], [], .01)[0]:
                            wire.extend(os.read(master, 5-len(wire)))
                    self.assertEqual(bytes(wire), console.envelope(3, address))
                    # Эхо не является ответом; только адрес 99 возвращается клиенту.
                    os.write(master, wire + console.envelope(answer, 99))
            except BaseException as error:
                errors.append(error)
        worker = threading.Thread(target=adapter, daemon=True)
        worker.start()
        try:
            for address, answer in ((12, 0xAA), (15, 0xBB)):
                self.assertEqual(console.exchange_byte(port, 3, lambda _: None, address), answer)
            worker.join(4)
            self.assertFalse(worker.is_alive())
            self.assertFalse(errors, errors)
        finally:
            port.close()
            os.close(master)
            os.close(slave)

    def test_echo_noise_and_fragmented_reply(self):
        for value in (0, 10, 13, 255):
            sent, trace = [], []
            chunks = [b"noise" + console.envelope(value, 12), b"\r6", b"3\xaa\n"]
            port = SimpleNamespace(send=sent.append, read=lambda timeout: chunks.pop(0))
            self.assertEqual(console.exchange_byte(port, value, trace.append), 0xAA)
            self.assertEqual(sent, [console.envelope(value, 12)])
            self.assertTrue(any("RX RAW:" in line for line in trace))

    def test_timeout_without_api_filter(self):
        now = [0]
        def read(timeout):
            now[0] += 1
            return b"old" if now[0] == 1 else b""
        port = SimpleNamespace(send=lambda packet: None, read=read)
        with patch.object(console.time, "monotonic", side_effect=lambda: now[0]):
            with self.assertRaisesRegex(TimeoutError, "RX байтов: 3"):
                console.exchange_byte(port, 42, lambda message: None)


if __name__ == "__main__":
    unittest.main()
