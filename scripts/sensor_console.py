#!/usr/bin/env python3
"""Read-only sensor console: PC -> SimpleBus hub -> gateway -> Arduino (v2).

Windows requires pyserial; Linux/macOS use the standard library. Use the MC-TUK
adapter, not Arduino USB. No automatic retries. Close other owners of the port.
"""
import argparse
import binascii
import glob
import os
import secrets
import select
import time

COMMANDS = {"temp": (0x20, 3), "pressure": (0x23, 3), "voc": (0x21, 2),
            "humidity": (0x24, 3), "dust": (0x25, 2)}
STATUSES = ("OK", "INVALID_INDEX", "INVALID_ARGUMENT", "UNKNOWN_COMMAND",
            "NOT_IMPLEMENTED", "SENSOR_UNAVAILABLE", "BUSY", "DEVICE_TIMEOUT",
            "UNSUPPORTED_VERSION", "INTERNAL_ERROR", "REQUEST_ID_CONFLICT",
            "INVALID_NODE", "TRANSPORT_OVERFLOW")
HELP = "probe (проверка хаба) | temp 0..2 | pressure 0..2 | voc 0..1 | humidity 0..2 | dust 0..1 | help | exit"


def parse_command(line):
    parts = line.split()
    if len(parts) != 2 or parts[0] not in COMMANDS:
        raise ValueError("Введите команду и индекс. " + HELP)
    command, count = COMMANDS[parts[0]]
    if not parts[1].isascii() or not parts[1].isdecimal() or len(parts[1]) > 3:
        raise ValueError("Индекс — целое число с нуля.")
    idx = int(parts[1])
    if idx >= count:
        raise ValueError(f"Индекс {parts[0]} должен быть 0..{count-1}.")
    return command, idx


def request_frame(command, idx, tx_id, session):
    frame = b"\xa5\x5a\x02\x01" + tx_id.to_bytes(2, "big")
    frame += bytes((13, command, 0, idx)) + bytes(8) + session.to_bytes(8, "big")
    return frame + binascii.crc_hqx(frame[2:], 0xffff).to_bytes(2, "big")


def envelope(byte, address=12):
    return b"\r" + f"{address:02X}".encode("ascii") + bytes((byte,)) + b"\n"


def describe(reply):
    status, command, data = reply[8], reply[7], reply[10:18]
    if status >= len(STATUSES):
        raise ValueError("Неизвестный status")
    if status:
        if any(data):
            raise ValueError("Ненулевые data в ответе ошибки")
        hint = ""
        if status == 5:
            hint = " — нет валидного измерения; VOC может прогреваться около 45 с"
        elif status == 7:
            hint = " — хаб/шлюз не дождался нижнего узла"
        return f"Ошибка {status}: {STATUSES[status]}{hint}"
    age = int.from_bytes(data[5:7], "big")
    if data[4] not in (1, 3) or age == 65535 or data[7]:
        raise ValueError("Некорректное качество/резервные байты")
    value = int.from_bytes(data[:4], "big")
    if command == 0x20:
        text = f"Температура: {int.from_bytes(data[:4], 'big', signed=True)/100:.2f} °C"
    elif command == 0x23:
        text = f"Давление: {value} Па"
    elif command == 0x21 and 1 <= value <= 500:
        text = f"VOC Index: {value} (не ppm)"
    elif command == 0x24 and value <= 10000:
        text = f"Влажность: {value/100:.2f} % RH"
    elif command == 0x25 and int.from_bytes(data[:2], "big") <= 1023:
        text = (f"Пыль: ADC={int.from_bytes(data[:2], 'big')}; "
                f"напряжение={int.from_bytes(data[2:4], 'big')/1000:.3f} В (не концентрация)")
    else:
        raise ValueError("Некорректная команда или значение")
    return f"{text}\nvalid=1 fresh={int(data[4] == 3)} age_ms={age}"


