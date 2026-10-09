"""Standard-library tests; PTY is an adapter simulator, not a hardware proof."""
import binascii
import importlib.util
import os
from pathlib import Path
import select
import subprocess
import sys
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sensor_console", ROOT / "scripts/sensor_console.py")
console = importlib.util.module_from_spec(spec)
spec.loader.exec_module(console)


def response(request, data=bytes(8), status=0):
    reply = bytearray(request)
    reply[3], reply[8], reply[10:18] = 2, status, data
    reply[26:28] = binascii.crc_hqx(reply[2:26], 0xffff).to_bytes(2, "big")
    return bytes(reply)


class ConsoleTests(unittest.TestCase):
    def test_commands_and_golden_request(self):
        self.assertEqual(console.request_frame(0x25, 0, 1, 1), bytes.fromhex(
            "A5 5A 02 01 00 01 0D 25 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 89 2C"))
        for name, (command, count) in console.COMMANDS.items():
            self.assertEqual(console.parse_command(f" {name} {count-1} "), (command, count-1))
            for arg in (str(count), "-1", "1.0", "x", "00000000", "１"):
                with self.assertRaises(ValueError):
                    console.parse_command(f"{name} {arg}")
        for line in ("", "pwm 0", "temp", "temp 0 junk"):
            with self.assertRaises(ValueError):
                console.parse_command(line)

    def test_interpretation_and_validation(self):
        examples = ((0x20, (-1234).to_bytes(4, "big", signed=True), "-12.34 °C"),
                    (0x23, (100653).to_bytes(4, "big"), "100653 Па"),
                    (0x21, (100).to_bytes(4, "big"), "VOC Index: 100"),
                    (0x24, (5550).to_bytes(4, "big"), "55.50 % RH"),
                    (0x25, bytes.fromhex("02 00 09 C6"), "напряжение=2.502 В"))
        for command, value, text in examples:
            request = console.request_frame(command, 0, 1, 1)
            self.assertIn(text, console.describe(response(request, value + bytes.fromhex("03 00 7B 00"))))
            self.assertIn("age_ms=123", console.describe(response(request, value + bytes.fromhex("03 00 7B 00"))))
        request = console.request_frame(0x25, 0, 1, 1)
        self.assertIn("DEVICE_TIMEOUT", console.describe(response(request, status=7)))
        for data in (bytes.fromhex("04 00 09 C6 03 00 00 00"),
                     bytes.fromhex("02 00 09 C6 00 00 00 00"),
                     bytes.fromhex("02 00 09 C6 03 FF FF 00"),
                     bytes.fromhex("02 00 09 C6 03 00 00 01")):
            with self.assertRaises(ValueError):
                console.describe(response(request, data))
        with self.assertRaises(ValueError):
            console.describe(response(request, bytes.fromhex("01 00 00 00 00 00 00 00"), 7))

    def test_packet_noise_binary_delimiters_crc_and_timeout(self):
        request = console.request_frame(0x25, 0, 1, 0xA55A0D0A12345678)
        reply = response(request, bytes.fromhex("02 00 09 C6 03 00 7B 00"))
        bad = bytearray(reply); bad[-1] ^= 1
        wire = b"noise" + b"".join(console.envelope(b, 12) for b in request)
        wire += b"".join(console.envelope(b, 99) for b in bad)
        wire += b"".join(console.envelope(b, 99) for b in reply)
        parser = console.ReplyParser()
        received = []
        for b in wire:
            received.extend(parser.feed(bytes((b,)), 10))
        self.assertEqual(received, [reply])
        parser = console.ReplyParser()
        self.assertEqual(list(parser.feed(console.envelope(0xa5, 99), 10)), [])
        self.assertEqual(list(parser.feed(b"".join(console.envelope(b, 99) for b in reply), 10.3)), [reply])
        self.assertLessEqual(len(parser.bus), 4)
        self.assertLessEqual(len(parser.frame), 27)

    @unittest.skipUnless(os.name == "posix", "PTY requires POSIX")
    def test_real_console_over_pty_with_stale_and_echo(self):
        master, slave = os.openpty()
        errors = []

        def adapter():
            try:
                wire = bytearray(); deadline = time.monotonic() + 8
                while len(wire) < 140 and time.monotonic() < deadline:
                    if select.select([master], [], [], .05)[0]:
                        wire.extend(os.read(master, 1024))
                self.assertEqual(len(wire), 140)
                request = bytes(wire[3::5])
                self.assertEqual(wire, b"".join(console.envelope(b, 12) for b in request))
                self.assertEqual(request[2:4], b"\x02\x01")
                self.assertEqual(request[6:10], bytes((13, 0x23, 0, 0)))
                self.assertEqual(int.from_bytes(request[26:], "big"), binascii.crc_hqx(request[2:26], 0xffff))
                reply = response(request, bytes.fromhex("00 01 89 2D 03 00 00 00"))
                stale_request = bytearray(request); stale_request[18] ^= 1
                stale = response(stale_request, bytes.fromhex("00 01 89 2D 03 00 00 00"))
                invalid = response(request, bytes.fromhex("00 01 89 2D 00 00 00 00"))
                os.write(master, bytes(wire) + b"".join(console.envelope(b, 99) for frame in (stale, invalid, reply) for b in frame))
            except BaseException as error:
                errors.append(error)

        thread = threading.Thread(target=adapter, daemon=True)
        thread.start()
        try:
            result = subprocess.run([sys.executable, str(ROOT / "scripts/sensor_console.py"),
                                     os.ttyname(slave), "--hex"], input="pressure 0\nexit\n",
                                    text=True, capture_output=True, timeout=10)
            thread.join(1)
            self.assertFalse(errors, errors)
            self.assertFalse(thread.is_alive())
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Давление: 100653 Па", result.stdout)
            self.assertIn("RX HEX:", result.stdout)
            self.assertNotIn("HOST_TIMEOUT", result.stdout)
        finally:
            os.close(master); os.close(slave)


if __name__ == "__main__":
    unittest.main()
