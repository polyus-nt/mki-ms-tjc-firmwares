#pragma once
#include "BoardConfig.h"
namespace tjc_lmp8 {
template<class Channel> class Outputs {
 public:
  Outputs(Channel& lock, Channel* relay, void (*flush)())
      : lock_(lock), relay_(relay), flush_(flush) {}
  void relays(uint8_t mask) {
    for(uint8_t i=0;i<6;++i) {
      const bool enabled=(mask & (1u<<i))!=0;
      const bool high=kRelayActiveLow ? !enabled : enabled;
      if(high) relay_[i].on(); else relay_[i].off();
    }
    flush_();
  }
  void lock(bool engaged) {
    const bool high=kLockActiveLow ? !engaged : engaged;
    if(high) lock_.on(); else lock_.off();
    flush_();
  }
 private:
  Channel& lock_;
  Channel* relay_;
  void (*flush_)();
};
}
