#!/usr/bin/env python3
"""SimpleBus-only console for firmware/hub-test; no API frames or CRC."""
import argparse
import os
import time

from sensor_console import Port, WindowsPort, envelope


def parse_byte(line):
    """Decimal 0..255 or hexadecimal 0x00..0xFF."""
    try:
        value = int(line, 16 if line.lower().startswith("0x") else 10)
    except ValueError as error:
        raise ValueError("Введите байт 0..255 или 0x00..0xFF") from error
    if not 0 <= value <= 255:
        raise ValueError("Байт должен быть в диапазоне 0..255")
    return value


def exchange_byte(port, value, trace=print):
    packet = envelope(value, 12)
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
        port = WindowsPort(args.port) if os.name == "nt" else Port(args.port)
        print("9600, 8N1. Только тестовая прошивка hub-test! Закройте другие владельцы порта.")
        port.quiet(print)
        print("Введите байт: 0..255 или 0xAA. exit — выход. Ожидаемый ответ: 0xAA.")
        while True:
            line = input("hub-test> ").strip()
            if line in ("exit", "quit"):
                break
            if not line:
                continue
            try:
                reply = exchange_byte(port, parse_byte(line))
                print(f"{'HUB_OK' if reply == 0xAA else 'НЕОЖИДАННЫЙ ОТВЕТ'}: {reply} (0x{reply:02X})")
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
