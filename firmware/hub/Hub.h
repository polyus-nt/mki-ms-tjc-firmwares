#pragma once
#include "../common/HardwareProtocol.h"

namespace tjc_hub {
// Один UART/полудуплексная шина: источник не передаётся в SimpleBus.
// Различаем запрос компьютера и ответ шлюза только после проверки полного кадра.
class Hub {
 public:
  enum State { Recovering, Idle, Forwarding, Waiting, Responding };
  static const uint32_t kTransactionMs = 3000;
  static const uint32_t kQuietMs = 250;
  static const uint32_t kRecoveryMaxMs = 1000;
  explicit Hub(uint32_t now = 0)
      : state_(Recovering), started_(now), quiet_(now), recovery_(now),
        request_pending_(false), response_pending_(false), busy_pending_(false),
        recover_after_(false), cancel_pending_(true) {}

  void tick(uint32_t now) {
    parser_.tick(now);
    if ((state_ == Forwarding || state_ == Waiting) &&
        elapsed(now, started_) >= kTransactionMs)
      fail(hardware_api::DEVICE_TIMEOUT, now);
    if (state_ == Recovering &&
        (elapsed(now, quiet_) >= kQuietMs ||
         elapsed(now, recovery_) >= kRecoveryMaxMs)) {
      // Шум не должен навсегда запрещать команды. Это НЕ session/epoch-защита.
      state_ = Idle;
      parser_.reset();
    }
  }

  void receiveBus(uint8_t byte, uint32_t now) {
    tick(now);
    if (state_ == Recovering) { quiet_ = now; return; }
    hardware_api::Frame frame;
    if (!parser_.feed(byte, now, frame)) return;
    if (frame.kind == hardware_api::kResponse) {
      if (state_ != Waiting || !hardware_api::sameRequest(request_, frame)) return;
      if (frame.status > hardware_api::TRANSPORT_OVERFLOW ||
          frame.status == hardware_api::REQUEST_ID_CONFLICT ||
          (frame.status != hardware_api::OK &&
           !hardware_api::zeros(frame.data, sizeof(frame.data)))) return;
      answer(frame, false);
      return;
    }
    if (state_ != Idle) {
      if (!busy_pending_) {
        busy_ = hardware_api::response(frame, hardware_api::BUSY);
        busy_pending_ = true;
      }
      return;
    }
    request_ = frame;
    const hardware_api::Status status = hardware_api::validateRequest(frame, frame.node);
    if (status != hardware_api::OK) {
      answer(hardware_api::response(frame, status), false);
      return;
    }
    started_ = now;
    request_pending_ = true;
    state_ = Forwarding;
  }

  bool takeGatewayRequest(hardware_api::Frame& frame) {
    if (state_ != Forwarding || !request_pending_) return false;
    frame = request_; request_pending_ = false;
    return true;
  }
  void gatewayRequestSent(uint32_t now) {
    tick(now);
    if (state_ == Forwarding && !request_pending_) state_ = Waiting;
  }
  bool takeHostResponse(hardware_api::Frame& frame) {
    if (state_ != Responding || !response_pending_) return false;
    frame = response_; response_pending_ = false;
    return true;
  }
  void hostResponseSent(uint32_t now) {
    if (state_ != Responding || response_pending_) return;
    if (busy_pending_) {
      response_ = busy_; busy_pending_ = false; response_pending_ = true;
      return;
    }
    parser_.reset();
    if (recover_after_) beginRecovery(now);
    else state_ = Idle;
  }
  void overflow(uint32_t now) {
    parser_.reset(); cancel_pending_ = true;
    if (state_ == Forwarding || state_ == Waiting)
      fail(hardware_api::TRANSPORT_OVERFLOW, now);
    else if (state_ == Responding) recover_after_ = true;
    else beginRecovery(now);
  }
  bool takeCancelTransmission() {
    const bool result = cancel_pending_; cancel_pending_ = false; return result;
  }
  State state() const { return state_; }
 private:
  static uint32_t elapsed(uint32_t now, uint32_t before) { return now - before; }
  void beginRecovery(uint32_t now) {
    state_ = Recovering; quiet_ = recovery_ = now;
    parser_.reset();
  }
  void answer(const hardware_api::Frame& frame, bool recover) {
    response_ = frame; response_pending_ = true; request_pending_ = false;
    recover_after_ = recover; state_ = Responding; parser_.reset();
  }
  void fail(uint8_t status, uint32_t) {
    cancel_pending_ = true;
    answer(hardware_api::response(request_, status), true);
  }
  State state_;
  uint32_t started_, quiet_, recovery_;
  bool request_pending_, response_pending_, busy_pending_, recover_after_, cancel_pending_;
  hardware_api::Frame request_, response_, busy_;
  hardware_api::StreamParser parser_;
};
}  // namespace tjc_hub
