#pragma once
#include "../common/HardwareProtocol.h"
namespace tjc_lmp8 {
const uint8_t kBusAddress = 16, kHubAddress = 12;
const uint8_t kLockPin = 1;
const uint8_t kRelayPins[6] = {2, 3, 4, 5, 6, 7};
// Реле и матрица ниже — сохранённая заготовка химреактора. Фаблаб их не включает.
const uint32_t kBreakMs = 250;
// Полярность внешнего драйвера замков обязательно проверить без нагрузки.
// По умолчанию, как у реле: HIGH = отключено, LOW = включено.
const bool kLockActiveLow = true;
const uint8_t kGetDoorLock = 0x43, kGetRelayLevel = 0x50, kSetRelayLevel = 0x51;
inline uint8_t relayMask(uint8_t level) {
  const uint8_t masks[11] = {0, 49, 51, 55, 63, 33, 17, 35, 39, 47, 1};
  return level <= 10 ? masks[level] : 0;
}
}
