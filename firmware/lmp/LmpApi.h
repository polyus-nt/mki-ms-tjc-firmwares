#pragma once
#include "BoardConfig.h"
namespace tjc_lmp {
// Hardware задаёт только физические состояния. Ни температурных правил,
// ни Arduino, ни PID здесь нет. Фаблаб: два независимых замка и пылесос.
// Релейный вентилятор реализован отдельно в химреакторе; здесь его нет.
template<class Hardware> class LmpApi {
 public:
  explicit LmpApi(Hardware& hw) : hw_(hw), vacuum_(false),
      replyReady_(false), cached_(false) {
    lock_[0] = lock_[1] = true;
    timed_[0]=timed_[1]=false; opened_[0]=opened_[1]=0;
    hw_.lock(0, true); hw_.lock(1, true); hw_.vacuum(false);
  }
  void receive(const hardware_api::Frame& request, uint32_t now) {
    using namespace hardware_api;
    if (request.kind != kRequest || replyReady_) return;
    const Status status = validateRequest(request, kBusAddress);
    if (status != OK) { setReply(response(request, status)); return; }
    if (request.command == kGetRelayLevel || request.command == kSetRelayLevel) {
      setReply(response(request, NOT_IMPLEMENTED));
      return; // Фаблаб не исполняет релейную матрицу химреактора.
    }
    if (cached_ && request.session_id == lastRequest_.session_id &&
        request.tx_id == lastRequest_.tx_id) {
      setReply(equal(request, lastRequest_) ? lastReply_ :
               response(request, REQUEST_ID_CONFLICT));
      return;
    }
    if (request.command == kSetDoorLock) {
      timed_[request.idx]=false; // Явная команда отменяет импульс открытия.
      lock_[request.idx] = request.data[0] != 0; hw_.lock(request.idx, lock_[request.idx]);
    }
    if (request.command == kOpenDoorTimed) {
      lock_[request.idx]=false; timed_[request.idx]=true; opened_[request.idx]=now;
      hw_.lock(request.idx,false);
    }
    if (request.command == kSetVacuum) {
      vacuum_ = request.data[0] != 0; hw_.vacuum(vacuum_);
    }
    Frame reply = response(request);
    if (request.command == kGetDoorLock) reply.data[0] = lock_[request.idx] ? 1 : 0;
    if (request.command == kGetVacuum) reply.data[0] = vacuum_ ? 1 : 0;
    complete(request, reply);
  }
  void tick(uint32_t now) {
    for (uint8_t i=0;i<2;++i) {
      if (timed_[i] && static_cast<uint32_t>(now-opened_[i]) >= kOpenMs) {
        timed_[i]=false; lock_[i]=true; hw_.lock(i,true);
      }
    }
  }
  bool takeReply(hardware_api::Frame& result) {
    if (!replyReady_) return false;
    result = reply_; replyReady_ = false; return true;
  }
 private:
  static bool equal(const hardware_api::Frame& a, const hardware_api::Frame& b) {
    uint8_t aa[hardware_api::kFrameSize], bb[hardware_api::kFrameSize];
    hardware_api::encode(a, aa); hardware_api::encode(b, bb);
    return memcmp(aa, bb, sizeof aa) == 0;
  }
  void setReply(const hardware_api::Frame& r) { reply_ = r; replyReady_ = true; }
  void complete(const hardware_api::Frame& request, const hardware_api::Frame& reply) {
    lastRequest_ = request; lastReply_ = reply; cached_ = true; setReply(reply);
  }
  Hardware& hw_;
  bool lock_[2], vacuum_, replyReady_, cached_;
  uint32_t opened_[2];
  bool timed_[2];
  hardware_api::Frame reply_, lastRequest_, lastReply_;
};
}
