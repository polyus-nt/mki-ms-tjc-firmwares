#pragma once
#include "../common/HardwareProtocol.h"

namespace tjc_hub {
// Хаб — единственный инициатор внутренних запросов. PC ждёт 1 с тишины;
// пауза между опросами 1.5 с оставляет ему окно для начала кадра.
// Core — существующий Hub; его маршрутизация/ошибки не заменяются.
// Один запрос PC, пришедший во время фонового обмена, сохраняется до его конца.
template<class Core> class DoorHub {
 public:
  explicit DoorHub(uint8_t lockCount=2)
      : locks_(lockCount), background_(false), hostPending_(false), first_(true),
        lastPoll_(0), started_(0), nextAllowed_(0), pending_(0), activeLock_(0),
        sequence_(0), session_(UINT64_C(0x444F4F5200000000)), polling_(false) {
    counters_[0]=counters_[1]=0;
  }
  void setSessionSeed(uint32_t seed) { session_ ^= seed; }
  void tick(uint32_t now) {
    input_.tick(now); core_.tick(now);
    // Фоновый узел не должен занимать шину на полный host timeout.
    if (background_ && static_cast<uint32_t>(now-started_) >= 1000)
      core_.overflow(now);
    if (background_) {
      hardware_api::Frame reply;
      if (core_.takeHostResponse(reply)) {
        core_.hostResponseSent(now); background_=false;
        if (polling_) {
          if (reply.status==hardware_api::OK) {
            const uint32_t values[2]={hardware_api::readU32(reply.data),
                                     hardware_api::readU32(reply.data+4)};
            if (!first_) {
              // Обратный скачок счётчика — перезапуск кнопочницы.
              // Разность по модулю uint32 сохраняет нажатие при wrap в 0.
              const bool reset = static_cast<uint32_t>(values[0]-counters_[0]) > 0x7fffffffUL ||
                                 static_cast<uint32_t>(values[1]-counters_[1]) > 0x7fffffffUL;
              if (!reset) {
                for (uint8_t i=0;i<2;++i)
                  if (values[i]!=counters_[i]) pending_ |= 1u << (locks_==1 ? 0 : i);
              }
            }
            counters_[0]=values[0]; counters_[1]=values[1]; first_=false;
          }
          lastPoll_=now;
        } else if (reply.status!=hardware_api::BUSY) {
          // При таймауте команда могла выполниться: не повторяем её автоматически.
          pending_ &= static_cast<uint8_t>(~(1u << activeLock_));
        }
        nextAllowed_=now; // BUSY ниже ограничивается периодом повторной попытки.
        if (reply.status==hardware_api::BUSY) nextAllowed_=now+500;
      }
    }
    if (background_ || core_.state()!=Core::Idle || input_.partial()) return;
    // Импульсы из текущего снимка раньше отложенной команды админки:
    // последняя явная команда сможет отменить уже созданный таймер.
    if (pending_ && static_cast<int32_t>(now-nextAllowed_)>=0) {
      uint8_t idx=(pending_ & 1) ? 0 : 1;
      start(hardware_api::kLmp8Node, hardware_api::kOpenDoorTimed,idx,now);
    } else if (hostPending_) {
      hostPending_=false; feedCore(host_,now);
    } else if (static_cast<uint32_t>(now-lastPoll_)>=1500 &&
               static_cast<int32_t>(now-nextAllowed_)>=0) {
      start(hardware_api::kButtonsNode,hardware_api::kGetOpenButtons,0,now);
    }
  }
  void receiveBus(uint8_t byte,uint32_t now) {
    hardware_api::Frame frame;
    if (!input_.feed(byte,now,frame)) return;
    if (background_ && frame.kind==hardware_api::kRequest) {
      // FastAPI сериализует запросы. Сохраняем первый, не заменяем другим.
      if (!hostPending_) { host_=frame; hostPending_=true; }
      return;
    }
    feedCore(frame,now);
  }
  bool takeGatewayRequest(hardware_api::Frame& f) { return core_.takeGatewayRequest(f); }
  void gatewayRequestSent(uint32_t now) { core_.gatewayRequestSent(now); }
  bool takeHostResponse(hardware_api::Frame& f) {
    return !background_ && core_.takeHostResponse(f);
  }
  void hostResponseSent(uint32_t now) { core_.hostResponseSent(now); }
  bool takeCancelTransmission() { return core_.takeCancelTransmission(); }
  void overflow(uint32_t now) { input_.reset(); core_.overflow(now); }
 private:
  void feedCore(const hardware_api::Frame& f,uint32_t now) {
    uint8_t bytes[hardware_api::kFrameSize]; hardware_api::encode(f,bytes);
    for (uint8_t i=0;i<hardware_api::kFrameSize;++i) core_.receiveBus(bytes[i],now);
  }
  void start(uint8_t node,uint8_t command,uint8_t idx,uint32_t now) {
    hardware_api::Frame f={}; f.version=hardware_api::kVersion;
    f.kind=hardware_api::kRequest; f.node=node; f.command=command; f.idx=idx;
    ++sequence_; f.tx_id=static_cast<uint16_t>(sequence_);
    f.session_id=session_ ^ (sequence_ >> 16);
    background_=true; started_=now; activeLock_=idx;
    polling_=command==hardware_api::kGetOpenButtons;
    feedCore(f,now);
  }
  Core core_;
  hardware_api::StreamParser input_;
  uint8_t locks_;
  bool background_,hostPending_,first_;
  uint32_t lastPoll_,started_,nextAllowed_;
  uint8_t pending_,activeLock_;
  uint64_t sequence_,session_;
  bool polling_;
  uint32_t counters_[2];
  hardware_api::Frame host_;
};
} // namespace tjc_hub
