"""SimpleBus byte test; simulated serial is not a hardware check."""
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import hub_test_console as console


class HubTestConsoleTests(unittest.TestCase):
    def test_full_api_size_sent_in_one_burst(self):
        values=console.request_frame(0x20,3,1,1)
        sent=[]
        chunks=[b"noise"+b"".join(console.envelope(v,12) for v in values),
                console.envelope(0xAA,99)*10, console.envelope(0xAA,99)*18]
        port=SimpleNamespace(send=sent.append,read=lambda timeout:chunks.pop(0))
        self.assertEqual(console.exchange_packets(port,values,5,lambda _:None),[0xAA]*28)
        self.assertEqual(len(sent),1)
        self.assertEqual(len(sent[0]),140)
        self.assertEqual(sent[0][3::5],values)

    def test_byte_values(self):
        for text, value in (("0", 0), ("255", 255), ("0xAA", 170), ("0X0D", 13)):
            self.assertEqual(console.parse_byte(text), value)
        for text in ("-1", "256", "0x100", "temp 0", "1.5", ""):
            with self.assertRaises(ValueError):
                console.parse_byte(text)

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
