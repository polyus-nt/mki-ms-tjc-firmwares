# МС-ТЮК теплицы-химреактора

Только МС-ТЮК-прошивки, локальный runtime, билд, документация и C++-тест.
Фаблаб-проект в корне не меняется.

| Плата | SimpleBus | Назначение |
| --- | --- | --- |
| Хаб | 12 | Компьютер — адрес 99 |
| Sensors-шлюз | 13 | Arduino BMP280×2, DHT11×2, MQ-2×2 |
| Actuator-шлюз | 14 | Arduino, вентиляторы 0/1 |
| Кнопочница | 15 | Концевики Button(1)/Button(2) |
| lmp8 | 16 | Общий замок на пине 1, шесть реле на пинах 2–7 |

Химреактор имеет отдельную физическую шину: адреса пересекаются с фаблабом.
Релейный вентилятор 0..10: all-off → пауза 250 мс → новая матрица.
HIGH открывает замки; LOW закрывает. Реле вентилятора включаются HIGH,
выключаются LOW. При старте пин замков 1 LOW, реле 2..7 LOW; уровни предзагружены до OUTPUT.
Используются классы LED и Button. [PINOUT.md](PINOUT.md) · [PROTOCOL.md](PROTOCOL.md).

## Сборка

Из корня tjc-sketch:

```powershell
cd "chem-reactor-теплица"
py -3 scripts/build.py --target all --toolchain-bin "C:\tools\arm-gnu\bin"
```

Нужен ARM GNU Toolchain. Если arm-none-eabi-g++/objcopy/size в PATH,
--toolchain-bin не нужен. Для сборки прошивок соседние репозитории
и checkout lapki-compiler не нужны: runtime включён локально.

| Плата | Бинарник |
| --- | --- |
| Хаб | `build/hub/sketch.bin` |
| Sensors-шлюз | `build/gateways/sensors/sketch.bin` |
| Actuator-шлюз | `build/gateways/actuator/sketch.bin` |
| Кнопочница | `build/buttons/sketch.bin` |
| lmp8 | `build/lmp8/sketch.bin` |

Отдельные цели: hub, gateways, buttons, lmp8. Один шлюз:
`py -3 scripts/build_gateways.py --gateway sensors` или actuator.
--prepare-only готовит runtime без компиляции.
Загрузка как **usercode по адресу 0x08003800**, не в начало flash/поверх загрузчика.
Команды только собирают, не прошивают. Полярность и силовую матрицу проверить на стенде.

## Остальные репозитории

| Репозиторий | Проект |
| --- | --- |
| arduino-sensor | `chem-reactor-теплица/arduino-sensors/arduino-sensors.ino` |
| arduino-actuator | `chem-reactor-теплица/arduino-actuator/arduino-actuator.ino` |
| tjc-commutator | `chem-reactor-теплица/`, FastAPI/консоль, HTTP 8001 |

## Интеграционный C++-тест

В отличие от сборки прошивок, этот тест требует оба Arduino-репозитория.
По умолчанию они клонированы рядом с tjc-sketch; для другого размещения задайте
--sensor-repo и --actuator-repo. Из этой папки:

```powershell
py -3 scripts/test_firmware.py --cxx g++ `
  --bridge-out build/tests/firmware-chain.exe
```

Проверяются три копии протокола, хаб/шлюзы/Arduino API, концевики/замки,
все уровни реле, пауза/wrap, CRC и таймауты. GPIO подменены, C++ API настоящие.
Нативные тесты Arduino GPIO лежат в соответствующих Arduino-репозиториях.
Bridge указывается через CHEM_FIRMWARE_CHAIN в HTTP-тестах tjc-commutator.
Runtime и лицензии: firmware/runtime/UPSTREAM.md, firmware/runtime-lmp8/UPSTREAM.md.

## Кнопочное открытие дверей

Хаб опрашивает счётчики кнопок 3/4 на узле 15 между внешними запросами
и передаёт команду открытия 0x44 на lmp8 (16). Локальный таймер закрывает
выход через 2 секунды без компьютера. На старте замки заблокированы.
Концевики idx=0/1 остаются на физических пинах кнопочницы 1/2.
Описание [автономного управления](../docs/DOOR-CONTROL.md) включает
новые внутренние команды 0x27/0x44, приоритеты и стендовую проверку.