class ReplyParser:
    """Two bounded sliding windows: 5-byte SimpleBus and 28-byte API."""
    def __init__(self):
        self.bus = bytearray()
        self.frame = bytearray()
        self.last = self.first = 0.0

    def feed(self, chunk, now):
        for byte in chunk:
            self.bus.append(byte)
            if len(self.bus) < 5:
                continue
            if (self.bus[0] != 13 or self.bus[4] != 10 or
                    any(c not in b"0123456789ABCDEF" for c in self.bus[1:3])):
                del self.bus[0]
                continue
            address, payload = int(self.bus[1:3], 16), self.bus[3]
            self.bus.clear()
            if address != 99:  # ignore echo, gateway traffic and other recipients
                continue
            if now - self.last >= .250 or now - self.first >= 1.0:
                self.frame.clear()
            self.last = now
            if not self.frame:
                if payload != 0xa5:
                    continue
                self.first = now
            self.frame.append(payload)
            if len(self.frame) == 2 and self.frame[1] != 0x5a:
                self.frame[:] = b"\xa5" if payload == 0xa5 else b""
            if len(self.frame) == 28:
                candidate = bytes(self.frame)
                if binascii.crc_hqx(candidate[2:26], 0xffff) == int.from_bytes(candidate[26:], "big"):
                    self.frame.clear()
                    yield candidate
                else:
                    pos = candidate.find(b"\xa5\x5a", 1)
                    self.frame[:] = candidate[pos:] if pos >= 0 else (b"\xa5" if payload == 0xa5 else b"")
                    self.first = now


