#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
// Только тестовая копия UART.hpp увеличивает этот счётчик в обработчике RX,
// ДО проверки обрамления и адреса SimpleBus. Исходный runtime не меняется.
namespace hub_test {
volatile uint32_t uart_rx_count = 0;
}
#include "UART.hpp"
#include "SimpleBus.hpp"
#include "LED.h"

// Тест связи для центральной платы main-a4: никаких команд API и шлюзов.
// На любой полезный байт пакета SimpleBus, адресованного хабу 12,
// отвечаем компьютеру 99 одним фиксированным байтом 0xAA.
// Обрамление SimpleBus сохраняется: CR, два ASCII HEX адреса, байт, LF.
SimpleBus bus;

int main() {
  bus.setAddress(12);
  detail::hal::initUartWrapper();
  // LED.h main-a4: пользовательский пин 1 -> PA5, активный низкий уровень.
  // Используем исходный класс, а не дублируем управление GPIO.
  // Конструктор LED запускает PWM-таймер: не допускаем его прерывание,
  // пока исходный initPWM() ещё заполняет таблицу каналов.
  const uint32_t saved_irq = __get_PRIMASK();
  __disable_irq();
  LED heartbeat(1);
  LED uart_received(2);   // LED.h main-a4: PA6.
  LED packet_received(3); // LED.h main-a4: PA7.
  heartbeat.on();
  __set_PRIMASK(saved_irq);
  uint32_t last_blink = millis();
  bool saw_uart = false, saw_packet = false;
  for (;;) {
    const uint32_t now = millis();
    // Неблокирующее мигание: приём SimpleBus продолжается между переключениями.
    // Безопасно при переполнении счётчика millis().
    if (static_cast<uint32_t>(now - last_blink) >= 500U) {
      heartbeat.toggle();
      last_blink = now;
    }
    // Защёлка до перезапуска: даже единичный байт будет заметен человеку.
    if (!saw_uart && hub_test::uart_rx_count != 0) {
      uart_received.on();
      saw_uart = true;
    }
    if (bus.packetReceived()) {
      // Пакет прошёл проверку CR/LF и адреса 12, ещё ДО отправки ответа.
      if (!saw_packet) {
        packet_received.on();
        saw_packet = true;
      }
      // Компьютер должен дождаться ответа перед отправкой следующего пакета.
      // Пауза даёт передатчику компьютера время освободить полудуплексную шину.
      delay(2);
      bus.sendPacket(99, 0xAA);
    }
  }
}
