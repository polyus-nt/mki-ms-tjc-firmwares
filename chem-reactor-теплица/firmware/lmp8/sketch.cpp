#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
#include "UART.hpp"
#include "SimpleBus.hpp"
#include "LED.h"
#include "Lmp8Api.h"
#include "Outputs.h"

LED lockOutput(tjc_lmp8::kLockPin);
LED relay[6] = {LED(tjc_lmp8::kRelayPins[0]), LED(tjc_lmp8::kRelayPins[1]),
                LED(tjc_lmp8::kRelayPins[2]), LED(tjc_lmp8::kRelayPins[3]),
                LED(tjc_lmp8::kRelayPins[4]), LED(tjc_lmp8::kRelayPins[5])};
SimpleBus bus;
int main() {
  using namespace hardware_api;
  LEDController::init(); // Замок LOW, реле LOW: уровни загружены до MODER.
  bus.setAddress(tjc_lmp8::kBusAddress); detail::hal::initUartWrapper();
  tjc_lmp8::Outputs<LED> outputs(lockOutput, relay, LEDController::update);
  tjc_lmp8::Lmp8Api<tjc_lmp8::Outputs<LED>> api(outputs);
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
