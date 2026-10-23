#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
// Только тестовая копия UART.hpp увеличивает счётчик RXNE до разбора пакета.
namespace buttons_test {
volatile uint32_t uart_rx_count = 0;
}
#include "UART.hpp"
#include "SimpleBus.hpp"
#include "LED.h"

// Только проверка физического обмена: ПК напрямую обращается к кнопочнице.
// Никакого API, опроса кнопок, пересылки через хаб и автономной передачи.
// 9600 задаётся сборщиком только в build/buttons-test/platform/BusConfig.h.
SimpleBus bus;
int main() {
  bus.setAddress(15);
  detail::hal::initUartWrapper();
  // Исходный LED btn-a2: 1/2/3 -> PA5/PA6/PA7, active LOW.
  // Конструкторы запускают PWM; IRQ запрещены до заполнения таблицы каналов.
  const uint32_t saved_irq=__get_PRIMASK();
  __disable_irq();
  LED heartbeat(1);
  LED uart_received(2);
  LED tx_pending(3);
  heartbeat.on();
  __set_PRIMASK(saved_irq);
  uint32_t last_blink=millis();
  bool saw_uart=false;
  for (;;) {
    const uint32_t now=millis();
    // №1: живой основной цикл, переключение каждые 500 мс.
    if(static_cast<uint32_t>(now-last_blink)>=500U) {
      heartbeat.toggle(); last_blink=now;
    }
    // №2: защёлка первого UART RX, даже если адрес/обрамление неверны.
    if(!saw_uart && buttons_test::uart_rx_count!=0) {
      uart_received.on(); saw_uart=true;
    }
    if(bus.packetReceived()) {
      // №3: принят пакет на адрес 15, ответ ещё не завершён.
      tx_pending.on();
      delay(100); // Видимая вспышка + освобождение полудуплексной шины.
      bus.sendPacket(99,0xBB);
      // Это доказывает возврат sendPacket(), но не получение ответа на ПК.
      tx_pending.off();
    }
  }
}
