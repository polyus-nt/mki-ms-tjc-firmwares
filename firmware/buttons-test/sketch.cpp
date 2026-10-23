#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
#include "UART.hpp"
#include "SimpleBus.hpp"

// Только проверка физического обмена: ПК напрямую обращается к кнопочнице.
// Никакого API, опроса кнопок, пересылки через хаб и автономной передачи.
// 9600 задаётся сборщиком только в build/buttons-test/platform/BusConfig.h.
SimpleBus bus;
int main() {
  bus.setAddress(15);
  detail::hal::initUartWrapper();
  for (;;) if(bus.packetReceived()) {
    // Как в hub-test: дать адаптеру освободить полудуплексную шину.
    delay(100);
    bus.sendPacket(99,0xBB);
  }
}
