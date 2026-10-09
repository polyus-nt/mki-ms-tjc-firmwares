# Runtime МС-ТЮК

Источник: https://github.com/kruzhok-team/lapki-compiler
Ревизия: `c0e235f364f21b1a7ba8dbb598ef0bc983511af6`.
Скопированы только 15 файлов, необходимых рукописным прошивкам, из
`compiler/library/tjc-ms1-ard-a1/1.0/source/`. Для main-a4 используемые 13
общих файлов побайтно совпадают с `compiler/library/tjc-ms1-main-a4/1.0/source/`.
ArduinoAdapter.hpp и arduino_link.c в сборку хаба не входят.

Это исходники без локальных изменений. SHA-256 закреплены в
`scripts/build_gateways.py`; CRLF перед проверкой нормализуется в LF.
Сборка готовит копию в build и применяет существующие локальные исправления:
FIFO SimpleBus 64 байта, overflow, короткие IRQ-секции, безопасный побайтный
сдвиг UART и исправленная защёлка overflow ArduinoAdapter. Upstream-файлы
в firmware/runtime при сборке не изменяются. Подробности — docs/GATEWAY-FIRMWARE.md.

## Лицензии и уведомления

`LICENSE` — неизменённый GPLv3 из корня lapki-compiler.
Заголовки copyright/лицензий сторонних CMSIS/ST файлов сохранены в исходниках:
ARM CMSIS содержит BSD-3-Clause notice; ST-файлы ссылаются на Apache-2.0
(текст в LICENSE-APACHE-2.0). Этот каталог не заменяет лицензии компонентов
единой лицензией и не назначает новую лицензию пользовательским файлам проекта.

Проверено при импорте: хеш каждого файла совпадает с закреплённым контрактом.
Сборка не скачивает lapki-compiler и не требует его отдельного checkout.
