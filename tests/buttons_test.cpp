#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/buttons/ButtonsApi.h"

// Проверяем сам неизменённый Button.hpp на модельных GPIO/часах, не подменяя
// его обработку дребезга своей реализацией.
struct GPIO_TypeDef { uint32_t PUPDR, MODER, IDR; };
struct FakeRCC { uint32_t IOPENR; };
static GPIO_TypeDef portA = {}, portB = {};
static FakeRCC rcc = {};
#define GPIOA (&portA)
#define GPIOB (&portB)
#define RCC (&rcc)
#define RCC_IOPENR_GPIOAEN 1
#define RCC_IOPENR_GPIOBEN 2
#define GPIO_PUPDR_PUPD0_Pos 0
#define GPIO_MODER_MODE0_Pos 0
#define INLINE__
static uint32_t now = 0;
static uint32_t millis() { return now; }
#include "../firmware/runtime/Button.hpp"

using namespace hardware_api;
static Frame request(uint8_t idx = 0) {
  Frame req = {};
  req.version = kVersion; req.kind = kRequest; req.node = kButtonsNode;
  req.command = kGetDoor; req.idx = idx; req.tx_id = 4321;
  req.session_id = 0x0102030405060708ULL;
  return req;
}

int main() {
  Button first(tjc_buttons::kDoorPins[0]), second(tjc_buttons::kDoorPins[1]);
  assert(btns[0].port == GPIOA && btns[0].num == 1);
  assert(btns[1].port == GPIOA && btns[1].num == 8);
  portA.IDR = (1u << 1) | (1u << 8); // Разомкнуто: подтянуты вверх.
  first.scan(); second.scan();
  now = 51; first.scan(); second.scan();
  assert(!first.isPressed() && !second.isPressed());
  // Дребезг и две независимые линии: LOW на первом, второй остаётся HIGH.
  portA.IDR &= ~(1u << 1);
  now = 52; first.scan(); second.scan();
  now = 90; first.scan(); assert(!first.isPressed());
  portA.IDR |= (1u << 1); now = 91; first.scan();
  portA.IDR &= ~(1u << 1); now = 92; first.scan();
  now = 142; first.scan(); assert(!first.isPressed());
  now = 143; first.scan(); second.scan();
  assert(first.isPressed() && !second.isPressed());
  tjc_buttons::ButtonsApi api;
  assert(api.execute(request()).status == SENSOR_UNAVAILABLE);
  api.sample(first.isPressed(), second.isPressed(), now);
  Frame reply = api.execute(request());
  assert(reply.status == OK && reply.data[0] == 1 && zeros(reply.data + 1, 7));
  assert(sameRequest(request(), reply));
  reply = api.execute(request(1));
  assert(reply.status == OK && reply.data[0] == 0 && zeros(reply.data + 1, 7));
  // Независимое изменение второго входа/возврат первого.
  portA.IDR = (1u << 1); now = 144; first.scan(); second.scan();
  now = 195; first.scan(); second.scan();
  api.sample(first.isPressed(), second.isPressed(), now);
  assert(api.execute(request()).data[0] == 0);
  assert(api.execute(request(1)).data[0] == 1);
  Frame invalid = request(2);
  reply = api.execute(invalid); assert(reply.status == INVALID_INDEX && zeros(reply.data,8));
  invalid = request(); invalid.data[7] = 1;
  assert(api.execute(invalid).status == INVALID_ARGUMENT);
  invalid = request(); invalid.node = kSensorsNode;
  assert(api.execute(invalid).status == INVALID_NODE);
  invalid = request(); invalid.command = kGetTemp;
  assert(api.execute(invalid).status == INVALID_NODE);
  invalid = request(); invalid.command = 0xff;
  assert(api.execute(invalid).status == UNKNOWN_COMMAND);
  invalid = request(); invalid.version = 1;
  assert(api.execute(invalid).status == UNSUPPORTED_VERSION);
  invalid = request(); invalid.kind = kResponse;
  assert(api.execute(invalid).status == INVALID_ARGUMENT);
  // Кадр на полном wire-path сохраняет session/tx и boolean payload.
  uint8_t wire[kFrameSize]; encode(api.execute(request(1)), wire);
  StreamParser parser; Frame decoded = {};
  for (uint8_t i = 0; i < kFrameSize; ++i)
    assert(parser.feed(wire[i], now + i, decoded) == (i + 1 == kFrameSize));
  assert(sameRequest(request(1),decoded) && decoded.data[0] == 1);
  wire[10] ^= 1;
  for (uint8_t i = 0; i < kFrameSize; ++i) assert(!parser.feed(wire[i], now+i,decoded));
  // Таймер первоначального debounce выдерживает переполнение millis().
  tjc_buttons::ButtonsApi wrapping(0xfffffff0u);
  wrapping.sample(true,false,0x22u);
  assert(wrapping.execute(request()).status == SENSOR_UNAVAILABLE);
  wrapping.sample(true,false,0x23u);
  assert(wrapping.execute(request()).status == OK);
  // Реальный Button(3/4): удержание при старте, дребезг, повторное нажатие.
  now=1000;
  Button open0(tjc_buttons::kOpenPins[0]), open1(tjc_buttons::kOpenPins[1]);
  assert(btns[2].port==GPIOA && btns[2].num==11);
  assert(btns[3].port==GPIOA && btns[3].num==12);
  portA.IDR=(1u<<12); // Button 3 зажат при включении.
  open0.scan();open1.scan();
  tjc_buttons::ButtonsApi keys(now);
  Frame snapshot=request();snapshot.command=kGetOpenButtons;
  now=1051;open0.scan();open1.scan();
  keys.sample(false,false,now);keys.sampleOpen(open0.isPressed(),open1.isPressed(),now);
  assert(readU32(keys.execute(snapshot).data)==0);
  now=2000;open0.scan();keys.sampleOpen(open0.isPressed(),open1.isPressed(),now);
  assert(readU32(keys.execute(snapshot).data)==0); // удержание не событие.
  portA.IDR|=(1u<<11);now=2001;open0.scan();
  now=2052;open0.scan();keys.sampleOpen(open0.isPressed(),open1.isPressed(),now);
  portA.IDR&=~(1u<<11);now=2053;open0.scan();
  now=2090;open0.scan();keys.sampleOpen(open0.isPressed(),open1.isPressed(),now);
  assert(readU32(keys.execute(snapshot).data)==0);
  now=2104;open0.scan();keys.sampleOpen(open0.isPressed(),open1.isPressed(),now);
  assert(readU32(keys.execute(snapshot).data)==1);
  portA.IDR&=~(1u<<12);now=2105;open1.scan();
  now=2156;open1.scan();keys.sampleOpen(open0.isPressed(),open1.isPressed(),now);
  assert(readU32(keys.execute(snapshot).data+4)==1);
  puts("buttons tests passed");
}
