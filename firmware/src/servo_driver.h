// Drives the eight servo pins from the ServoBank with the core's waveform
// generator (design D5). Never blocks: release runs in two phases.
#pragma once

#include <stdint.h>

#include "servo/servo_bank.h"

class ServoDriver {
 public:
  // Registers the bank's release hook (release phase 1). Touches no pin.
  void begin(ms::ServoBank& bank);

  // Syncs the pins to the bank: starts newly driven servos, retimes changed
  // widths, and completes release phase 2. Called every pass. Release timing
  // uses micros(), recorded in phase 1, rather than the pass's millis() time.
  void poll(uint32_t nowMs);

 private:
  enum class PinState : uint8_t {
    Untouched,  // reset configuration, or low after a completed release
    Running,    // pulses at widthUs_
    Releasing,  // phase 1 done at releasedAtUs_: high time 0, pin goes low
  };

  static void onRelease(void* ctx, int servo);
  void releasePhase1(int servo);
  void startOrRetime(int servo, uint16_t widthUs);

  ms::ServoBank* bank_ = nullptr;
  PinState state_[ms::kServoCount] = {};
  uint16_t widthUs_[ms::kServoCount] = {};
  uint32_t releasedAtUs_[ms::kServoCount] = {};
  bool generatorSelected_ = false;
};
