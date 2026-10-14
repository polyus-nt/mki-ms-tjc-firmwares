#include <cassert>
#include <cstdio>
#ifndef HOST_HUB_HEADER
#define HOST_HUB_HEADER "../firmware/hub/Hub.h"
#define HOST_ARBITER_HEADER "../firmware/hub/HostArbiter.h"
#endif
#include HOST_HUB_HEADER
#include HOST_ARBITER_HEADER
using namespace hardware_api;
using Arb=tjc_hub::HostArbiter<tjc_hub::Hub>;
template<class T> void feed(T& t,const Frame& f,uint32_t now) {
  uint8_t b[kFrameSize];encode(f,b);for(uint8_t x:b)t.receiveBus(x,now);
}
Frame claim(const Frame& ready,uint64_t salt=7) {
  Frame f=ready;f.kind=kRequest;f.command=Arb::kClaim;
  writeU64(f.data,salt);return f;
}
Frame command(const Frame& granted,uint8_t code=kGetPressure,uint8_t idx=0) {
  Frame f=granted;f.kind=kRequest;f.node=13;f.command=code;f.idx=idx;
  memset(f.data,0,8);return f;
}
Frame acquire(Arb& a,uint32_t now) {
  a.tick(now);Frame ready,granted,down;
  assert(a.takeHostResponse(ready)&&ready.command==Arb::kReady);
  a.hostResponseSent(now+1);feed(a,claim(ready),now+2);
  assert(a.takeHostResponse(granted)&&granted.command==Arb::kGrant);
  assert(granted.session_id==(ready.session_id^7));
  assert(!a.takeGatewayRequest(down));
  a.hostResponseSent(now+3);return granted;
}
int main() {
  { // До CLAIM/GRANT действия запрещены; устаревший номер окна тоже.
    Arb a;Frame unsolicited={};unsolicited.version=2;unsolicited.kind=1;
    unsolicited.node=13;unsolicited.command=kGetPressure;
    feed(a,unsolicited,250);Frame down;
    assert(!a.takeGatewayRequest(down));
    Frame grant=acquire(a,250);
    auto stale=command(grant);stale.session_id^=1;feed(a,stale,255);
    assert(!a.takeGatewayRequest(down));
    auto real=command(grant);feed(a,real,256);
    assert(a.takeGatewayRequest(down)&&down.kind==kRequest&&down.command==real.command&&
           down.session_id==real.session_id&&down.tx_id==real.tx_id);
    a.gatewayRequestSent(256);
    feed(a,real,257);assert(!a.takeGatewayRequest(down)); // повтор не исполняется
    feed(a,response(real),260);Frame result;
    assert(a.takeHostResponse(result)&&sameRequest(real,result));
    a.hostResponseSent(261);
    a.tick(261);assert(a.takeHostResponse(result)&&result.command==Arb::kReady);
    // Новое окно немедленно после ответа, без секунды тишины.
  }
  { // Молчание компьютера не мешает автономному опросу раз в 2 с.
    Arb a;uint32_t polls[3]={};unsigned count=0;Frame f;
    for(uint32_t now=250;now<=4500;++now) {
      a.tick(now);
      if(a.takeHostResponse(f)) a.hostResponseSent(now);
      if(a.takeGatewayRequest(f)) {
        assert(f.command==kGetOpenButtons);
        assert(count<3);polls[count++]=now;
        a.gatewayRequestSent(now);feed(a,response(f),now);a.tick(now);
      }
    }
    assert(count==2);
    assert(polls[0]>=2000&&polls[0]<=2110);
    assert(polls[1]-polls[0]>=2000&&polls[1]-polls[0]<=2110);
  }
  { // Внутренний опрос не прерывает уже начатую внешнюю команду.
    Arb a;auto grant=acquire(a,1900);auto real=command(grant);
    feed(a,real,1910);Frame down;
    assert(a.takeGatewayRequest(down)&&down.command==kGetPressure);
    a.gatewayRequestSent(1910);
    a.tick(2000);assert(!a.takeGatewayRequest(down));
    feed(a,response(real),2050);Frame result;
    assert(a.takeHostResponse(result)&&result.command==kGetPressure);
    a.hostResponseSent(2051);a.tick(2051);
    assert(a.takeGatewayRequest(down)&&down.command==kGetOpenButtons);
  }
  { // Просроченный CLAIM не подтверждается.
    Arb a;a.tick(250);Frame ready,f;
    assert(a.takeHostResponse(ready));a.hostResponseSent(251);
    feed(a,claim(ready),351);assert(!a.takeHostResponse(f));
    a.tick(351);assert(a.takeHostResponse(f)&&f.command==Arb::kReady);
    assert(f.tx_id!=ready.tx_id);
  }
  { // Просроченная команда после GRANT не исполняется.
    Arb a;auto grant=acquire(a,250);Frame f;
    feed(a,command(grant),353);assert(!a.takeGatewayRequest(f));
    a.tick(353);assert(a.takeHostResponse(f)&&f.command==Arb::kReady);
  }
  puts("arbiter: handshake, stale permits, one command, no success pause, offline polling and deadlines passed");
}
