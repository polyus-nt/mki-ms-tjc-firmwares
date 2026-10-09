#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
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
  heartbeat.on();
  __set_PRIMASK(saved_irq);
  uint32_t last_blink = millis();
  for (;;) {
    const uint32_t now = millis();
    // Неблокирующее мигание: приём SimpleBus продолжается между переключениями.
    // Безопасно при переполнении счётчика millis().
    if (static_cast<uint32_t>(now - last_blink) >= 500U) {
      heartbeat.toggle();
      last_blink = now;
    }
    if (bus.packetReceived()) {
      // Компьютер должен дождаться ответа перед отправкой следующего пакета.
      // Пауза даёт передатчику компьютера время освободить полудуплексную шину.
      delay(2);
      bus.sendPacket(99, 0xAA);
    }
  }
}
