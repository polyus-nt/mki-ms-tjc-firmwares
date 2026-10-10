#pragma once
#include "BoardConfig.h"
namespace tjc_lmp8 {
// Hardware задаёт только физические состояния. Ни температурных правил,
// ни Arduino, ни PID здесь нет. Фаблаб: активен только общий выход замков.
// Релейный вентилятор относится к химреактору, его исполнение закомментировано.
template<class Hardware> class Lmp8Api {
 public:
  explicit Lmp8Api(Hardware& hw) : hw_(hw), level_(0), lock_(false),
      switching_(false), replyReady_(false), cached_(false), started_(0) {
    hw_.relays(0); hw_.lock(false);
  }
  void receive(const hardware_api::Frame& request, uint32_t now) {
    using namespace hardware_api;
    (void)now;
    if (request.kind != kRequest || replyReady_) return;
    const Status status = validateRequest(request, kBusAddress);
    if (status != OK) { setReply(response(request, status)); return; }
    if (request.command == kGetRelayLevel || request.command == kSetRelayLevel) {
      setReply(response(request, NOT_IMPLEMENTED));
      return; // Фаблаб не исполняет релейную матрицу химреактора.
    }
#if 0  // Химреактор: не включать в текущую прошивку фаблаб-теплицы.
    if (switching_) {
      // Повтор той же команды в процессе не перезапускает 250 мс.
      // Другой запрос отклоняется BUSY, исходная команда не теряется.
      if (!equal(request, pending_)) setReply(response(request, BUSY));
      return;
    }
#endif
    if (cached_ && request.session_id == lastRequest_.session_id &&
        request.tx_id == lastRequest_.tx_id) {
      setReply(equal(request, lastRequest_) ? lastReply_ :
               response(request, REQUEST_ID_CONFLICT));
      return;
    }
#if 0  // Заготовка химреактора: break-before-make без блокирующего delay().
    if (request.command == kSetRelayLevel) {
      pending_ = request; hw_.relays(0); level_ = 0;
      switching_ = true; started_ = now;
      return; // OK только после физического применения всей матрицы.
    }
#endif
    if (request.command == kSetDoorLock) {
      lock_ = request.data[0] != 0; hw_.lock(lock_);
    }
    Frame reply = response(request);
    if (request.command == kGetDoorLock) reply.data[0] = lock_ ? 1 : 0;
    // Химреактор: if (request.command == kGetRelayLevel) reply.data[0] = level_;
    complete(request, reply);
  }
  void tick(uint32_t now) {
    (void)now;
#if 0  // Заготовка химреактора отключена: фаблаб никогда не коммутирует реле.
    if (!switching_ || replyReady_ || static_cast<uint32_t>(now - started_) < kBreakMs) return;
    level_ = pending_.data[0]; hw_.relays(relayMask(level_)); switching_ = false;
    complete(pending_, hardware_api::response(pending_));
#endif
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
  uint8_t level_; bool lock_, switching_, replyReady_, cached_;
  uint32_t started_;
  hardware_api::Frame pending_, reply_, lastRequest_, lastReply_;
};
}
