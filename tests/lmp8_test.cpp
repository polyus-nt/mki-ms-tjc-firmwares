#include <assert.h>
#include <stdio.h>
#include "../firmware/lmp8/Lmp8Api.h"
using namespace hardware_api;
struct Hardware {
  uint8_t mask; bool locked; unsigned changes;
  Hardware() : mask(255), locked(true), changes(0) {}
  void relays(uint8_t value) { mask = value; ++changes; }
  void lock(bool value) { locked = value; }
};
Frame req(uint8_t cmd, uint8_t value = 0, uint16_t tx = 1) {
  Frame r{}; r.version = kVersion; r.kind = kRequest; r.node = kLmp8Node;
  r.command = cmd; r.tx_id = tx; r.session_id = 7; r.data[0] = value; return r;
}
#if 0 // Сохранённые тесты будущего химреактора; не запускаются для фаблаб.
int main() {
  Hardware hw; tjc_lmp8::Lmp8Api<Hardware> api(hw); Frame reply;
  assert(hw.mask == 0 && !hw.locked);
  const uint8_t masks[] = {0,49,51,55,63,33,17,35,39,47,1};
  for (uint8_t i = 0; i <= 10; ++i) {
    Frame r = req(kSetRelayLevel, i, i + 1);
    api.receive(r, 1000); assert(hw.mask == 0); assert(!api.takeReply(reply));
    api.tick(1249); assert(hw.mask == 0 && !api.takeReply(reply));
    api.tick(1250); assert(hw.mask == masks[i]); assert(api.takeReply(reply) && reply.status == OK);
    const unsigned changes = hw.changes;
    api.receive(r, 2000); assert(api.takeReply(reply) && reply.status == OK);
    assert(hw.changes == changes); // reply cache avoids contact switching
    r.data[0] = static_cast<uint8_t>((i + 1) % 11);
    api.receive(r, 2001); assert(api.takeReply(reply) && reply.status == REQUEST_ID_CONFLICT);
    assert(hw.changes == changes);
  }
  Frame bad = req(kSetRelayLevel,11,99);
  api.receive(bad,3000); assert(api.takeReply(reply) && reply.status==INVALID_ARGUMENT);
  bad=req(kSetDoorLock,2,100); api.receive(bad,3000);
  assert(api.takeReply(reply) && reply.status==INVALID_ARGUMENT);
  bad=req(kSetDoorLock,1,101); bad.idx=1; api.receive(bad,3000);
  assert(api.takeReply(reply) && reply.status==INVALID_INDEX && !hw.locked);
  bad=req(kGetRelayLevel,1,102); api.receive(bad,3000);
  assert(api.takeReply(reply) && reply.status==INVALID_ARGUMENT);
  bad=req(kSetRelayLevel,1,103); bad.data[7]=1; api.receive(bad,3000);
  assert(api.takeReply(reply) && reply.status==INVALID_ARGUMENT);
  bad=req(kSetRelayLevel,1,104); bad.node=14; api.receive(bad,3000);
  assert(api.takeReply(reply) && reply.status==INVALID_NODE);
  Frame r=req(kSetDoorLock,1,105); api.receive(r,3000);
  assert(api.takeReply(reply) && reply.status==OK && hw.locked);
  r=req(kGetDoorLock,0,106); api.receive(r,3000);
  assert(api.takeReply(reply) && reply.data[0]==1);
  r=req(kSetRelayLevel,4,107); api.receive(r,0xffffff80u);
  api.receive(r,0xffffff90u); assert(!api.takeReply(reply));
  api.receive(req(kGetRelayLevel,0,108),0xffffffa0u);
  assert(api.takeReply(reply) && reply.status==BUSY);
  api.tick(0x79u); assert(!api.takeReply(reply));
  api.tick(0x7au); assert(hw.mask==63 && api.takeReply(reply) && reply.status==OK);
  api.receive(req(kGetRelayLevel,0,109),1000);
  assert(api.takeReply(reply) && reply.status==OK && reply.data[0]==4);
  puts("lmp8: matrix, break-before-make, rollover, boot, validation, duplicates, overlap passed");
}
#endif

int main() {
  Hardware hw; tjc_lmp8::Lmp8Api<Hardware> api(hw); Frame reply;
  assert(hw.mask == 0 && !hw.locked && hw.changes == 1);
  const uint8_t masks[] = {0,49,51,55,63,33,17,35,39,47,1};
  for (uint8_t i = 0; i <= 10; ++i) {
    // Матрица сохранилась, но любая корректная команда реле отключена.
    assert(tjc_lmp8::relayMask(i) == masks[i]);
    api.receive(req(kSetRelayLevel, i, i + 1), 1000);
    assert(api.takeReply(reply) && reply.status == NOT_IMPLEMENTED);
    assert(zeros(reply.data, sizeof reply.data));
    api.tick(1250); api.tick(200000); api.tick(0xffffffffu); api.tick(0);
    assert(hw.mask == 0 && hw.changes == 1 && !api.takeReply(reply));
  }
  api.receive(req(kGetRelayLevel,0,20),1000);
  assert(api.takeReply(reply) && reply.status==NOT_IMPLEMENTED && zeros(reply.data,8));
  api.receive(req(kSetRelayLevel,11,21),1000);
  assert(api.takeReply(reply) && reply.status==INVALID_ARGUMENT);
  Frame bad=req(kSetRelayLevel,1,22); bad.idx=1; api.receive(bad,1000);
  assert(api.takeReply(reply) && reply.status==INVALID_INDEX);
  Frame lock=req(kSetDoorLock,1,30); api.receive(lock,1000);
  assert(api.takeReply(reply) && reply.status==OK && hw.locked);
  api.receive(lock,1200); assert(api.takeReply(reply) && reply.status==OK && hw.locked);
  lock.data[0]=0; api.receive(lock,1300);
  assert(api.takeReply(reply) && reply.status==REQUEST_ID_CONFLICT && hw.locked);
  api.receive(req(kGetDoorLock,0,31),1400);
  assert(api.takeReply(reply) && reply.status==OK && reply.data[0]==1);
  api.receive(req(kSetDoorLock,0,32),1500);
  assert(api.takeReply(reply) && reply.status==OK && !hw.locked);
  bad=req(kSetDoorLock,2,33); api.receive(bad,1500);
  assert(api.takeReply(reply) && reply.status==INVALID_ARGUMENT);
  assert(hw.mask==0 && hw.changes==1);
  puts("lmp8 fablab: lock control, relay compile-off, zero commutations, matrix preserved passed");
}
