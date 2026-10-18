#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
#include "Pins.hpp"
#include "ArduinoAdapter.hpp"
#include "Config.h"

// Только низкоуровневый Austere link; SimpleBus/хаб/Hardware API здесь не нужны.
// Полезны в отладчике. queued — принятые очередью байты, не подтверждение Mega.
volatile uint32_t queued=0,skipped=0;

int main() {
  arduino_link_init();
  uint32_t last=millis();
  for(;;) {
    uint32_t now=millis();
    if(static_cast<uint32_t>(now-last)<mega_link_test::periodMs) continue;
    last=now;
    const uint32_t saved=__get_PRIMASK();
    __disable_irq();
    // Не накапливаем очередь без Mega: максимум один ожидающий байт.
    // Он будет выдан, когда Mega увидит IRQ=LOW и сформирует девять тактов.
    if(sm.tx_head==sm.tx_tail && arduino_link_send_byte(mega_link_test::testByte)) ++queued;
    else ++skipped;
    __set_PRIMASK(saved);
  }
}
