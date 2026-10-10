#pragma once
#include "../common/HardwareProtocol.h"

namespace tjc_hub {
// Один UART/полудуплексная шина: источник не передаётся в SimpleBus.
// Различаем запрос компьютера и ответ шлюза только после проверки полного кадра.
class Hub {
 public:
  enum State { Recovering, Idle, Forwarding, Waiting, Responding };
  static const uint32_t kTransactionMs = 3000;
  // Пять обменов GET/GET/OFF/OFF/ON требуют ~3.4 с при 9600 бод.
  // 4 с + кадры PC↔хаб укладываются в стандартный host timeout 5 с.
  static const uint32_t kModeTransactionMs = 4000;
  static const uint32_t kQuietMs = 250;
  static const uint32_t kRecoveryMaxMs = 1000;
  explicit Hub(uint32_t now = 0)
      : state_(Recovering), started_(now), quiet_(now), recovery_(now),
        request_pending_(false), response_pending_(false), busy_pending_(false),
        recover_after_(false), cancel_pending_(true), mode_stage_(0) {}

  void tick(uint32_t now) {
    parser_.tick(now);
    if ((state_ == Forwarding || state_ == Waiting) &&
        elapsed(now, started_) >= (mode_stage_ ? kModeTransactionMs : kTransactionMs))
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
      if (state_ != Waiting || !hardware_api::sameRequest(downstream_, frame)) return;
      if (frame.status > hardware_api::TRANSPORT_OVERFLOW ||
          frame.status == hardware_api::REQUEST_ID_CONFLICT ||
          (frame.status != hardware_api::OK &&
           !hardware_api::zeros(frame.data, sizeof(frame.data)))) return;
      if (mode_stage_) { modeReply(frame); return; }
      hardware_api::Frame restored = frame;
      restored.node = request_.node; restored.idx = request_.idx;
      answer(restored, false);
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
    mode_stage_ = 0;
    downstream_ = request_;
    if (frame.node == hardware_api::kActuatorNode &&
        (frame.command == hardware_api::kGetMode || frame.command == hardware_api::kSetMode)) {
      mode_stage_ = 1;
      modeRequest(hardware_api::kActuatorNode, hardware_api::kGetMode, 0xff, 0);
    } else {
      if (frame.node == hardware_api::kActuatorNode && frame.idx >= 4) {
        downstream_.node = hardware_api::kActuator2Node;
        downstream_.idx = frame.idx - 4;
      }
      forward();
    }
  }

  bool takeGatewayRequest(hardware_api::Frame& frame) {
    if (state_ != Forwarding || !request_pending_) return false;
    frame = downstream_; request_pending_ = false;
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
  void forward() {
    parser_.reset(); request_pending_ = true; state_ = Forwarding;
  }
  // Внутренние стадии имеют разные session_id: поздний ответ на сброс не может
  // подтвердить последующее включение с теми же node/command/idx/tx_id.
  void modeRequest(uint8_t node, uint8_t command, uint8_t idx, uint8_t mode) {
    downstream_ = request_; downstream_.node = node;
    downstream_.command = command; downstream_.idx = idx;
    downstream_.session_id ^= static_cast<uint64_t>(mode_stage_) << 56;
    memset(downstream_.data, 0, sizeof(downstream_.data));
    downstream_.data[0] = mode; forward();
  }
  bool validMode(const hardware_api::Frame& frame) const {
    return frame.data[0] <= 2 && hardware_api::zeros(frame.data + 2, 6) &&
        (frame.data[0] == 0 ? frame.data[1] == 0xff :
         frame.data[1] < (frame.node == hardware_api::kActuatorNode ? 4 : 2));
  }
  void modeResult(uint8_t mode, uint8_t idx) {
    hardware_api::Frame result = hardware_api::response(request_);
    result.data[0] = mode; result.data[1] = idx;
    answer(result, false);
  }
  void modeReply(const hardware_api::Frame& frame) {
    using namespace hardware_api;
    if (frame.status != OK) { answer(response(request_, frame.status), false); return; }
    if (!validMode(frame)) return; // Ждём достоверный ответ либо общий таймаут.
    if (mode_stage_ == 1) {
      first_mode_ = frame.data[0]; first_idx_ = frame.data[1];
      mode_stage_ = 2; modeRequest(kActuator2Node, kGetMode, 0xff, 0); return;
    }
    if (mode_stage_ == 2) {
      const bool conflict = first_mode_ != 0 && frame.data[0] != 0;
      const uint8_t mode = first_mode_ ? first_mode_ : frame.data[0];
      const uint8_t idx = first_mode_ ? first_idx_ :
          (frame.data[0] ? static_cast<uint8_t>(frame.data[1] + 4) : 0xff);
      if (request_.command == kGetMode) {
        if (conflict) answer(response(request_, INTERNAL_ERROR), false);
        else modeResult(mode, idx);
        return;
      }
      // Повтор того же сценария не меняет PWM и не перезапускает его таймер.
      if (!conflict && mode == request_.data[0] &&
          (mode == 0 || idx == request_.idx)) { modeResult(mode, idx); return; }
      mode_stage_ = 3; modeRequest(kActuatorNode, kSetMode, 0, 0); return;
    }
    if (mode_stage_ == 3 || mode_stage_ == 4) {
      if (frame.data[0] != 0 || frame.data[1] != 0xff) {
        answer(response(request_, INTERNAL_ERROR), false); return;
      }
      if (mode_stage_ == 3) {
        mode_stage_ = 4; modeRequest(kActuator2Node, kSetMode, 0, 0); return;
      }
      if (request_.data[0] == 0) { modeResult(0, 0xff); return; }
      mode_stage_ = 5;
      modeRequest(request_.idx < 4 ? kActuatorNode : kActuator2Node,
                  kSetMode, request_.idx < 4 ? request_.idx : request_.idx - 4,
                  request_.data[0]); return;
    }
    if (frame.data[0] != request_.data[0] || frame.data[1] != downstream_.idx) {
      answer(response(request_, INTERNAL_ERROR), false); return;
    }
    modeResult(frame.data[0], request_.idx);
  }
  void fail(uint8_t status, uint32_t) {
    cancel_pending_ = true;
    answer(hardware_api::response(request_, status), true);
  }
  State state_;
  uint32_t started_, quiet_, recovery_;
  bool request_pending_, response_pending_, busy_pending_, recover_after_, cancel_pending_;
  hardware_api::Frame request_, downstream_, response_, busy_;
  uint8_t mode_stage_, first_mode_, first_idx_;
  hardware_api::StreamParser parser_;
};
}  // namespace tjc_hub
