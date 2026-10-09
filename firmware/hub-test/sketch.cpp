#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
#include "UART.hpp"
#include "SimpleBus.hpp"

// Тест связи для центральной платы main-a4: никаких команд API и шлюзов.
// На любой полезный байт пакета SimpleBus, адресованного хабу 12,
// отвечаем компьютеру 99 одним фиксированным байтом 0xAA.
// Обрамление SimpleBus сохраняется: CR, два ASCII HEX адреса, байт, LF.
SimpleBus bus;

int main() {
  bus.setAddress(12);
  detail::hal::initUartWrapper();
  for (;;) {
    if (bus.packetReceived()) {
      // Компьютер должен дождаться ответа перед отправкой следующего пакета.
      // Пауза даёт передатчику компьютера время освободить полудуплексную шину.
      delay(2);
      bus.sendPacket(99, 0xAA);
    }
  }
}
