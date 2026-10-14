#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
#include "UART.hpp"
#include "SimpleBus.hpp"
#include "LED.h"
#include "LmpApi.h"
#include "Outputs.h"

LED locks[2] = {LED(tjc_lmp::kLockPins[0]), LED(tjc_lmp::kLockPins[1])};
LED vacuumOutput(tjc_lmp::kVacuumPin);
SimpleBus bus;
int main() {
  using namespace hardware_api;
  // Стартовые уровни выставлены до GPIO output-mode: замки заблокированы.
  // Исходные upstream файлы сохранены отдельно и не модифицированы.
  // Компоненты LED уже предзагрузили уровни до GPIO output-mode.
  bus.setAddress(tjc_lmp::kBusAddress); detail::hal::initUartWrapper();
  tjc_lmp::Outputs<LED> outputs(locks, vacuumOutput);
  tjc_lmp::LmpApi<tjc_lmp::Outputs<LED>> api(outputs);
  StreamParser parser; ReplyBuffer tx;
  for (;;) {
    const uint32_t now = millis(); parser.tick(now); api.tick(now);
    if (detail::simpleBusHelpers::Api::takeOverflow()) parser.reset();
    while (bus.packetReceived()) {
      Frame request;
      if (parser.feed(bus.lastData, millis(), request) && !tx.pending()) api.receive(request, millis());
    }
    if (!tx.pending()) { Frame reply; if (api.takeReply(reply)) tx.enqueue(reply); }
    if (tx.pending()) {
      bus.sendPacket(tjc_lmp::kHubAddress, tx.nextByte()); tx.accepted();
    }
  }
}
