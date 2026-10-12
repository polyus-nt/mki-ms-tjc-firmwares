#pragma once
#include "../common/HardwareProtocol.h"
namespace tjc_lmp {
const uint8_t kBusAddress = 16, kHubAddress = 12;
const uint8_t kLockPins[2] = {1, 2};
const uint8_t kVacuumPin = 3;
const uint32_t kOpenMs = 2000;
// Полярность внешнего драйвера замков обязательно проверить без нагрузки.
// Реле МС-ТЮК: HIGH открывает замок, LOW закрывает.
const bool kLockActiveLow = true;
// Пылесос: HIGH включает, LOW выключает; полярность замков другая.
const bool kVacuumActiveLow = false;
// Уровни до GPIO output-mode: замки LOW (закрыты), пылесос LOW (выключен).
inline bool startupHigh(uint8_t ledIndex) {
  if (ledIndex == kLockPins[0] - 1 || ledIndex == kLockPins[1] - 1) return !kLockActiveLow;
  if (ledIndex == kVacuumPin - 1) return kVacuumActiveLow;
  return false; // У обычной лампочницы только три пользовательских выхода.
}
}
