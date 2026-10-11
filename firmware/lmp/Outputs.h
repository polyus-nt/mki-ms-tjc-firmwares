#pragma once
#include "BoardConfig.h"
namespace tjc_lmp {
// LED обычной лампочницы немедленно выставляет GPIO: controller.update не нужен.
template<class Channel> class Outputs {
 public:
  Outputs(Channel* locks, Channel& vacuum) : locks_(locks), vacuum_(vacuum) {}
  void lock(uint8_t idx, bool engaged) {
    const bool high=kLockActiveLow ? !engaged : engaged;
    if(high) locks_[idx].on(); else locks_[idx].off();
  }
  void vacuum(bool enabled) {
    const bool high=kVacuumActiveLow ? !enabled : enabled;
    if(high) vacuum_.on(); else vacuum_.off();
  }
 private:
  Channel* locks_;
  Channel& vacuum_;
};
}
