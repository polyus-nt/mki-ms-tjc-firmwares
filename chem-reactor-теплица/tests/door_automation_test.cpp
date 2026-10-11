#include <cassert>
#include <cstdio>
#include "../firmware/hub/Hub.h"
#include "../firmware/hub/DoorHub.h"
#include "../firmware/buttons/ButtonsApi.h"
#include "../firmware/lmp8/Lmp8Api.h"
using namespace hardware_api;
#ifdef CHEM_DOORS
const uint8_t lockCount=1;
struct Outputs {
  bool locked=true;
  void lock(bool value) { locked=value; }
  void relays(uint8_t) {}
  bool state(uint8_t) const { return locked; }
};
#else
const uint8_t lockCount=2;
struct Outputs {
  bool locked[2]={true,true};
  void lock(uint8_t idx,bool value) { locked[idx]=value; }
  void vacuum(bool) {}
  bool state(uint8_t idx) const { return locked[idx]; }
};
#endif
Frame req(uint8_t node,uint8_t command,uint8_t idx=0,uint8_t value=0) {
  static uint16_t id=1; Frame f={}; f.version=kVersion; f.kind=kRequest;
  f.node=node;f.command=command;f.idx=idx;f.data[0]=value;f.tx_id=id++;f.session_id=123;
  return f;
}
template<class Target> void feed(Target& t,const Frame& f,uint32_t now) {
  uint8_t wire[kFrameSize];encode(f,wire);
  for(uint8_t i=0;i<kFrameSize;++i)t.receiveBus(wire[i],now);
}
int main() {
  Outputs hw;tjc_lmp8::Lmp8Api<Outputs> lmp(hw);Frame r;
  for(uint8_t i=0;i<lockCount;++i)assert(hw.state(i));
  // Локальный таймер: 5 с, повтор новым запросом продлевает; дубликат не продлевает.
  Frame open=req(16,kOpenDoorTimed);
  lmp.receive(open,100);assert(lmp.takeReply(r)&&r.status==OK&&!hw.state(0));
  lmp.receive(open,200);assert(lmp.takeReply(r)&&r.status==OK);
  lmp.tick(5099);assert(!hw.state(0));lmp.tick(5100);assert(hw.state(0));
  open=req(16,kOpenDoorTimed);lmp.receive(open,6000);assert(lmp.takeReply(r));
  open=req(16,kOpenDoorTimed);lmp.receive(open,7000);assert(lmp.takeReply(r));
  lmp.tick(11000);assert(!hw.state(0));lmp.tick(12000);assert(hw.state(0));
  // Таймер работает без связи, с незабранным ответом и через wrap millis.
  open=req(16,kOpenDoorTimed);lmp.receive(open,0xffffff00UL);
  lmp.tick(0x1287);assert(!hw.state(0));lmp.tick(0x1288);assert(hw.state(0));
  assert(lmp.takeReply(r));
  // Явное состояние админки отменяет таймер и для unlock, и для lock.
  open=req(16,kOpenDoorTimed);lmp.receive(open,20000);assert(lmp.takeReply(r));
  lmp.receive(req(16,kSetDoorLock,0,0),21000);assert(lmp.takeReply(r));
  lmp.tick(30000);assert(!hw.state(0));
  open=req(16,kOpenDoorTimed);lmp.receive(open,31000);assert(lmp.takeReply(r));
  lmp.receive(req(16,kSetDoorLock,0,1),31500);assert(lmp.takeReply(r));
  lmp.tick(40000);assert(hw.state(0));
#ifndef CHEM_DOORS
  // Независимые двери.
  lmp.receive(req(16,kOpenDoorTimed,1),50000);assert(lmp.takeReply(r));
  assert(hw.state(0)&&!hw.state(1));lmp.tick(55000);assert(hw.state(1));
#endif
  open=req(16,kOpenDoorTimed,lockCount);lmp.receive(open,60000);
  assert(lmp.takeReply(r)&&r.status==INVALID_INDEX);
  open=req(16,kOpenDoorTimed);open.data[7]=1;lmp.receive(open,60000);
  assert(lmp.takeReply(r)&&r.status==INVALID_ARGUMENT);
  // Нажатия, удержание и отпускание; при старте удерживаемая кнопка игнорируется.
  tjc_buttons::ButtonsApi buttons;
  buttons.sample(false,false,100);buttons.sampleOpen(true,false,100);
  Frame snap=req(15,kGetOpenButtons);
  assert(readU32(buttons.execute(snap).data)==0);
  buttons.sampleOpen(true,false,200);assert(readU32(buttons.execute(snap).data)==0);
  buttons.sampleOpen(false,false,300);buttons.sampleOpen(true,false,400);
  assert(readU32(buttons.execute(snap).data)==1);
  buttons.sampleOpen(true,false,500);assert(readU32(buttons.execute(snap).data)==1);
  buttons.sampleOpen(false,true,600);assert(readU32(buttons.execute(snap).data+4)==1);
  // Полная автономная цепочка и отложенный запрос PC.
  tjc_hub::DoorHub<tjc_hub::Hub> hub(lockCount);
  hub.tick(500);hub.tick(1500);Frame down;
  assert(hub.takeGatewayRequest(down)&&down.command==kGetOpenButtons);
  hub.gatewayRequestSent(1500);feed(hub,buttons.execute(down),1600);hub.tick(1600);
  assert(!hub.takeHostResponse(r)); // Фоновые ответы не попадают на компьютер.
  buttons.sampleOpen(false,false,1650);buttons.sampleOpen(true,true,1700);
  hub.tick(3100);assert(hub.takeGatewayRequest(down)&&down.command==kGetOpenButtons);
  hub.gatewayRequestSent(3100);
  Frame host=req(16,kSetDoorLock,0,1);feed(hub,host,3150); // Во время фонового чтения.
  feed(hub,buttons.execute(down),3200);hub.tick(3200);
  for(uint8_t i=0;i<lockCount;++i) {
    assert(hub.takeGatewayRequest(down)&&down.command==kOpenDoorTimed&&down.idx==i);
    hub.gatewayRequestSent(3200+i*100);
    lmp.receive(down,3200+i*100);assert(lmp.takeReply(r));feed(hub,r,3250+i*100);
    hub.tick(3250+i*100);
  }
  assert(hub.takeGatewayRequest(down)&&down.command==kSetDoorLock);
  hub.gatewayRequestSent(3500);lmp.receive(down,3500);assert(lmp.takeReply(r));
  feed(hub,r,3600);assert(hub.takeHostResponse(r)&&sameRequest(host,r)&&r.status==OK);
  hub.hostResponseSent(3600);assert(hw.state(0));
  lmp.tick(10000);for(uint8_t i=0;i<lockCount;++i)assert(hw.state(i));
  // Нет кнопочницы: bounded timeout, хаб восстанавливается и обслуживает PC.
  tjc_hub::DoorHub<tjc_hub::Hub> missing(lockCount);
  missing.tick(500);missing.tick(1500);assert(missing.takeGatewayRequest(down));
  missing.gatewayRequestSent(1500);feed(missing,req(16,kGetDoorLock),1600);
  missing.tick(2500);assert(!missing.takeHostResponse(r));
  missing.tick(2750);assert(missing.takeGatewayRequest(down)&&down.command==kGetDoorLock);
  // Counter wrap в ноль — событие, а обычный сброс после reboot — нет.
  for(uint8_t scenario=0;scenario<2;++scenario) {
    tjc_hub::DoorHub<tjc_hub::Hub> h(lockCount);
    h.tick(500);h.tick(1500);assert(h.takeGatewayRequest(down));
    h.gatewayRequestSent(1500);
    Frame values=response(down);
    writeU32(values.data,scenario ? 10u : 0xffffffffUL);
    feed(h,values,1600);h.tick(1600);
    h.tick(3100);assert(h.takeGatewayRequest(down));h.gatewayRequestSent(3100);
    values=response(down); // новое значение 0
    feed(h,values,3200);h.tick(3200);
    const bool issued=h.takeGatewayRequest(down);
    assert(issued==!scenario);
    if(issued)assert(down.command==kOpenDoorTimed&&down.idx==0);
  }
  puts("doors: boot locked, edge counters, timers, duplicate, wrap, admin override, hub queue and offline timeout passed");
}
