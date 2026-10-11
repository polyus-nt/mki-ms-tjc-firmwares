#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
#include "UART.hpp"
#include "SimpleBus.hpp"
#include "LED.h"
#include "Lmp8Api.h"

LED locks[2] = {LED(tjc_lmp8::kLockPins[0]), LED(tjc_lmp8::kLockPins[1])};
LED vacuumOutput(tjc_lmp8::kVacuumPin);
#if 0  // Только будущий химреактор: не объявляем реле на пинах замков/пылесоса.
LED relay[6] = {LED(2), LED(3), LED(4), LED(5), LED(6), LED(7)};
#endif
SimpleBus bus;
struct Outputs {
#if 0 // Заготовка химреактора. В фаблаб пины 2/3 заняты замком и пылесосом.
  void relays(uint8_t mask) {
    // LED.on(100) означает HIGH. Реле active-low, поэтому инверсия.
    for (uint8_t i = 0; i < 6; ++i) {
      if (mask & (1u << i)) relay[i].off(); else relay[i].on();
    }
    LEDController::update();
  }
#endif
  void lock(uint8_t idx, bool engaged) {
    // Валидатор допускает только idx=0/1; каждый замок имеет отдельный LED-пин.
    const bool high = tjc_lmp8::kLockActiveLow ? !engaged : engaged;
    if (high) locks[idx].on(); else locks[idx].off();
    LEDController::update();
  }
  void vacuum(bool enabled) {
    const bool high = tjc_lmp8::kVacuumActiveLow ? !enabled : enabled;
    if (high) vacuumOutput.on(); else vacuumOutput.off();
    LEDController::update();
  }
};
int main() {
  using namespace hardware_api;
  // Стартовые уровни выставлены до GPIO output-mode: замки заблокированы.
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
