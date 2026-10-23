"""Standard-library tests; PTY is an adapter simulator, not a hardware proof."""
import binascii
import importlib.util
import io
import os
from pathlib import Path
import select
import subprocess
import sys
import threading
import time
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from contextlib import redirect_stdout

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("sensor_console", ROOT / "scripts/sensor_console.py")
console = importlib.util.module_from_spec(spec)
spec.loader.exec_module(console)


def response(request, data=bytes(8), status=0):
    reply = bytearray(request)
    reply[3], reply[8], reply[10:18] = 2, status, data
    reply[26:28] = binascii.crc_hqx(reply[2:26], 0xffff).to_bytes(2, "big")
    return bytes(reply)



def permits(request):
    ready=bytearray(request)
    ready[3],ready[6],ready[7],ready[8],ready[9]=2,12,0xf0,0,0
    ready[10:18]=b"\x00\x64"+bytes(6)
    ready[18:26]=bytes(8)  # Фикстура: effective session остаётся session запроса.
    ready[26:28]=binascii.crc_hqx(ready[2:26],0xffff).to_bytes(2,"big")
    grant=bytearray(ready);grant[7]=0xf2;grant[18:26]=request[18:26]
    grant[26:28]=binascii.crc_hqx(grant[2:26],0xffff).to_bytes(2,"big")
    return [b"".join(console.envelope(b,99) for b in f) for f in (ready,grant)]

