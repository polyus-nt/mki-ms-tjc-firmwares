#pragma once
#ifndef REACTOR_API_PROTOCOL_H
#define REACTOR_API_PROTOCOL_H
#include "HardwareProtocol.h"

// Расширение Hardware API v2: дополнительный Arduino-шлюз и Mega реактора.
// Индексы с нуля. Числа big-endian; скорости — signed milli-units.
namespace reactor_api {
using namespace hardware_api;
enum {
  kNode=17, kTemp=0x60, kSetSps=0x61, kSetRpm=0x62,
  kStop=0x63, kEnable=0x64, kMicro=0x65, kMotor=0x66,
  kMotorConfig=0x67, kTiming=0x68, kStopAll=0x6a,
  kSetU=0x70, kSetI=0x71, kGetU=0x72, kGetI=0x73,
  kGetSetU=0x74, kGetSetI=0x75
};
inline Status validate(const Frame& r) {
  if(r.version!=kVersion) return UNSUPPORTED_VERSION;
  if(r.kind!=kRequest || r.status!=OK) return INVALID_ARGUMENT;
  if(r.node!=kNode) return INVALID_NODE;
  uint8_t count=3,used=0;
  switch(r.command) {
    case kTemp: case kStopAll: count=1; break;
    case kMotor: case kMotorConfig: case kTiming: case kStop:
    case kGetU: case kGetI: case kGetSetU: case kGetSetI: break;
    case kEnable: case kMicro: used=1; break;
    case kSetSps: case kSetRpm: case kSetU: case kSetI: used=4; break;
    default: return UNKNOWN_COMMAND;
  }
  if(r.idx>=count) return INVALID_INDEX;
  if(!zeros(r.data+used,8-used)) return INVALID_ARGUMENT;
  uint32_t value=readU32(r.data);
  if(r.command==kEnable && r.data[0]>1) return INVALID_ARGUMENT;
  if(r.command==kMicro && r.data[0]!=4 && r.data[0]!=8 &&
     r.data[0]!=16 && r.data[0]!=32) return INVALID_ARGUMENT;
  if(r.command==kSetU && value>6000) return INVALID_ARGUMENT;
  if(r.command==kSetI && value>50000) return INVALID_ARGUMENT;
  if((r.command==kSetSps || r.command==kSetRpm) &&
     (value<=0x7fffffffUL ? value : 0u-value)>7000000UL) return INVALID_ARGUMENT;
  return OK;
}
}
#endif
