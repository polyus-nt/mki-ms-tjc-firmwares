#include <stdint.h>
#include "stm32g030xx.h"
#include "system.c"
#undef INLINE__
#include "UART.hpp"
#include "SimpleBus.hpp"
#include "Hub.h"
#include "HostArbiter.h"

// main-a4: PC и платы используют ОДНУ шину USART1, PB6/PB7, PA0 ReDe.
// Здесь нет второго UART и нет автоматически назначенного адреса восьмилампочницы.
SimpleBus bus;
tjc_hub::HostArbiter<tjc_hub::Hub> hub(1);

int main() {
  bus.setAddress(12);
  detail::hal::initUartWrapper();
  hub.setSessionSeed(*reinterpret_cast<volatile uint32_t*>(UID_BASE) ^ SysTick->VAL);
  uint8_t outgoing[hardware_api::kFrameSize];
  uint8_t position = hardware_api::kFrameSize, address = 0;
  bool to_host = false;
  uint32_t last_end = 0;
  for (;;) {
    if (detail::simpleBusHelpers::Api::takeOverflow()) hub.overflow(millis());
    if (hub.takeCancelTransmission() && !to_host)
      position = hardware_api::kFrameSize;
    while (bus.packetReceived()) hub.receiveBus(bus.lastData, millis());
    // Сначала учесть входящие байты; не начинать опрос поверх начала кадра PC.
    hub.tick(millis());
    if (hub.takeCancelTransmission() && !to_host)
      position = hardware_api::kFrameSize;
    if (position == hardware_api::kFrameSize) {
      hardware_api::Frame frame;
      if (hub.takeHostResponse(frame)) {
        address = 99; to_host = true;
        hardware_api::encode(frame, outgoing); position = 0;
      } else if (hub.takeGatewayRequest(frame)) {
        address = frame.node; to_host = false;
        hardware_api::encode(frame, outgoing); position = 0;
      }
    }
    if (position < hardware_api::kFrameSize) {
      bus.sendPacket(address, outgoing[position++]);
      last_end = millis();
      if (position == hardware_api::kFrameSize) {
        if (to_host) hub.hostResponseSent(last_end);
        else hub.gatewayRequestSent(last_end);
      }
    }
  }
}
