# МС-ТЮК: рукописные прошивки аппаратного API

Актуальный код без GraphML:

- [Центральный хаб](docs/HUB-FIRMWARE.md): `firmware/hub/sketch.cpp`, плата
  `tjc-ms1-main-a4/1.0`, адрес 12.
- [Arduino-шлюзы](docs/GATEWAY-FIRMWARE.md): `firmware/gateway/sketch.cpp`,
  `tjc-ms1-ard-a1/1.0`, адреса 13 и 14.
- [Общий аппаратный протокол](../tjc-commutator/docs/HARDWARE-PROTOCOL.md).

## Проверка хаба и датчиков без сервера

Интерактивная read-only консоль для Windows/Linux/macOS. Для Windows нужен Python 3
и pyserial; для Linux/macOS достаточно стандартной библиотеки Python.

### Windows (PowerShell или CMD)

Из каталога `tjc-sketch`:

```powershell
py -3 -m pip install pyserial
py -3 scripts/sensor_console.py --list-ports
py -3 scripts/sensor_console.py COM3
```

Замените COM3 на порт адаптера МС-ТЮК из списка или диспетчера устройств.
Поддерживаются и COM10/COM12 и выше. Для отображения кадров:

```powershell
py -3 scripts/sensor_console.py COM3 --hex
```

