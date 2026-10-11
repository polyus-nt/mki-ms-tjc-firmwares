# LED обычной лампочницы ФабЛаб

Источники: [lapki-compiler](https://github.com/kruzhok-team/lapki-compiler/tree/c0e235f364f21b1a7ba8dbb598ef0bc983511af6/compiler/library/tjc-ms1-lmp-a3/1.0/source),
ревизия c0e235f364f21b1a7ba8dbb598ef0bc983511af6,
платформа tjc-ms1-lmp-a3/1.0. Это НЕ lmp8 и НЕ LED центральной платы.

В этом каталоге LED.h, PWM.hpp, PWMInit.hpp, Connector.hpp, UART.hpp, system.c.
Код upstream не изменён; нормализованы только CRLF и пробелы/переводы строк
в конце файла. SHA-256 закреплены в scripts/build_lmp.py.
Остальные 11 общих файлов в ../runtime совпадают с lmp-a3; они проверяются
по закреплённым хешам build_gateways.FILES, в том числе linker script и SimpleBus.
LED(1/2/3) управляет тремя пользовательскими выходами.

Build проверяет оригиналы, затем патчит только build/lmp/platform:
- LED constructor загружает стартовый latch ДО GPIO output-mode:
  замки 1/2 HIGH, пылесос 3 LOW; не вызывает startup off() для замков.
- Убирается глобальная GPIO-инициализация Connector, которая иначе даёт
  LOW-пульс всем трём выходам ещё до конструкторов LED.
- UART получает существующие IRQ/FIFO/unaligned-access исправления проекта.

В рабочем коде используем LED.on()/off(); на обычной lmp GPIO меняется сразу,
LEDController здесь нет. Не задаём промежуточную яркость реле.
GNU++14 нужен для цифровых разделителей в исходном PWMInit.hpp.
Лицензия GPLv3 находится в ../runtime/LICENSE; общие ARM/ST лицензии сохранены.
Подробности в ../../docs/LMP-FIRMWARE.md.
