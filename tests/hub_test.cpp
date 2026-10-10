#include <assert.h>
#include <stdio.h>
#include "../firmware/hub/Hub.h"
using namespace hardware_api;
using tjc_hub::Hub;
static Frame request(uint8_t node = 14, uint8_t cmd = kSetPwm) {
  Frame frame = {}; frame.version = hardware_api::kVersion; frame.kind = kRequest;
  frame.tx_id = 123; frame.node = node; frame.command = cmd;
  frame.data[0] = cmd == kSetPwm ? 100 : 0;
  return frame;
}
static void feed(Hub& hub, const Frame& frame, uint32_t now) {
  uint8_t bytes[kFrameSize]; encode(frame, bytes);
  for (unsigned i = 0; i < kFrameSize; ++i) hub.receiveBus(bytes[i], now);
}
static void start(Hub& hub, const Frame& req, uint32_t now = 300) {
  feed(hub, req, now); Frame out;
  assert(hub.takeGatewayRequest(out));
  uint8_t a[kFrameSize], b[kFrameSize]; encode(out,a); encode(req,b);
  assert(memcmp(a,b,kFrameSize)==0);
  hub.gatewayRequestSent(now + 150); assert(hub.state()==Hub::Waiting);
}
static Frame stage(Hub& hub, uint8_t node, uint8_t cmd, uint8_t idx, uint32_t now) {
  Frame out; assert(hub.takeGatewayRequest(out));
  assert(out.node == node && out.command == cmd && out.idx == idx);
  hub.gatewayRequestSent(now); return out;
}
static void modeReply(Hub& hub, const Frame& req, uint8_t mode, uint8_t idx, uint32_t now) {
  Frame reply=response(req); reply.data[0]=mode; reply.data[1]=idx; feed(hub,reply,now);
}
int main() {
  Frame out;
  {
    Hub hub; hub.tick(250); assert(hub.state()==Hub::Idle);
    const Frame req=request(); start(hub,req);
    Frame reply=response(req); reply.data[0]=230;
    Frame old_session=reply; ++old_session.session_id; feed(hub,old_session,490);
    assert(!hub.takeHostResponse(out));
    Frame wrong=reply; ++wrong.tx_id; feed(hub,wrong,500);
    assert(!hub.takeHostResponse(out));
    wrong=reply; wrong.status=5; feed(hub,wrong,510);
    assert(!hub.takeHostResponse(out)); // Nonzero error payload rejected.
    feed(hub,reply,520); assert(hub.takeHostResponse(out));
    assert(out.data[0]==230); hub.hostResponseSent(670);
    assert(hub.state()==Hub::Idle);
    start(hub,request(13,kGetTemp),700);
  }
  {
    Hub hub; hub.tick(250); const Frame req=request(); start(hub,req);
    Frame second=request(); ++second.tx_id; feed(hub,second,500);
    Frame third=second; ++third.tx_id; feed(hub,third,600);
    feed(hub,response(req),700); assert(hub.takeHostResponse(out));
    assert(out.tx_id==req.tx_id); hub.hostResponseSent(850);
    assert(hub.takeHostResponse(out)); assert(out.tx_id==second.tx_id && out.status==BUSY);
    hub.hostResponseSent(1000); assert(hub.state()==Hub::Idle);
    assert(!hub.takeHostResponse(out));
  }
  {
    Hub hub; hub.tick(250); const Frame req=request(); start(hub,req);
    hub.tick(3300); assert(hub.takeHostResponse(out)); assert(out.status==DEVICE_TIMEOUT);
    assert(zeros(out.data,8)); assert(hub.takeCancelTransmission());
    hub.hostResponseSent(3450); feed(hub,response(req),3500);
    hub.tick(3750); assert(hub.state()==Hub::Idle);
    feed(hub,response(req),3800); assert(!hub.takeHostResponse(out));
  }
  {
    Hub hub; for(unsigned t=0;t<1000;t+=10) hub.receiveBus(0xff,t);
    hub.tick(1000); assert(hub.state()==Hub::Idle); // Noisy startup is bounded.
    start(hub,request(),1010); hub.overflow(1200);
    assert(hub.takeHostResponse(out)); assert(out.status==TRANSPORT_OVERFLOW);
    hub.hostResponseSent(1300); hub.tick(1550); assert(hub.state()==Hub::Idle);
  }
  {
    Hub hub; hub.tick(250); Frame req=request(13,kSetPwm);
    feed(hub,req,300); assert(hub.takeHostResponse(out)); assert(out.status==INVALID_NODE);
    hub.hostResponseSent(450); req=request(); req.command=kSetDoorLock;
    feed(hub,req,500); assert(hub.takeHostResponse(out)); assert(out.status==INVALID_NODE);
  }
  {
    Hub hub; hub.tick(250);
    Frame req=request(kSensorsNode,kGetDoor);
    feed(hub,req,300); assert(hub.takeHostResponse(out)); assert(out.status==INVALID_NODE);
    hub.hostResponseSent(450);
    req=request(kButtonsNode,kGetDoor); req.idx=1;
    start(hub,req,500);
    Frame reply=response(req); reply.data[0]=1;
    feed(hub,reply,700); assert(hub.takeHostResponse(out));
    assert(out.node==kButtonsNode && out.command==kGetDoor && out.idx==1 && out.data[0]==1);
    hub.hostResponseSent(850); assert(hub.state()==Hub::Idle);
  }
  {
    Hub hub(0xffffff00u); hub.tick(0xfffffffaU); start(hub,request(),0xfffffffbU);
    hub.tick(static_cast<uint32_t>(0xfffffffbU+3000u));
    assert(hub.takeHostResponse(out)); assert(out.status==DEVICE_TIMEOUT);
  }
  {
    Hub hub; hub.tick(250); uint8_t bytes[kFrameSize]; encode(request(),bytes);
    bytes[18]^=1; for(unsigned i=0;i<kFrameSize;++i) hub.receiveBus(bytes[i],300);
    assert(!hub.takeGatewayRequest(out));
    hub.receiveBus(0xa5,310); hub.receiveBus(0x5a,311); hub.tick(600);
    start(hub,request(),610);
  }
  {
    const uint8_t commands[]={kGetTemp,kGetVoc,kGetDoor,kGetPressure,kGetHumidity,kGetDust,
                              kGetPwm,kGetRpm,kSetPwm,
                              kGetDoorLock,kSetDoorLock,kGetRelayLevel,kSetRelayLevel,kGetVacuum,kSetVacuum};
    for (unsigned i=0;i<sizeof(commands);++i) {
      Hub hub; hub.tick(250);
      const uint8_t command=commands[i];
      const uint8_t node = command == kGetDoor ? kButtonsNode :
          ((command == kGetDoorLock || command == kSetDoorLock ||
            command == kGetRelayLevel || command == kSetRelayLevel ||
            command == kGetVacuum || command == kSetVacuum) ? kLmp8Node :
           (command < kGetPwm ? kSensorsNode : kActuatorNode));
      Frame req=request(node,command);
      if(command==kGetMode) req.idx=0xff;
      start(hub,req);
      Frame reply=response(req);
      Frame wrong=reply; wrong.version=1; feed(hub,wrong,500);
      wrong=reply; wrong.node^=1; feed(hub,wrong,510);
      wrong=reply; wrong.command^=1; feed(hub,wrong,520);
      wrong=reply; wrong.idx^=1; feed(hub,wrong,530);
      wrong=reply; wrong.status=REQUEST_ID_CONFLICT; feed(hub,wrong,540);
      wrong=reply; wrong.status=255; feed(hub,wrong,550);
      assert(!hub.takeHostResponse(out));
      feed(hub,reply,560); assert(hub.takeHostResponse(out));
    }
  }
  {
    for (uint8_t idx=0; idx<6; ++idx) {
      Hub hub; hub.tick(250); Frame req=request(); req.idx=idx; feed(hub,req,300);
      Frame physical=stage(hub,idx<4 ? 14 : 17,kSetPwm,idx<4 ? idx : idx-4,400);
      Frame reply=response(physical); reply.data[0]=230;
      if (idx>=4) { feed(hub,response(req),420); assert(!hub.takeHostResponse(out)); }
      feed(hub,reply,450); assert(hub.takeHostResponse(out));
      assert(sameRequest(req,out) && out.data[0]==230);
    }
    Hub hub; hub.tick(250); feed(hub,request(17),300);
    assert(hub.takeHostResponse(out) && out.status==INVALID_NODE);
  }
  {
    Hub hub; hub.tick(250); Frame req=request(14,kGetMode); req.idx=0xff; feed(hub,req,300);
    Frame a=stage(hub,14,kGetMode,0xff,400); modeReply(hub,a,0,0xff,450);
    Frame b=stage(hub,17,kGetMode,0xff,500); modeReply(hub,b,2,1,550);
    assert(hub.takeHostResponse(out) && sameRequest(req,out));
    assert(out.data[0]==2 && out.data[1]==5);
    hub.hostResponseSent(600); feed(hub,req,700);
    a=stage(hub,14,kGetMode,0xff,800); modeReply(hub,a,1,0,850);
    b=stage(hub,17,kGetMode,0xff,900); modeReply(hub,b,2,1,950);
    assert(hub.takeHostResponse(out) && out.status==INTERNAL_ERROR && zeros(out.data,8));
  }
  {
    Hub hub; hub.tick(250); Frame req=request(14,kSetMode); req.idx=4; req.data[0]=1; feed(hub,req,300);
    Frame a=stage(hub,14,kGetMode,0xff,400); modeReply(hub,a,2,2,450);
    Frame b=stage(hub,17,kGetMode,0xff,500); modeReply(hub,b,0,0xff,550);
    a=stage(hub,14,kSetMode,0,600); assert(a.data[0]==0); modeReply(hub,a,0,0xff,650);
    b=stage(hub,17,kSetMode,0,700); assert(b.data[0]==0); modeReply(hub,b,0,0xff,750);
    Frame c=stage(hub,17,kSetMode,0,800); assert(c.data[0]==1 && c.session_id!=b.session_id);
    modeReply(hub,b,0,0xff,820); assert(!hub.takeHostResponse(out));
    modeReply(hub,c,1,0,850); assert(hub.takeHostResponse(out));
    assert(sameRequest(req,out) && out.data[0]==1 && out.data[1]==4);
    hub.hostResponseSent(900); feed(hub,req,1000);
    a=stage(hub,14,kGetMode,0xff,1100); modeReply(hub,a,0,0xff,1150);
    b=stage(hub,17,kGetMode,0xff,1200); modeReply(hub,b,1,0,1250);
    assert(hub.takeHostResponse(out) && out.status==OK && out.data[1]==4);
    assert(!hub.takeGatewayRequest(out)); // Idempotent: no writes / re-randomization.
  }
  {
    Hub hub; hub.tick(250); Frame req=request(14,kSetMode); req.idx=5; req.data[0]=2; feed(hub,req,300);
    Frame a=stage(hub,14,kGetMode,0xff,400); modeReply(hub,a,1,0,450);
    Frame b=stage(hub,17,kGetMode,0xff,500); modeReply(hub,b,0,0xff,550);
    a=stage(hub,14,kSetMode,0,600); feed(hub,response(a,DEVICE_TIMEOUT),650);
    assert(hub.takeHostResponse(out) && sameRequest(req,out) && out.status==DEVICE_TIMEOUT);
    assert(!hub.takeGatewayRequest(out)); // Failed reset must never enable another fault.
  }
  {
    Hub hub; hub.tick(250); Frame req=request(14,kGetMode); req.idx=0xff; feed(hub,req,300);
    Frame a=stage(hub,14,kGetMode,0xff,400); modeReply(hub,a,0,0,450); // Bad normal-mode sentinel.
    assert(!hub.takeGatewayRequest(out) && !hub.takeHostResponse(out));
    hub.tick(4300); assert(hub.takeHostResponse(out) && out.status==DEVICE_TIMEOUT);
  }
  {
    // Реалистичные ~700 мс на полный SimpleBus+Austere обмен: пять шагов
    // превышают обычный 3-секундный дедлайн, но укладываются в режимный 4 с.
    Hub hub; hub.tick(250); Frame req=request(14,kSetMode); req.idx=5; req.data[0]=2;
    feed(hub,req,300);
    Frame a=stage(hub,14,kGetMode,0xff,450); modeReply(hub,a,1,0,1000);
    Frame b=stage(hub,17,kGetMode,0xff,1150); modeReply(hub,b,0,0xff,1700);
    a=stage(hub,14,kSetMode,0,1850); modeReply(hub,a,0,0xff,2400);
    b=stage(hub,17,kSetMode,0,2550); modeReply(hub,b,0,0xff,3100);
    Frame c=stage(hub,17,kSetMode,1,3250); modeReply(hub,c,2,1,3800);
    assert(hub.takeHostResponse(out) && out.status==OK && out.data[1]==5);
  }
  {
    Hub hub; hub.tick(250); const Frame req=request(); feed(hub,req,300);
    feed(hub,response(req),310); assert(!hub.takeHostResponse(out));
    hub.tick(3300); assert(hub.takeHostResponse(out)); assert(out.status==DEVICE_TIMEOUT);
  }
  {
    Hub hub; hub.tick(250); start(hub,request());
    hub.overflow(500); assert(hub.takeHostResponse(out));
    hub.overflow(600); hub.hostResponseSent(700);
    for(unsigned t=710;t<1700;t+=10) hub.receiveBus(0xff,t);
    hub.tick(1700); assert(hub.state()==Hub::Idle);
    start(hub,request(),1710);
  }
  puts("hub tests passed");
}