Если команда `py` отсутствует, используйте `python` вместо `py -3` (Python 3
должен быть в PATH). Библиотека устанавливается в тот же интерпретатор,
который запускает скрипт. Источник Windows-backend —
[pySerial](https://github.com/pyserial/pyserial/blob/master/serial/serialwin32.py).
Проверены mock-тесты этого backend; реальный COM-адаптер Windows пока не проверялся.

### Linux/macOS

```sh
python3 scripts/sensor_console.py --list-ports
python3 scripts/sensor_console.py /dev/ttyUSB0
```

Укажите **порт адаптера МС-ТЮК**, а не USB Arduino. Закройте commutator и другие
программы, работающие с тем же портом: на общей шине должен быть один инициатор.
Хаб, шлюз sensors и Arduino должны иметь совместимые прошивки v2;
на Arduino флаг TEST для этой проверки не требуется, лучше выключить.
Протокол/уровни адаптера должны соответствовать физической SimpleBus-линии.

### Команды и результаты (одинаковы на всех ОС)

В консоли:

```text
mstjuk> temp 0
mstjuk> pressure 1
mstjuk> voc 0
mstjuk> humidity 2
mstjuk> dust 1
mstjuk> help
mstjuk> exit
```

Температура/давление/влажность: idx=0..2; VOC/пыль: idx=0..1.
Скрипт сам создаёт CRC-кадр v2 и SimpleBus-пакеты хабу 12, получает ответ
компьютеру 99 через шлюз sensors 13, проверяет реквизиты/CRC/данные и печатает
значение, единицы, valid/fresh и age_ms. Для просмотра байтов добавьте `--hex`:

```sh
python3 scripts/sensor_console.py /dev/ttyUSB0 --hex
```

Пример вывода (не измерение стенда):

```text
Давление: 100653 Па
valid=1 fresh=1 age_ms=123
```

SENSOR_UNAVAILABLE означает отсутствие валидного измерения (для VOC возможен
прогрев около 45 с); DEVICE_TIMEOUT — нижний узел не ответил; HOST_TIMEOUT —
подходящий ответ не пришёл за 5 с. Потерянные запросы автоматически не повторяются.
При старте/после тайм-аута скрипт ждёт 1 с тишины; если за 3 с её нет, закрывается
с ошибкой. tx_id переиспользуется только после смены session_id.
Это прямой однопоточный тест, без MCP, SQLite и фонового опроса актуаторов.
Проверяет только чтения; команды PWM/режимов/замков намеренно отсутствуют.

Если Python не установлен, в Nix:

```sh
nix --extra-experimental-features 'nix-command flakes' shell nixpkgs#python3 \
  --command python3 scripts/sensor_console.py /dev/ttyUSB0
```

Автотесты консоли: `python3 tests/sensor_console_test.py` (включают виртуальный
serial-порт, но не заменяют стендовую проверку). Консоль не прошивает устройства.

## Сборка одной командой

Нужны только Python 3 и ARM GCC (`arm-none-eabi-g++`, `arm-none-eabi-objcopy`,
`arm-none-eabi-size`). Runtime МС-ТЮК уже находится в [firmware/runtime](firmware/runtime).
Отдельный lapki-compiler, скачивание исходников и COMPILER_ROOT больше не нужны.

### Windows

В PowerShell/CMD из каталога проекта:

```powershell
py -3 scripts/build.py
```

Если ARM GCC не в PATH, укажите каталог с `.exe` инструментов:

```powershell
py -3 scripts/build.py --toolchain-bin "C:\путь\к\ARM GCC\bin"
```

Отдельно: `--target hub` или `--target gateways`. Make на Windows не требуется.

### Linux/macOS

Из каталога `tjc-sketch`:

```sh
python3 scripts/build.py
# Либо при установленном Make:
make build
```

Отдельные цели: `make build-hub` и
`make build-gateways`; справка — `make help`. Все цели сборки используют
одинаковые параметры TOOLCHAIN_BIN и PYTHON.
Если ARM-инструменты не в PATH, добавьте `TOOLCHAIN_BIN=/path/to/arm/bin`.

Для Nix можно получить зависимости без системной установки:

```sh
nix --extra-experimental-features 'nix-command flakes' shell \
  nixpkgs#gnumake nixpkgs#python3 nixpkgs#gcc-arm-embedded \
  --command make build
```

Готовые `.bin`, `.elf` и `.map`:

- `build/hub/sketch.*` — центральный хаб;
- `build/gateways/sensors/sketch.*` — шлюз датчиков, адрес 13;
- `build/gateways/actuator/sketch.*` — шлюз актуаторов, адрес 14.

Команда только собирает, ничего не прошивает. Существующие Python-скрипты
`scripts/build_hub.py` и `scripts/build_gateways.py` остаются доступны напрямую.
Ревизия upstream закреплена, подробности и ограничения — в документации выше.
Источник, лицензии и локальные исправления описаны в
[firmware/runtime/UPSTREAM.md](firmware/runtime/UPSTREAM.md).
Проверка runtime/CRLF и сборочного входа: `python3 tests/build_runtime_test.py`.
Старые GraphML и документы ниже сохранены как исторические материалы; они не
описывают текущий v2 и не входят в сборку прошивок.

## Историческая реализация: фиксированный протокол охлаждения

> Новое направление: прошивки центральной МС-ТЮК платы и двух Arduino-шлюзов
> разрабатываются непосредственно на C++, вместо GraphML-диаграммы.
> Целевая архитектура — [аппаратный API](../tjc-commutator/docs/HARDWARE-API-ARCHITECTURE.md),
> сведения о сборке из platforms lapki-compiler — [TJC-FIRMWARE.md](../tjc-commutator/docs/TJC-FIRMWARE.md).
> Описание ниже относится к прежней реализации и не является текущим контрактом.

`Скетч.graphml` — Cyberiada-GraphML-модель трёх TJC-плат. Форматы сообщений,
коды команд и тайм-ауты определены в [`docs/protocol.md`](docs/protocol.md).
GraphML генерируется скриптом `scripts/generate_protocol_graphml.mjs`, поэтому
ручные изменения сгенерированного файла при следующем запуске будут потеряны.

## Узлы

| Автомат | Роль | SimpleBus |
| --- | --- | ---: |
| `MS1GateSensors` | Выполняет `POLL_SENSORS`, затем `READ_SENSORS`, и передаёт центральной плате полный позиционный ответ | `13` |
| `MS1MainProtocol` | Последовательно выполняет периодический сбор телеметрии и проксирует управляющие запросы ПК | `12` |
| `MS1GateActuator` | Прозрачно пересылает команды и ответы между SimpleBus и Arduino актуатора | `14` |
| ПК | Принимает телеметрию и `CONTROL_RESPONSE` | `99` |

Вспомогательный автомат `ArduinoUno1` сохранён отдельно и не участвует в
обмене охлаждения.

## Датчики

Центральная плата посылает узлу датчиков `0x01`. Узел датчиков:

1. отправляет Arduino `POLL_SENSORS=0x01` и ожидает `OK`;
2. отправляет `READ_SENSORS=0x04`;
3. принимает ровно 10 байтов успешного ответа либо один статус ошибки;
4. без изменения пересылает ответ центральной плате.

Успешный ответ содержит Hall, три концевика, две пары температуры, флаги
`valid/fresh` и финальный `OK`.

## Управляющий обмен

В состоянии idle центральная плата принимает `CONTROL_REQUEST=0x11`, затем код
команды и фиксированное число байтов payload. Поддерживаются все команды
`0x01`, `0x06`, `0x08`–`0x10`, перечисленные в `docs/protocol.md`.

Запрос пересылается узлу актуатора без внешнего `0x11`. Ответ читается строго
по ожидаемой длине: 21 байт для `GET_ACTUATOR_STATE`, 18 байтов для
`GET_CONTROL_CONFIG` и один статус для изменяющих команд. На ПК отправляется:

```text
CONTROL_RESPONSE=0x12, command, fixed response body
```

Неизвестная команда получает `CONTROL_RESPONSE`, исходный код и
`INVALID_ARGUMENT=0xE0`. Неполный запрос или ответ через две секунды
отбрасывается без синтетического статуса.

## Периодическая телеметрия

Когда управляющего обмена нет, центральная плата раз в две секунды:

1. читает датчики;
2. вычисляет `doorOpen` (`0`, если все три концевика равны `1`) и `doorStage`;
3. отправляет актуатору пятибайтовый `UPDATE_ENVIRONMENT`;
4. читает 21-байтовый `GET_ACTUATOR_STATE`;
5. формирует кадр `0x0C + 29 байтов payload + 0x0C`.

В текущей модели `doorStage` равен `0` для закрытой двери и `1` для открытой.
Если проекту потребуется более подробная шкала стадий, её семантику следует
сначала закрепить в `docs/protocol.md`.

Телеметрия и управляющий ответ не интерливятся. Новые управляющие запросы
обрабатываются только после возврата автомата в idle.

## Позиционность и `0x03`

Каждый принятый байт данных сначала сохраняется по текущему индексу. Значение
байта не участвует в определении конца сообщения. Поэтому RPM `0x03 0x03`,
байт температуры `0x03` и любые другие совпадения с `OK` передаются без замены.
`OK=0x03` проверяется только в заранее определённой финальной позиции.

## Генерация и проверка

Для генерации нужен Node.js 20 или новее:

```bash
node scripts/generate_protocol_graphml.mjs
```

Файл совместим с `GraphMlParser`/`GraphMlBuilder` из `lapki-client` и использует
только компоненты, объявленные платформами `tjc-ms1-main-a4-1.0.json` и
`tjc-ms1-ard-a1-1.0.json`: `SimpleBus`, `ArduinoAdapter`, `Timer`, `Counter`,
`CalcInt`, `Array` и `UserSignal`.

Перед прошивкой модель следует открыть в Lapki IDE, собрать для обеих TJC-плат
и проверить на стенде общие тестовые векторы из `docs/protocol.md`.
