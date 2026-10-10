#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
#include "UART.hpp"
#include "SimpleBus.hpp"
#include "LED.h"
#include "Lmp8Api.h"

LED locks(tjc_lmp8::kLockPin);
LED relay[6] = {LED(2), LED(3), LED(4), LED(5), LED(6), LED(7)};
SimpleBus bus;
struct Outputs {
  void relays(uint8_t mask) {
    // LED.on(100) означает HIGH. Реле active-low, поэтому инверсия.
    for (uint8_t i = 0; i < 6; ++i) {
      if (mask & (1u << i)) relay[i].off(); else relay[i].on();
    }
    LEDController::update();
  }
  void lock(bool engaged) {
    // Один выход для обоих замков; idx=0, раздельного управления нет.
    const bool high = tjc_lmp8::kLockActiveLow ? !engaged : engaged;
    if (high) locks.on(); else locks.off();
    LEDController::update();
  }
};
int main() {
  using namespace hardware_api;
  // В подготовленном runtime init начинает с HIGH для active-low выходов.
  // Исходные upstream файлы сохранены отдельно и не модифицированы.
  LEDController::init();
  bus.setAddress(tjc_lmp8::kBusAddress); detail::hal::initUartWrapper();
  Outputs outputs; tjc_lmp8::Lmp8Api<Outputs> api(outputs);
  StreamParser parser; ReplyBuffer tx;
  uint32_t lastSent = 0; bool sent = false;
  for (;;) {
    const uint32_t now = millis(); parser.tick(now); api.tick(now);
    if (detail::simpleBusHelpers::Api::takeOverflow()) parser.reset();
    while (bus.packetReceived()) {
      Frame request;
      if (parser.feed(bus.lastData, millis(), request) && !tx.pending()) api.receive(request, millis());
    }
    if (!tx.pending()) { Frame reply; if (api.takeReply(reply)) tx.enqueue(reply); }
    if (tx.pending() && (!sent || static_cast<uint32_t>(millis() - lastSent) >= 2)) {
      bus.sendPacket(tjc_lmp8::kHubAddress, tx.nextByte()); tx.accepted();
      lastSent = millis(); sent = true;
    }
  }
}
