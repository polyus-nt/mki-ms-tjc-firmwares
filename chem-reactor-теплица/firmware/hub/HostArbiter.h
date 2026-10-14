#pragma once
#include "DoorHub.h"

namespace tjc_hub {
// READY -> CLAIM -> GRANT -> одна команда. Только хаб управляет фазами шины.
// Короткая аренда действует при молчании ПК; после успешного ответа нет паузы.
template<class Core> class HostArbiter {
 public:
  enum Phase { Idle, SendingReady, AwaitClaim, SendingGrant, AwaitCommand, Executing };
  enum { kReady=0xf0, kClaim=0xf1, kGrant=0xf2 };
  static const uint32_t kLeaseMs=100;
  explicit HostArbiter(uint8_t locks=2) : door_(locks), phase_(Idle),
      controlPending_(false), started_(0), sequence_(0),
      epoch_(UINT64_C(0x484f535400000001)) {}
  void setSessionSeed(uint32_t seed) { epoch_^=seed; door_.setSessionSeed(seed); }
  void tick(uint32_t now) {
    input_.tick(now);
    if ((phase_==AwaitClaim || phase_==AwaitCommand) &&
        static_cast<uint32_t>(now-started_)>=kLeaseMs) {
      input_.reset(); phase_=Idle;
    }
    door_.setHostPhase(phase_!=Idle);
    door_.tick(now);
    if (phase_==Idle && door_.idle()) {
      hardware_api::Frame ready={};
      ready.version=hardware_api::kVersion; ready.kind=hardware_api::kResponse;
      ready.node=12; ready.command=kReady;
      ++sequence_; ready.tx_id=static_cast<uint16_t>(sequence_);
      ready.session_id=epoch_^(sequence_>>16);
      hardware_api::writeU16(ready.data,static_cast<uint16_t>(kLeaseMs));
      ticket_=ready; control_=ready; controlPending_=true; phase_=SendingReady;
      door_.setHostPhase(true);
    }
  }
  void receiveBus(uint8_t byte,uint32_t now) {
    hardware_api::Frame frame;
    if (!input_.feed(byte,now,frame)) return;
    if (frame.kind==hardware_api::kResponse) { feedDoor(frame,now); return; }
    if (frame.version!=hardware_api::kVersion ||
        frame.session_id!=(phase_==AwaitCommand ? control_.session_id : ticket_.session_id) || frame.tx_id!=ticket_.tx_id) return;
    if (phase_==AwaitClaim && static_cast<uint32_t>(now-started_)<kLeaseMs &&
        frame.node==12 && frame.command==kClaim && !frame.idx && !frame.status &&
        !hardware_api::zeros(frame.data,sizeof(frame.data))) {
      control_=ticket_; control_.command=kGrant;
      control_.session_id ^= hardware_api::readU64(frame.data); // Соль нового процесса ПК.
      controlPending_=true;
      phase_=SendingGrant; return;
    }
    if (phase_==AwaitCommand && static_cast<uint32_t>(now-started_)<kLeaseMs &&
        frame.node!=12) {
      phase_=Executing; feedDoor(frame,now);
    }
  }
  bool takeGatewayRequest(hardware_api::Frame& f) { return door_.takeGatewayRequest(f); }
  void gatewayRequestSent(uint32_t now) { door_.gatewayRequestSent(now); }
  bool takeHostResponse(hardware_api::Frame& f) {
    if (controlPending_) { f=control_; controlPending_=false; return true; }
    return phase_==Executing && door_.takeHostResponse(f);
  }
  void hostResponseSent(uint32_t now) {
    started_=now;
    if (phase_==SendingReady) phase_=AwaitClaim;
    else if (phase_==SendingGrant) phase_=AwaitCommand;
    else if (phase_==Executing) { door_.hostResponseSent(now); phase_=Idle; }
  }
  bool takeCancelTransmission() { return door_.takeCancelTransmission(); }
  void overflow(uint32_t now) {
    input_.reset(); door_.overflow(now);
    if (phase_!=Executing) { phase_=Idle; controlPending_=false; }
  }
 private:
  void feedDoor(const hardware_api::Frame& f,uint32_t now) {
    uint8_t bytes[hardware_api::kFrameSize]; hardware_api::encode(f,bytes);
    for(uint8_t i=0;i<hardware_api::kFrameSize;++i) door_.receiveBus(bytes[i],now);
  }
  DoorHub<Core> door_;
  hardware_api::StreamParser input_;
  Phase phase_;
  bool controlPending_;
  uint32_t started_, sequence_;
  uint64_t epoch_;
  hardware_api::Frame ticket_{},control_{};
};
}
