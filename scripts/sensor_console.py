#!/usr/bin/env python3
"""Sensor and fan console: PC -> SimpleBus hub -> gateway -> Arduino (v2).

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
from continuous_rx import ContinuousReader, PermitTracker

COMMANDS = {"temp": (0x20, 3), "pressure": (0x23, 3), "voc": (0x21, 2),
            "humidity": (0x24, 3), "dust": (0x25, 1), "door": (0x22, 2),
            "pwm": (0x30, 6), "rpm": (0x31, 6)}
STATUSES = ("OK", "INVALID_INDEX", "INVALID_ARGUMENT", "UNKNOWN_COMMAND",
            "NOT_IMPLEMENTED", "SENSOR_UNAVAILABLE", "BUSY", "DEVICE_TIMEOUT",
            "UNSUPPORTED_VERSION", "INTERNAL_ERROR", "REQUEST_ID_CONFLICT",
            "INVALID_NODE", "TRANSPORT_OVERFLOW")
HELP = ("probe (проверка хаба) | temp 0..2 | pressure 0..2 | voc 0..1 | humidity 0..2 | "
        "dust | door 0..1 | pwm 0..5 | rpm 0..5 | set_pwm idx(0..5) pwm(0..255) | help | exit\n"
        "set_pwm реально включает вентилятор; PWM=0 выключает. Индексы общие для шести вентиляторов.")


def parse_command(line):
    parts = line.split()
    if parts and parts[0] == "set_pwm":
        if len(parts) != 3:
            raise ValueError("Формат: set_pwm idx(0..5) pwm(0..255).")
        values = []
        for text, limit, label in ((parts[1], 5, "Индекс вентилятора"),
                                   (parts[2], 255, "PWM")):
            if (not text.isascii() or not text.isdecimal() or len(text) > 3
                    or int(text) > limit):
                raise ValueError(f"{label} — целое число 0..{limit}.")
            values.append(int(text))
        return 0x32, values[0], values[1]
    if parts == ["dust"]:
        return 0x25, 0
    if parts and parts[0] == "dust":
        raise ValueError("Команда dust не принимает индекс.")
    if len(parts) != 2 or parts[0] not in COMMANDS:
        raise ValueError("Введите команду и индекс. " + HELP)
    command, count = COMMANDS[parts[0]]
    if not parts[1].isascii() or not parts[1].isdecimal() or len(parts[1]) > 3:
        raise ValueError("Индекс — целое число с нуля.")
    idx = int(parts[1])
    if idx >= count:
        raise ValueError(f"Индекс {parts[0]} должен быть 0..{count-1}.")
    return command, idx


def request_frame(command, idx, tx_id, session, pwm=None):
    if command == 0x32:
        if type(idx) is not int or not 0 <= idx < 6:
            raise ValueError("Индекс вентилятора — целое число 0..5.")
        if type(pwm) is not int or not 0 <= pwm <= 255:
            raise ValueError("PWM — целое число 0..255.")
    elif pwm is not None:
        raise ValueError("PWM допустим только для set_pwm.")
    frame = b"\xa5\x5a\x02\x01" + tx_id.to_bytes(2, "big")
    node = 15 if command == 0x22 else (14 if command in (0x30, 0x31, 0x32) else 13)
    data = bytes((pwm,)) + bytes(7) if command == 0x32 else bytes(8)
    frame += bytes((node, command, 0, idx)) + data + session.to_bytes(8, "big")
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
    if command == 0x22:
        if data[0] not in (0, 1) or any(data[1:]):
            raise ValueError("Некорректное состояние концевика/резервные байты")
        return f"Дверь {reply[9]}: {'закрыта' if data[0] else 'открыта'} (state={bool(data[0])})"
    if command in (0x30, 0x31, 0x32):
        width = 4 if command == 0x31 else 1
        if any(data[width:]):
            raise ValueError("Ненулевые резервные байты вентилятора")
        value = int.from_bytes(data[:width], "big")
        label = "RPM" if command == 0x31 else ("PWM применён" if command == 0x32 else "PWM")
        return f"Вентилятор {reply[9]}: {label}={value}"
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
            attrs[4] = attrs[5] = termios.B115200
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
        # Дождаться передачи пакета; не меняем RTS/DTR вручную.
        self.termios.tcdrain(self.fd)
        # Нет межпакетной паузы: передаём весь API-кадр одной записью.

    def quiet(self, trace=None):
        # Только отбросить старое READY/поздний ответ; не ждать секунду тишины.
        if hasattr(self, "serial"):
            self.serial.reset_input_buffer()
        else:
            self.termios.tcflush(self.fd, self.termios.TCIFLUSH)

    def close(self):
        os.close(self.fd)


class WindowsPort(Port):
    def __init__(self, path):
        try:
            import serial
        except ImportError as error:
            raise ImportError("Установите pyserial: py -3 -m pip install pyserial") from error
        self.serial = serial.Serial(port=path, baudrate=115200, bytesize=serial.EIGHTBITS,
                                    parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                                    timeout=.05, write_timeout=1,
                                    xonxoff=False, rtscts=False, dsrdtr=False)

    def read(self, timeout):
        self.serial.timeout = max(0, timeout)
        first = self.serial.read(1)
        waiting = min(getattr(self.serial, "in_waiting", 0), 4095)
        return first + self.serial.read(waiting) if first and waiting else first

    def send(self, packet):
        deadline = time.monotonic() + 1
        while packet:
            if time.monotonic() >= deadline:
                raise OSError("Тайм-аут записи")
            written = self.serial.write(packet)
            if not written:
                raise OSError("COM-порт не принимает данные")
            packet = packet[written:]
        # Одна запись 140 байт; никаких 28 отдельных flush/sleep.

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
    receiver = getattr(port, "receiver", None)
    if receiver is not None:
        receiver.begin()
        try:
            return _exchange(port, request, trace, receiver)
        finally:
            receiver.end()
    return _exchange(port, request, trace)


def _exchange(port, request, trace=None, receiver=None):
    if receiver is None:
        port.quiet(trace)
    parser = ReplyParser()
    deadline = time.monotonic() + 5
    received, ticket, sent = 0, None, False
    nonce = request[18:26]
    permits = PermitTracker()
    claim_until = 0.0
    ready_seen = claims_sent = grants_seen = 0

    def send(frame):
        wire = b"".join(envelope(byte) for byte in frame)
        if trace:
            trace("TX RAW: " + wire.hex(" ").upper())
        port.send(wire)

    def crc(frame):
        frame[26:28] = binascii.crc_hqx(frame[2:26],0xffff).to_bytes(2,"big")
        return bytes(frame)

    while time.monotonic() < deadline:
        chunk = (receiver or port).read(max(0,min(.005,deadline-time.monotonic())))
        received += len(chunk)
        now = time.monotonic()
        received_at = receiver.received_at if receiver else now
        newest = None
        # Разобрать всю пачку ДО CLAIM, не отправлять по CLAIM на каждый READY.
        for reply in list(parser.feed(chunk, now)):
            is_control = (reply[2:4]==b"\x02\x02" and reply[6]==12 and
                          reply[8:10]==bytes(2) and reply[10:18]==b"\x00\x64"+bytes(6))
            if not sent and is_control and reply[7]==0xf0:
                ready_seen += 1
                key = (int.from_bytes(reply[18:26], "big"), int.from_bytes(reply[4:6], "big"))
                if permits.accept(key, received_at):
                    newest = reply
                continue
            effective = (int.from_bytes(ticket[18:26],"big") ^
                         int.from_bytes(nonce,"big")) if ticket else None
            if not sent and ticket and is_control and reply[7]==0xf2:
                if time.monotonic() >= claim_until:
                    continue
                if reply[4:6]!=ticket[4:6] or int.from_bytes(reply[18:26],"big")!=effective:
                    continue
                actual=bytearray(request); actual[4:6]=reply[4:6]; actual[18:26]=reply[18:26]
                grants_seen += 1
                request=crc(actual); sent=True
                send(request)
                continue
            if sent and (reply[2:4]==b"\x02\x02" and reply[4:8]==request[4:8] and
                         reply[9]==request[9] and reply[18:26]==request[18:26]):
                try:
                    describe(reply)
                except ValueError as error:
                    if trace: trace(f"RX API отклонён: {error}; " + reply.hex(" ").upper())
                    continue
                return reply
            elif trace:
                trace("RX API другого запроса/версии: " + reply.hex(" ").upper())
        if newest is not None and not sent and time.monotonic() >= claim_until:
            # Отложенный в потоке Python пакет уже не годится для 100-мс окна.
            if time.monotonic() - received_at < .075:
                ticket = newest
                claim = bytearray(ticket)
                claim[3], claim[7] = 1, 0xf1
                claim[10:18] = nonce
                claim_until = received_at + .100
                claims_sent += 1
                send(crc(claim))
        if chunk and trace:
            suffix = f" ... ({len(chunk)} bytes)" if len(chunk)>160 else ""
            trace("RX RAW: " + chunk[:160].hex(" ").upper() + suffix)
    raise TimeoutError(f"HOST_TIMEOUT: разрешение/ответ хаба не пришёл за 5 с; сырых байтов RX: {received}; "
                       f"команда отправлена: {sent}; READY={ready_seen} CLAIM={claims_sent} GRANT={grants_seen}")

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
        print("115200, 8N1. ХАБ 12: датчики — шлюз 13, двери — кнопочница 15; "
              "вентиляторы 0..3 — шлюз 14, 4/5 — шлюз 17. Закройте других владельцев порта.")
        port.quiet(trace)
        # Чтение продолжается во время input(), без CLAIM/команд в фоне.
        if hasattr(port, "read"):
            port.receiver = ContinuousReader(port.read)
            print("Фоновый RX включён: порт читается и во время ожидания команды.")
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
                command, idx, *arguments = (0x20, 3) if probe else parse_command(line)
            except ValueError as error:
                print(error)
                continue
            request = request_frame(command, idx, tx, session, *arguments)
            tx += 1
            if tx > 65535:
                tx, session = 1, (session + 1) % (1 << 64) or 1
            if options.hex:
                print("TX HEX:", request.hex(" ").upper())
            try:
                reply = exchange(port, request, trace)
            except TimeoutError as error:
                print(error)
                port.receiver.clear() if hasattr(port, "receiver") else port.quiet(trace)
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
                port.receiver.clear() if hasattr(port, "receiver") else port.quiet(trace)
    except (KeyboardInterrupt, EOFError):
        print("\nКонсоль закрыта.")
    except (OSError, ImportError) as error:
        args.exit(1, f"Ошибка порта: {error}\nПроверьте имя порта и закройте другие программы, использующие его.\n")
    finally:
        if port is not None:
            try:
                if hasattr(port, "receiver"):
                    port.receiver.close()
            finally:
                port.close()


if __name__ == "__main__":
    main()
