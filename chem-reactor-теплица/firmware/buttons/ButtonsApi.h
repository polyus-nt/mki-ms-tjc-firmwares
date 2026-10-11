#pragma once
#include "BoardConfig.h"

namespace tjc_buttons {
// Концевики и счётчики нажатий. Кнопочница сама не передаёт события в шину:
// хаб читает счётчики, поэтому чтение не стирает событие и не создаёт коллизий.
class ButtonsApi {
 public:
  explicit ButtonsApi(uint32_t now = 0) : started_(now), ready_(false), openReady_(false) {
    states_[0] = states_[1] = false;
    presses_[0] = presses_[1] = 0; held_[0] = held_[1] = false;
  }
  // Состояния передаются из постоянно сканируемых Button::isPressed().
  void sample(bool first, bool second, uint32_t now) {
    states_[0] = first; states_[1] = second;
    ready_ = static_cast<uint32_t>(now - started_) > kDebounceMs;
  }
  void sampleOpen(bool first, bool second, uint32_t now) {
    const bool pressed[2] = {first, second};
    if (static_cast<uint32_t>(now-started_) <= kDebounceMs) return;
    for (uint8_t i=0;i<2;++i) {
      if (openReady_ && pressed[i] && !held_[i]) ++presses_[i];
      held_[i]=pressed[i];
    }
    // Нажатая при старте кнопка не открывает дверь: сначала её отпустить.
    openReady_=true;
  }
  hardware_api::Frame execute(const hardware_api::Frame& request) const {
    const hardware_api::Status status =
        hardware_api::validateRequest(request, kBusAddress);
    hardware_api::Frame reply = hardware_api::response(request, status);
    if (status != hardware_api::OK) return reply;
    if (!ready_) {
      reply.status = hardware_api::SENSOR_UNAVAILABLE;
      return reply;
    }
    if (request.command == hardware_api::kGetOpenButtons) {
      if (!openReady_) return hardware_api::response(request, hardware_api::SENSOR_UNAVAILABLE);
      hardware_api::writeU32(reply.data, presses_[0]);
      hardware_api::writeU32(reply.data+4, presses_[1]);
      return reply;
    }
    // Ответ get_door_state: один bool (0/1), затем семь нулевых байтов.
    reply.data[0] = (states_[request.idx] == kClosedWhenPressed[request.idx]) ? 1 : 0;
    return reply;
  }
 private:
  uint32_t started_;
  bool ready_, states_[2], openReady_, held_[2];
  uint32_t presses_[2];
};
}
