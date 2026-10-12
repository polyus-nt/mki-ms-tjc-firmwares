#include <cassert>
#include <cstdio>
#ifdef CHEM_OUTPUTS
#include "../firmware/lmp8/Lmp8Api.h"
#include "../firmware/lmp8/Outputs.h"
namespace cfg=tjc_lmp8;
#else
#include "../firmware/lmp/LmpApi.h"
#include "../firmware/lmp/Outputs.h"
namespace cfg=tjc_lmp;
#endif
struct Channel {
  bool high=false;
  void on() { high=true; }
  void off() { high=false; }
};
using namespace hardware_api;
Frame request(uint8_t command,uint8_t idx=0,uint8_t value=0) {
  static uint16_t id=0;
  Frame f={};f.version=kVersion;f.kind=kRequest;f.node=16;
  f.command=command;f.idx=idx;f.data[0]=value;f.session_id=5;f.tx_id=++id;
  return f;
}
int main() {
  Frame reply;
#ifdef CHEM_OUTPUTS
  Channel lock,relays[6];cfg::Outputs<Channel> outputs(lock,relays,[](){});
  cfg::Lmp8Api<cfg::Outputs<Channel>> api(outputs);
  assert(!cfg::startupHigh(0)&&!lock.high);
  for(uint8_t i=1;i<8;++i)assert(!cfg::startupHigh(i));
  const uint8_t masks[]={0,49,51,55,63,33,17,35,39,47,1};
  for(uint8_t level=0;level<=10;++level) {
    const uint32_t start=1000u*level;
    api.receive(request(kSetRelayLevel,0,level),start);
    for(const auto& ch:relays)assert(!ch.high); // break before make: LOW.
    api.tick(start+249);for(const auto& ch:relays)assert(!ch.high);
    api.tick(start+250);assert(api.takeReply(reply)&&reply.status==OK);
    for(uint8_t i=0;i<6;++i)assert(relays[i].high==!!(masks[level]&(1u<<i)));
    assert(!lock.high); // Переключение скорости не трогает замки.
  }
  api.receive(request(kOpenDoorTimed),20000);
  assert(api.takeReply(reply)&&lock.high);
  api.tick(21999);assert(lock.high);api.tick(22000);assert(!lock.high);
  api.receive(request(kSetDoorLock,0,0),26000);
  assert(api.takeReply(reply)&&lock.high);
  api.receive(request(kSetDoorLock,0,1),27000);
  assert(api.takeReply(reply)&&!lock.high);
#else
  Channel locks[2],vacuum;cfg::Outputs<Channel> outputs(locks,vacuum);
  cfg::LmpApi<cfg::Outputs<Channel>> api(outputs);
  assert(!cfg::startupHigh(0)&&!cfg::startupHigh(1)&&!cfg::startupHigh(2));
  assert(!locks[0].high&&!locks[1].high&&!vacuum.high);
  for(uint8_t idx=0;idx<2;++idx) {
    api.receive(request(kOpenDoorTimed,idx),1000);
    assert(api.takeReply(reply)&&locks[idx].high&&!locks[1-idx].high);
    api.tick(2999);assert(locks[idx].high);api.tick(3000);assert(!locks[idx].high);
    api.receive(request(kSetDoorLock,idx,0),7000);
    assert(api.takeReply(reply)&&locks[idx].high);
    api.receive(request(kSetDoorLock,idx,1),8000);
    assert(api.takeReply(reply)&&!locks[idx].high);
  }
  api.receive(request(kSetVacuum,0,1),9000);
  assert(api.takeReply(reply)&&vacuum.high);
  api.receive(request(kSetVacuum,0,0),10000);
  assert(api.takeReply(reply)&&!vacuum.high);
#endif
  puts("MC outputs: startup LOW closed locks, LOW motors, physical levels, timed opening passed");
}
