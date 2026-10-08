#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/gateway/Gateway.h"

using hardware_api::Frame;
using tjc_gateway::Gateway;

static Frame request(uint8_t node = 14, uint16_t id = 5) {
  Frame f = {};
  f.version = hardware_api::kVersion; f.kind = 1; f.node = node; f.tx_id = id;
  f.command = node == 13 ? 0x20 : 0x32;
  f.idx = 0; f.data[0] = node == 14 ? 100 : 0;
  return f;
}

static void bus(Gateway& g, const Frame& f, uint32_t now) {
  uint8_t bytes[hardware_api::kFrameSize]; hardware_api::encode(f, bytes);
  for (uint8_t i = 0; i < hardware_api::kFrameSize; ++i) g.receiveBus(bytes[i], now);
}

static void arduino(Gateway& g, const Frame& f, uint32_t now) {
  uint8_t bytes[hardware_api::kFrameSize]; hardware_api::encode(f, bytes);
  for (uint8_t i = 0; i < hardware_api::kFrameSize; ++i) g.receiveArduino(bytes[i], now);
}

static void ready(Gateway& g, uint32_t now = 250) {
  assert(g.takeCancelArduino());
  g.tick(now); assert(g.state() == Gateway::Idle);
}

static void vector_round_trip() {
  // Transparent temperature exchange; independent frozen vectors are tested across repositories.
  uint8_t in[hardware_api::kFrameSize]; Frame req = request(13,1); hardware_api::encode(req,in);
  uint8_t out[hardware_api::kFrameSize]; Frame reply = hardware_api::response(req); reply.data[2]=9; reply.data[3]=0xF6; reply.data[4]=3; hardware_api::encode(reply,out);
  Gateway g(13); ready(g);
  for (uint8_t i = 0; i < hardware_api::kFrameSize; ++i) g.receiveBus(in[i], 251 + i * 8);
  Frame f; assert(g.takeArduinoRequest(f));
  uint8_t raw[hardware_api::kFrameSize]; hardware_api::encode(f, raw);
  assert(memcmp(in, raw, hardware_api::kFrameSize) == 0);
  assert(!g.takeArduinoRequest(f));
  for (uint8_t i = 0; i < hardware_api::kFrameSize; ++i) g.receiveArduino(out[i], 500 + i * 5);
  assert(g.takeHubResponse(f)); hardware_api::encode(f, raw);
  assert(memcmp(out, raw, hardware_api::kFrameSize) == 0);
  assert(!g.takeHubResponse(f));
  g.hubResponseSent(750); assert(g.state() == Gateway::Idle);
}

static void errors_and_transparency() {
  Gateway g(14); ready(g); Frame f = request(), received;
  f.node = 13; bus(g, f, 251);
  assert(!g.takeArduinoRequest(received));
  assert(g.takeHubResponse(received)); assert(received.status == 0x0B);
  g.hubResponseSent(260);
  f = request(); f.version = 9; bus(g, f, 261);
  assert(g.takeHubResponse(received)); assert(received.status == 0x08);
  g.hubResponseSent(270);
  f = request(); f.status = 1; bus(g, f, 271);
  assert(g.takeHubResponse(received)); assert(received.status == 2);
  g.hubResponseSent(280);
  f = request(); f.command = 0xFE; f.idx = 0xFD;
  const uint8_t special[8] = {3,12,13,10,0xA5,0x5A,0xFF,0};
  memcpy(f.data, special, 8); bus(g, f, 281);
  assert(g.takeArduinoRequest(received));
  // Gateway does not impose an obsolete hardware map on downstream firmware.
  assert(received.command == 0xFE && received.idx == 0xFD);
  assert(memcmp(received.data, special, 8) == 0);
}

