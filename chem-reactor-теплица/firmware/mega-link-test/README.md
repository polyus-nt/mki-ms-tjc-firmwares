# Arduino-шлюз → Mega: байт раз в секунду

Отдельная тестовая прошивка **tjc-ms1-ard-a1**, без SimpleBus и центрального хаба.
В пару загрузить `mega_link_test.ino` из репозитория arduino-actuator:
`chem-reactor-теплица/chemical-reactor/link-test/mega_link_test/`.

Из папки `tjc-sketch/chem-reactor-теплица`:

```powershell
py -3 scripts/build.py --target mega-link-test
```

При необходимости добавить `--toolchain-bin "C:\tools\arm-gnu\bin"`.
Результат `build/mega-link-test/sketch.bin`. Usercode по **0x08003800**,
загрузчик не стирать. В штатную цель `all` этот тест НЕ включён.

| Пин шлюза | Сигнал | Mega |
| --- | --- | --- |
| 1 | MISO | D40 |
| 2 | MOSI | D41 |
| 3 | CLK | D42 |
| 4 | CS | D43 |
| 5 | IRQ | D44 |
| GND | Земля | GND |

Шлюз ставит A5 в очередь раз в 1000 мс и выставляет IRQ LOW.
Mega опрашивает IRQ и формирует такты Austere. Встроенный LED Mega меняет
состояние при приёме, Serial 115200 показывает `byte=0xA5`.
Если Mega отсутствует, больше одного ожидающего байта не копится.
В отладчике `queued` — число постановок в очередь (не число доставок),
`skipped` — периоды с уже ожидающим байтом.

Полная инструкция, Mega-скетч и проверки — в
`arduino-actuator/chem-reactor-теплица/chemical-reactor/link-test/README.md`.
После проверки вернуть штатные прошивки обеих плат.