class Port:
    def __init__(self, path):
        import termios
        self.termios = termios
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            attrs = termios.tcgetattr(self.fd)
            attrs[0] = attrs[1] = attrs[3] = 0  # raw binary; no CR/LF or flow-control transformations
            attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            attrs[4] = attrs[5] = termios.B9600
            attrs[6][termios.VMIN] = attrs[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        except BaseException:
            os.close(self.fd)
            raise

    def read(self, timeout):
        if not select.select([self.fd], [], [], max(0, timeout))[0]:
            return b""
        data = os.read(self.fd, 1024)
        if not data:
            raise OSError("Порт отключён")
        return data

    def send(self, packet):
        deadline = time.monotonic() + 1
        while packet:
            left = deadline - time.monotonic()
            if left <= 0 or not select.select([], [self.fd], [], left)[1]:
                raise OSError("Тайм-аут записи")
            packet = packet[os.write(self.fd, packet):]
        # Дождаться передачи пакета, затем >=2 мс паузы; не меняем RTS/DTR вручную.
        self.termios.tcdrain(self.fd)
        time.sleep(.002)

    def quiet(self, trace=None):
        started = last = time.monotonic()
        while time.monotonic() - last < 1:
            if time.monotonic() - started >= 3:
                raise OSError("На шине нет секунды тишины; проверьте другие инициаторы")
            data = self.read(.05)
            if data:
                if trace:
                    trace("RX RAW (ожидание тишины): " + data.hex(" ").upper())
                last = time.monotonic()

    def close(self):
        os.close(self.fd)


class WindowsPort(Port):
    def __init__(self, path):
        try:
            import serial
        except ImportError as error:
            raise ImportError("Установите pyserial: py -3 -m pip install pyserial") from error
        self.serial = serial.Serial(port=path, baudrate=9600, bytesize=serial.EIGHTBITS,
                                    parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                                    timeout=.05, write_timeout=1,
                                    xonxoff=False, rtscts=False, dsrdtr=False)

    def read(self, timeout):
        self.serial.timeout = max(0, timeout)
        return self.serial.read(1024)

    def send(self, packet):
        deadline = time.monotonic() + 1
        earliest_end = time.monotonic() + len(packet) * 10 / 9600
        while packet:
            if time.monotonic() >= deadline:
                raise OSError("Тайм-аут записи")
            written = self.serial.write(packet)
            if not written:
                raise OSError("COM-порт не принимает данные")
            packet = packet[written:]
        # pyserial Windows flush polls at 50 ms: 28 packets could exceed the
        # hub's 1 s frame budget. Use bounded queue polling without that delay.
        while self.serial.out_waiting:
            if time.monotonic() >= deadline:
                raise OSError("Тайм-аут передачи COM-порта")
            time.sleep(0)
        gap_end = max(earliest_end, time.monotonic()) + .002
        while time.monotonic() < gap_end:
            time.sleep(0)  # yield; avoid coarse Windows sleep(2ms) rounding

    def close(self):
        self.serial.close()


def list_ports():
    if os.name == "nt":
        try:
            from serial.tools import list_ports as serial_ports
        except ImportError as error:
            raise ImportError("Установите pyserial: py -3 -m pip install pyserial") from error
        return [f"{port.device}: {port.description}" for port in serial_ports.comports()]
    return sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*") + glob.glob("/dev/cu.*"))


def exchange(port, request, trace=None):
    parser = ReplyParser()
    started = time.monotonic()
    deadline = started + 5
    received = 0
    if trace:
        trace("TX RAW: " + b"".join(envelope(byte) for byte in request).hex(" ").upper())
    for byte in request:
        port.send(envelope(byte))
    if trace:
        trace(f"TX: отправлено 28 пакетов SimpleBus (140 байт) за {time.monotonic()-started:.3f} с")
    while time.monotonic() < deadline:
        chunk = port.read(max(0, min(.05, deadline-time.monotonic())))
        received += len(chunk)
        if chunk and trace:
            trace("RX RAW: " + chunk.hex(" ").upper())
        for reply in parser.feed(chunk, time.monotonic()):
            if (reply[2:4] == b"\x02\x02" and reply[4:8] == request[4:8]
                    and reply[9] == request[9] and reply[18:26] == request[18:26]):
                try:
                    describe(reply)
                except ValueError as error:
                    if trace:
                        trace(f"RX API отклонён: {error}; " + reply.hex(" ").upper())
                    continue
                return reply
            elif trace:
                trace("RX API другого запроса/версии: " + reply.hex(" ").upper())
    detail = ("сырых байтов RX: 0 — порт не вернул ни одного байта"
              if not received else f"сырых байтов RX: {received} — данные пришли, подходящий ответ не разобран")
    raise TimeoutError("HOST_TIMEOUT: подходящий ответ от хаба не пришёл за 5 с; " + detail)


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("port", nargs="?", help="порт адаптера МС-ТЮК, например COM3 или /dev/ttyUSB0")
    args.add_argument("--hex", action="store_true", help="показывать кадры TX/RX без SimpleBus-обёртки")
    args.add_argument("--raw", action="store_true", help="показывать все сырые RX до фильтрации, TX и длительность отправки")
    args.add_argument("--list-ports", action="store_true")
    options = args.parse_args()
    if options.list_ports:
        try:
            print("\n".join(list_ports()) or "Порты не найдены")
        except (ImportError, OSError) as error:
            args.exit(1, str(error) + "\n")
        return
    if not options.port:
        args.error("укажите порт адаптера МС-ТЮК или --list-ports")
    port = None
    try:
        port = WindowsPort(options.port) if os.name == "nt" else Port(options.port)
        trace = (lambda message: print(message, flush=True)) if options.raw else None
        print("9600, 8N1. Через ХАБ 12 и шлюз sensors 13. Закройте commutator/другие владельцы порта.")
        port.quiet(trace)
        print(HELP)
        session, tx = secrets.randbits(64) or 1, 1
        while True:
            line = input("mstjuk> ").strip()
            if not line:
                continue
            if line in ("exit", "quit"):
                break
            if line == "help":
                print(HELP)
                continue
            try:
                # Индекс 3 не существует: хаб сам отвечает INVALID_INDEX,
                # не отправляя запрос шлюзу. Проверяем только PC <-> Hub.
                probe = line == "probe"
                command, idx = (0x20, 3) if probe else parse_command(line)
            except ValueError as error:
                print(error)
                continue
            request = request_frame(command, idx, tx, session)
            tx += 1
            if tx > 65535:
                tx, session = 1, (session + 1) % (1 << 64) or 1
            if options.hex:
                print("TX HEX:", request.hex(" ").upper())
            try:
                reply = exchange(port, request, trace)
            except TimeoutError as error:
                print(error)
                port.quiet(trace)
                continue  # no automatic retry
            if probe and reply[8] == 1:
                print("HUB_OK: хаб ответил INVALID_INDEX, как и ожидалось. Шлюз и Arduino не участвовали.")
            elif probe:
                print("HUB_PROBE: неожиданный ответ; " + describe(reply))
            else:
                print(describe(reply))
            if options.hex:
                print("RX HEX:", reply.hex(" ").upper())
            if reply[8] in (7, 9, 12):
                port.quiet(trace)
    except (KeyboardInterrupt, EOFError):
        print("\nКонсоль закрыта.")
    except (OSError, ImportError) as error:
        args.exit(1, f"Ошибка порта: {error}\nПроверьте имя порта и закройте другие программы, использующие его.\n")
    finally:
        if port is not None:
            port.close()


if __name__ == "__main__":
    main()