static void correlation() {
  Gateway g(14); ready(g); Frame f = request(), received;
  bus(g, f, 251); assert(g.takeArduinoRequest(received));
  const Frame success = hardware_api::response(f);
  for (uint8_t field = 0; field < 7; ++field) {
    Frame wrong = success;
    switch (field) {
      case 0: ++wrong.tx_id; break;
      case 1: wrong.node = 13; break;
      case 2: ++wrong.command; break;
      case 3: ++wrong.idx; break;
      case 4: wrong.kind = 1; break;
      case 5: wrong.version = 1; break;
      case 6: ++wrong.session_id; break;
    }
    arduino(g, wrong, 300 + field * hardware_api::kFrameSize);
    assert(!g.takeHubResponse(received));
  }
  Frame malformed = success; malformed.status = 1; malformed.data[0] = 1;
  arduino(g, malformed, 450); assert(!g.takeHubResponse(received));
  Frame applied = success; applied.data[0] = 230;
  arduino(g, applied, 500); assert(g.takeHubResponse(received));
  assert(received.data[0] == 230);  // No echo of requested 100.
}

static void timeout_and_late_reply() {
  Gateway g(14); ready(g); Frame f = request(), received;
  bus(g, f, 300); assert(g.takeArduinoRequest(received));
  g.tick(1799); assert(!g.takeHubResponse(received));
  g.tick(1800); assert(g.takeHubResponse(received));
  assert(received.status == 7 && hardware_api::sameRequest(f, received));
  assert(g.takeCancelArduino());
  arduino(g, hardware_api::response(f), 1801);
  assert(!g.takeHubResponse(received));
  g.hubResponseSent(1900); assert(g.state() == Gateway::Recovering);
  arduino(g, hardware_api::response(f), 2100);
  g.tick(2349); assert(g.state() == Gateway::Recovering);
  g.tick(2350); assert(g.state() == Gateway::Idle);
  Frame next = request(14, 6); bus(g, next, 2351);
  assert(g.takeArduinoRequest(received));
  arduino(g, hardware_api::response(f), 2400);
  assert(!g.takeHubResponse(received));
  arduino(g, hardware_api::response(next), 2500);
  assert(g.takeHubResponse(received)); assert(received.tx_id == 6);
}

static void busy_is_deferred() {
  Gateway g(14); ready(g); Frame first = request(), second = request(14, 6), f;
  bus(g, first, 300); assert(g.takeArduinoRequest(f));
  bus(g, second, 310);
  assert(!g.takeHubResponse(f)); assert(!g.takeArduinoRequest(f));
  arduino(g, hardware_api::response(first), 400);
  assert(g.takeHubResponse(f)); assert(f.tx_id == first.tx_id && f.status == 0);
  g.hubResponseSent(600);
  assert(g.takeHubResponse(f)); assert(f.tx_id == second.tx_id && f.status == 6);
  g.hubResponseSent(800); assert(g.state() == Gateway::Idle);
}

static void crc_truncation_and_wrap() {
  Gateway g(14); ready(g); Frame f = request(), result;
  uint8_t bytes[hardware_api::kFrameSize]; hardware_api::encode(f, bytes); bytes[hardware_api::kFrameSize - 1] ^= 1;
  for (uint8_t i = 0; i < hardware_api::kFrameSize; ++i) g.receiveBus(bytes[i], 300);
  assert(!g.takeArduinoRequest(result)); assert(!g.takeHubResponse(result));
  hardware_api::encode(f, bytes);
  for (uint8_t i = 0; i < 10; ++i) g.receiveBus(bytes[i], 400);
  g.tick(650); bus(g, f, 651); assert(g.takeArduinoRequest(result));
  g.overflow(700); assert(g.takeCancelArduino());
  assert(g.takeHubResponse(result)); assert(result.status == 12);
  g.hubResponseSent(800); g.tick(1050); assert(g.state() == Gateway::Idle);

  Gateway wrap(14, UINT32_MAX - 99); ready(wrap, 150);
  bus(wrap, f, UINT32_MAX - 999); assert(wrap.takeArduinoRequest(result));
  wrap.tick(499); assert(!wrap.takeHubResponse(result));
  wrap.tick(500); assert(wrap.takeHubResponse(result)); assert(result.status == 7);
}

int main() {
  vector_round_trip(); errors_and_transparency(); correlation();
  timeout_and_late_reply(); busy_is_deferred(); crc_truncation_and_wrap();
  puts("gateway: 6 suites passed (both nodes, fixed vectors, CRC, correlation, timeout, late replies, BUSY, overflow, millis wrap)");
}
