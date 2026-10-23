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
// Только тестовая сборка: 9600 бод, кнопочница-test отвечает независимо на 15.
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
  LED tx_pending(3); // LED.h main-a4: PA7; пакет принят, отправка ещё не завершена.
  heartbeat.on();
  __set_PRIMASK(saved_irq);
  uint32_t last_blink = millis();
  bool saw_uart = false;
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
      tx_pending.on();
      // Компьютер должен дождаться ответа перед отправкой следующего пакета.
      // Только для диагностики: короткая видимая вспышка перед передачей.
      // Пауза также даёт компьютеру время освободить полудуплексную шину.
      delay(100);
      bus.sendPacket(99, 0xAA);
      // До этой строки дойдём лишь после возврата sendPacket(): UART дождался
      // завершения передачи (TC), а драйвер RS485 переведён обратно в приём.
      // Это НЕ доказывает, что ответ физически дошёл до USB-адаптера.
      tx_pending.off();
    }
  }
}
