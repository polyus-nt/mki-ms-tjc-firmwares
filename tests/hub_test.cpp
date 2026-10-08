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
    feed(hub,req,500); assert(hub.takeHostResponse(out)); assert(out.status==UNKNOWN_COMMAND);
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
                              kGetPwm,kGetRpm,kSetPwm,kGetMode,kSetMode};
    for (unsigned i=0;i<sizeof(commands);++i) {
      Hub hub; hub.tick(250);
      const uint8_t command=commands[i];
      Frame req=request(command<kGetPwm ? 13 : 14,command);
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
