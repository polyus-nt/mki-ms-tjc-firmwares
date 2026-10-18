#pragma once
#include <stdint.h>

namespace mega_link_test {
static const uint32_t periodMs=1000;
static const uint8_t testByte=0xa5;
// Аппаратная карта шлюза задана штатным runtime arduino_link.c:
// 1 MISO=PA9; 2 MOSI=PC6; 3 CLK=PA10; 4 CS=PA11; 5 IRQ=PA12.
}
