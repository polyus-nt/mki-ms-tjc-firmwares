#!/usr/bin/env python3
"""Direct SimpleBus tests at 9600: hub 12 / buttons 15; no API or routing."""
import argparse
import os
import time

from sensor_console import Port, WindowsPort, envelope

TARGETS = {"hub": 12, "buttons": 15, "12": 12, "15": 15}
EXPECTED = {12: 0xAA, 15: 0xBB}
HELP = ("hub 3 / 12 3 — центральная плата, ответ 0xAA\n"
        "buttons 3 / 15 3 — кнопочница напрямую, ответ 0xBB\n"
        "Байт: 0..255 или 0xAA. Просто 3 — как раньше, запрос хабу. help | exit")


def parse_byte(line):
    """Decimal 0..255 or hexadecimal 0x00..0xFF."""
    try:
        value = int(line, 16 if line.lower().startswith("0x") else 10)
    except ValueError as error:
        raise ValueError("Введите байт 0..255 или 0x00..0xFF") from error
    if not 0 <= value <= 255:
        raise ValueError("Байт должен быть в диапазоне 0..255")
    return value


def parse_command(line):
    parts = line.split()
    if len(parts) == 1:
        return 12, parse_byte(parts[0])
    if len(parts) == 2 and parts[0].lower() in TARGETS:
        return TARGETS[parts[0].lower()], parse_byte(parts[1])
    raise ValueError(HELP)


def exchange_byte(port, value, trace=print, address=12):
    if address not in EXPECTED:
        raise ValueError("Тестовые адреса: 12 (hub), 15 (buttons)")
    packet = envelope(value, address)
    trace("TX RAW: " + packet.hex(" ").upper())
    port.send(packet)
    deadline = time.monotonic() + 2
    window = bytearray()
    received = 0
    while time.monotonic() < deadline:
        chunk = port.read(max(0, min(.05, deadline - time.monotonic())))
        if chunk:
            received += len(chunk)
            trace("RX RAW: " + chunk.hex(" ").upper())
        for byte in chunk:
            window.append(byte)
            if len(window) < 5:
                continue
            if (window[0] == 13 and window[4] == 10 and
                    all(c in b"0123456789ABCDEF" for c in window[1:3])):
                address, payload = int(window[1:3], 16), window[3]
                window.clear()
                if address == 99:
                    return payload
            else:
                del window[0]
    raise TimeoutError(f"Нет пакета на адрес компьютера 99 за 2 с; сырых RX байтов: {received}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="например COM13")
    args = parser.parse_args()
    port = None
    try:
        port = WindowsPort(args.port, baudrate=9600) if os.name == "nt" else Port(args.port, baudrate=9600)
        print("9600, 8N1. Только hub-test + buttons-test! Остановите FastAPI и других владельцев порта.")
        port.quiet(print)
        print(HELP)
        while True:
            line = input("hub-test> ").strip()
            if line in ("exit", "quit"):
                break
            if not line:
                continue
            if line == "help":
                print(HELP)
                continue
            try:
                address, value = parse_command(line)
                port.quiet(print) # Не принимать накопившийся ответ прошлого запроса.
                reply = exchange_byte(port, value, address=address)
                label = "HUB_OK" if address == 12 else "BUTTONS_OK"
                print(f"{label if reply == EXPECTED[address] else 'НЕОЖИДАННЫЙ ОТВЕТ'}: "
                      f"{reply} (0x{reply:02X}), ожидалось 0x{EXPECTED[address]:02X}")
            except ValueError as error:
                print(error)
            except TimeoutError as error:
                print(error)
                port.quiet(print)
    except (KeyboardInterrupt, EOFError):
        print("\nКонсоль закрыта.")
    except (OSError, ImportError) as error:
        parser.exit(1, f"Ошибка порта: {error}\n")
    finally:
        if port is not None:
            port.close()


if __name__ == "__main__":
    main()