class ConsoleTests(unittest.TestCase):
    def test_ready_burst_claims_only_newest_not_each_old_ticket(self):
        request = console.request_frame(0x20,3,1,99)
        ready_wire, _ = permits(request)
        base = bytearray(ready_wire[3::5])
        def frame(tx):
            f=bytearray(base); f[4:6]=tx.to_bytes(2,"big")
            f[26:28]=binascii.crc_hqx(f[2:26],0xffff).to_bytes(2,"big")
            return b"".join(console.envelope(b,99) for b in f)
        initial=b"".join(frame(i) for i in list(range(100,121))+list(range(90,100)))
        chunks=[initial]; sent=[]
        def send(wire):
            f=wire[3::5]; sent.append(f)
            if f[7]==0xf1:
                actual=console.request_frame(0x20,3,int.from_bytes(f[4:6],"big"),99)
                chunks.append(permits(actual)[1])
            else:
                chunks.append(b"".join(console.envelope(b,99) for b in response(f,status=1)))
        port=SimpleNamespace(quiet=lambda trace=None:None,send=send,read=lambda timeout:chunks.pop(0))
        self.assertEqual(console.exchange(port,request)[8],1)
        self.assertEqual([int.from_bytes(f[4:6],"big") for f in sent],[120,120])
        self.assertEqual([f[7] for f in sent],[0xf1,0x20])

    @unittest.skipUnless(os.name == "posix", "PTY requires POSIX")
    def test_background_rx_sensor_pwm_sensor_after_idle_same_open_port(self):
        master,slave=os.openpty()
        import tty
        tty.setraw(slave)
        port=console.Port(os.ttyname(slave))
        port.receiver=console.ContinuousReader(port.read)
        stop=threading.Event(); errors=[]; claims=[]; commands=[]
        def hardware():
            try:
                pending=bytearray(); ticket=0; last=0; ready=None
                while not stop.is_set():
                    if time.monotonic()-last>.05:
                        ticket+=1
                        r=console.request_frame(0x20,3,ticket,0)
                        ready=permits(r)[0]
                        os.write(master,ready); last=time.monotonic()
                    if not select.select([master],[],[],.005)[0]:
                        continue
                    pending.extend(os.read(master,1024))
                    while len(pending)>=140:
                        f=bytes(pending[:140][3::5]); del pending[:140]
                        if f[7]==0xf1:
                            claims.append(f)
                            # Принимать только последнее окно, как настоящий хаб.
                            if f[4:6]!=ticket.to_bytes(2,"big"):
                                continue
                            actual=console.request_frame(0x20,3,ticket,int.from_bytes(f[10:18],"big"))
                            os.write(master,permits(actual)[1]);last=time.monotonic()
                        else:
                            commands.append(f)
                            if f[7]==0x30: reply=response(f,b"\x96"+bytes(7))
                            else: reply=response(f,status=1)
                            os.write(master,b"".join(console.envelope(b,99) for b in reply))
            except BaseException as error:
                errors.append(error)
        worker=threading.Thread(target=hardware,daemon=True);worker.start()
        try:
            for command,idx in ((0x20,3),(0x30,0),(0x20,3),(0x30,1),(0x20,3)):
                count=len(claims)
                time.sleep(.2) # Несколько окон без читающего main/input/HTTP.
                self.assertEqual(len(claims),count) # В фоне не передавать CLAIM.
                self.assertFalse(port.receiver._chunks)
                reply=console.exchange(port,console.request_frame(command,idx,1,99))
                self.assertEqual(reply[7],command)
                self.assertEqual(reply[8],0 if command==0x30 else 1)
            self.assertEqual(len(commands),5)
            self.assertFalse(errors,errors)
        finally:
            stop.set();worker.join(1);port.receiver.close();port.close()
            os.close(master);os.close(slave)

    def test_set_pwm_parser_frame_and_applied_value(self):
        for idx in range(6):
            for pwm in (0, 100, 150, 255):
                self.assertEqual(console.parse_command(f"set_pwm {idx} {pwm}"), (0x32, idx, pwm))
                request = console.request_frame(0x32, idx, 42, 99, pwm)
                self.assertEqual(request[6:10], bytes((14, 0x32, 0, idx)))
                self.assertEqual(request[10:18], bytes((pwm,)) + bytes(7))
                self.assertEqual(int.from_bytes(request[26:], "big"),
                                 binascii.crc_hqx(request[2:26], 0xffff))
                # Ответ содержит применённый PWM, не обязательно запрошенный (fault mode).
                reply = response(request, b"\xe6" + bytes(7))
                self.assertEqual(console.describe(reply), f"Вентилятор {idx}: PWM применён=230")
        for line in ("set_pwm", "set_pwm 0", "set_pwm 0 1 extra", "set_pwm -1 0",
                     "set_pwm 6 0", "set_pwm 0 -1", "set_pwm 0 256", "set_pwm 0 1.5",
                     "set_pwm x 0", "set_pwm 0 x", "set_pwm １ 0", "set_pwm 0 ２",
                     "set_pwm 0000 1", "set_pwm 0 0000"):
            with self.subTest(line=line), self.assertRaises(ValueError):
                console.parse_command(line)
        for idx, pwm in ((6, 0), (-1, 0), (0, None), (0, -1), (0, 256), (0, True), (0, 1.5)):
            with self.assertRaises(ValueError):
                console.request_frame(0x32, idx, 42, 99, pwm)
        with self.assertRaises(ValueError):
            console.request_frame(0x30, 0, 42, 99, 100)
        with self.assertRaises(ValueError):
            console.describe(response(console.request_frame(0x32, 0, 42, 99, 0),
                                      b"\x00\x01" + bytes(6)))

    def test_set_pwm_exchange_sends_once_and_handles_error(self):
        for status in (0, 7):
            request = console.request_frame(0x32, 5, 42, 99, 150)
            reply = response(request, b"\x96" + bytes(7) if status == 0 else bytes(8), status)
            chunks = permits(request) + [b"".join(console.envelope(b, 99) for b in reply)]
            sent = []
            port = SimpleNamespace(quiet=lambda trace=None: None, send=sent.append,
                                   read=lambda timeout: chunks.pop(0))
            self.assertEqual(console.exchange(port, request), reply)
            frames = [wire[3::5] for wire in sent]
            self.assertEqual([frame[7] for frame in frames], [0xf1, 0x32])
            self.assertEqual(frames[-1], request)
            if status:
                self.assertIn("DEVICE_TIMEOUT", console.describe(reply))

    def test_main_fan_commands_and_invalid_input(self):
        port = SimpleNamespace(quiet=lambda trace=None: None, close=lambda: None)
        requests = []
        def exchange(_port, request, trace):
            requests.append(request)
            if request[7] == 0x31:
                return response(request, (1800).to_bytes(4, "big") + bytes(4))
            return response(request, bytes((request[10] if request[7] == 0x32 else 150,)) + bytes(7))
        output = io.StringIO()
        with patch.object(sys, "argv", ["sensor_console.py", "test"]), \
                patch.object(console, "Port", return_value=port), \
                patch.object(console, "WindowsPort", return_value=port), \
                patch.object(console, "exchange", side_effect=exchange), \
                patch("builtins.input", side_effect=["set_pwm 5 150", "pwm 5", "rpm 5",
                                                    "set_pwm 5 256", "set_pwm 5 0", "exit"]), \
                redirect_stdout(output):
            console.main()
        self.assertEqual([r[7] for r in requests], [0x32, 0x30, 0x31, 0x32])
        self.assertEqual([r[10] for r in requests], [150, 0, 0, 0])
        self.assertTrue(all(r[6] == 14 and r[9] == 5 for r in requests))
        self.assertIn("PWM применён=150", output.getvalue())
        self.assertIn("PWM применён=0", output.getvalue())
        self.assertIn("RPM=1800", output.getvalue())

    def test_global_fan_indices_remain_virtual_node14(self):
        for name, command in (("pwm", 0x30), ("rpm", 0x31)):
            self.assertEqual(console.parse_command(name + " 5"), (command, 5))
            request = console.request_frame(command, 5, 42, 99)
            self.assertEqual(request[6], 14)
            value = 230 if name == "pwm" else 1800
            width = 1 if name == "pwm" else 4
            self.assertIn(str(value), console.describe(response(request, value.to_bytes(width, "big") + bytes(8-width))))
            with self.assertRaises(ValueError):
                console.parse_command(name + " 6")

    def test_door_routes_to_buttons_and_validates_boolean_payload(self):
        self.assertEqual(console.parse_command("door 1"), (0x22, 1))
        with self.assertRaises(ValueError):
            console.parse_command("door 2")
        request = console.request_frame(0x22, 1, 42, 99)
        self.assertEqual(request[6], 15)
        self.assertIn("закрыта", console.describe(response(request, b"\x01" + bytes(7))))
        self.assertIn("открыта", console.describe(response(request)))
        for data in (b"\x02" + bytes(7), b"\x01\x01" + bytes(6)):
            with self.assertRaises(ValueError):
                console.describe(response(request, data))
        reply = response(request, b"\x01" + bytes(7))
        chunks = permits(request) + [b"".join(console.envelope(b,99) for b in reply)]
        port = SimpleNamespace(quiet=lambda trace=None: None, send=lambda packet: None, read=lambda timeout: chunks.pop(0))
        self.assertEqual(console.exchange(port, request), reply)

    def test_raw_trace_keeps_noise_and_reports_timeout_bytes(self):
        class FakePort:
            def __init__(self, chunk):
                self.chunk = chunk
                self.now = 0
            def quiet(self, trace=None):
                pass
            def send(self, packet):
                pass
            def read(self, timeout):
                self.now += 1
                chunk, self.chunk = self.chunk, b""
                return chunk
        request = console.request_frame(0x20, 0, 1, 1)
        for chunk in (b"", b"legacy\r63\x0c\n"):
            port, trace = FakePort(chunk), []
            with patch.object(console.time, "monotonic", side_effect=lambda: port.now):
                with self.assertRaisesRegex(TimeoutError, f"сырых байтов RX: {len(chunk)}"):
                    console.exchange(port, request, trace.append)
            self.assertFalse(any(line.startswith("TX RAW:") for line in trace))
            if chunk:
                self.assertIn("RX RAW: " + chunk.hex(" ").upper(), trace)

    def test_raw_trace_and_matching_response(self):
        request = console.request_frame(0x20, 3, 1, 1)
        stale = response(console.request_frame(0x20, 3, 2, 1), status=1)
        reply = response(request, status=1)
        chunks = permits(request) + [b"noise", b"".join(console.envelope(b, 99) for b in stale + reply)]
        port = SimpleNamespace(quiet=lambda trace=None: None, send=lambda packet: None, read=lambda timeout: chunks.pop(0))
        trace = []
        self.assertEqual(console.exchange(port, request, trace.append), reply)
        self.assertIn("RX RAW: 6E 6F 69 73 65", trace)
        self.assertTrue(any("другого запроса/версии" in line for line in trace))

    def test_probe_command_uses_hub_validation_only(self):
        class FakePort:
            def __init__(self, path):
                self.sent = bytearray()
                self.closed = False
            def quiet(self, trace=None):
                pass
            def send(self, packet):
                self.sent.extend(packet)
            def read(self, timeout):
                if not self.sent:
                    return permits(console.request_frame(0x20,3,1,0))[0]
                request = bytes(self.sent[3::5]); self.sent.clear()
                if request[7]==0xf1:
                    actual=console.request_frame(0x20,3,int.from_bytes(request[4:6],"big"),
                                                 int.from_bytes(request[10:18],"big"))
                    return permits(actual)[1]
                self_request = console.request_frame(0x20,3,
                    int.from_bytes(request[4:6],"big"),int.from_bytes(request[18:26],"big"))
                if request!=self_request:
                    raise AssertionError("Probe must be GET_TEMP with invalid index 3")
                return b"".join(console.envelope(b,99) for b in response(request,status=1))
            def close(self):
                self.closed = True
        port = FakePort("test")
        output = io.StringIO()
        with patch.object(sys, "argv", ["sensor_console.py", "test", "--raw"]), \
                patch.object(console, "Port", return_value=port), \
                patch.object(console, "WindowsPort", return_value=port), \
                patch("builtins.input", side_effect=["probe", "exit"]), redirect_stdout(output):
            console.main()
        self.assertTrue(port.closed)
        self.assertIn("HUB_OK:", output.getvalue())
        self.assertIn("RX RAW:", output.getvalue())
        self.assertNotIn("Ошибка 1", output.getvalue())

    def test_windows_backend_without_windows_hardware(self):
        class Serial:
            def __init__(self, **kwargs):
                self.options=kwargs; self.writes=[]; self.closed=False
                self.pending=[1,0]
            def write(self, data):
                self.writes.append(data[:2]); return min(2,len(data))
            @property
            def out_waiting(self):
                return self.pending.pop(0) if self.pending else 0
            def read(self, size):
                self.size=size; return b"\r63\xa5\n"
            def close(self):
                self.closed=True
        serial=SimpleNamespace(Serial=Serial, EIGHTBITS=8, PARITY_NONE="N", STOPBITS_ONE=1)
        with patch.dict(sys.modules, {"serial":serial}):
            port=console.WindowsPort("COM12")
        self.assertEqual(port.serial.options["port"],"COM12")
        self.assertEqual(port.serial.options["baudrate"],115200)
        self.assertFalse(port.serial.options["xonxoff"])
        self.assertEqual(port.read(.01), b"\r63\xa5\n")
        self.assertEqual(port.serial.timeout,.01)
        clock=iter(i*.001 for i in range(3000))
        with patch.object(console.time,"monotonic",side_effect=lambda: next(clock)), \
                patch.object(console.time,"sleep"):
            port.send(b"\r0C\xa5\n")
        self.assertEqual(b"".join(port.serial.writes),b"\r0C\xa5\n")
        port.close(); self.assertTrue(port.serial.closed)
        port.serial.write=lambda data: 0
        with self.assertRaises(OSError):
            port.send(b"\r0C\xa5\n")
        port.serial.write=lambda data: len(data)
        with patch.object(console.time,"sleep",side_effect=AssertionError("no pacing sleeps")):
            port.send(b"\r0C\xa5\n")

    def test_windows_port_list_and_missing_dependency(self):
        ports=SimpleNamespace(comports=lambda: [SimpleNamespace(device="COM12", description="USB adapter")])
        with patch.object(console.os,"name","nt"), patch.dict(sys.modules,
                {"serial":SimpleNamespace(), "serial.tools":SimpleNamespace(list_ports=ports)}):
            self.assertEqual(console.list_ports(),["COM12: USB adapter"])
        with patch.dict(sys.modules,{"serial":None}), self.assertRaisesRegex(ImportError,"pip install pyserial"):
            console.WindowsPort("COM3")

    def test_commands_and_golden_request(self):
        self.assertEqual(console.request_frame(0x25, 0, 1, 1), bytes.fromhex(
            "A5 5A 02 01 00 01 0D 25 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 89 2C"))
        for name, (command, count) in console.COMMANDS.items():
            self.assertEqual(console.parse_command("dust" if name == "dust" else f" {name} {count-1} "), (command, count-1))
            for arg in (str(count), "-1", "1.0", "x", "00000000", "１"):
                with self.assertRaises(ValueError):
                    console.parse_command(f"{name} {arg}")
        for line in ("", "unknown 0", "temp", "temp 0 junk", "dust 0", "dust 1"):
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
        import tty
        tty.setraw(slave)  # Не эхоить ранний READY до открытия pyserial.
        errors = []

        def adapter():
            try:
                wire = bytearray(); deadline = time.monotonic()+8; last_ready=0
                ready=console.request_frame(0x20,3,1,0)
                claimed=False
                while len(wire)<140 and time.monotonic()<deadline:
                    if not claimed and time.monotonic()-last_ready>.03:
                        os.write(master,permits(ready)[0]);last_ready=time.monotonic()
                    if select.select([master],[],[],.01)[0]:
                        wire.extend(os.read(master,1024))
                        if len(wire)==140 and bytes(wire[3::5])[7]==0xf1:
                            claim=bytes(wire[3::5])
                            actual=console.request_frame(0x23,0,1,int.from_bytes(claim[10:18],"big"))
                            os.write(master,permits(actual)[1]);wire.clear();claimed=True
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
