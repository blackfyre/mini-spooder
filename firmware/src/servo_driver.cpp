#include "servo_driver.h"

#include <Arduino.h>
#include <core_esp8266_waveform.h>

namespace {

// Servo index 0-7 to GPIO: the stock start-up attach order, fixed at build time.
constexpr uint8_t kServoPins[ms::kServoCount] = {15, 13, 12, 14, 2, 4, 5, 16};

// Phase 2 runs in the first pass at least one full period after phase 1, when the
// pulse in progress at phase 1 has ended and no new one has started.
constexpr uint32_t kReleaseSettleUs = ms::kServoPeriodUs;

}  // namespace

void ServoDriver::begin(ms::ServoBank& bank) {
  bank_ = &bank;
  bank.setReleaseHook(&ServoDriver::onRelease, this);
}

void ServoDriver::onRelease(void* ctx, int servo) {
  static_cast<ServoDriver*>(ctx)->releasePhase1(servo);
}

void ServoDriver::releasePhase1(int servo) {
  if (state_[servo] != PinState::Running) return;
  // A zero high time: the pulse in progress completes at its full width, and the
  // phase-locked generator starts no further high pulse from the next period.
  startWaveform(kServoPins[servo], 0, ms::kServoPeriodUs, 0);
  releasedAtUs_[servo] = micros();
  state_[servo] = PinState::Releasing;
}

void ServoDriver::startOrRetime(int servo, uint16_t widthUs) {
  const uint8_t pin = kServoPins[servo];
  if (!generatorSelected_) {
    // The phase-locked generator applies a new timing, including a zero high
    // time, at the next period without truncating the pulse in progress, and
    // does not wait for other pins' changes (design D5, task 1.1).
    enablePhaseLockedWaveform();
    generatorSelected_ = true;
  }
  if (state_[servo] == PinState::Untouched) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  // A new waveform starts high with this width; a running one (Running or
  // Releasing) takes it from its next period.
  startWaveform(pin, widthUs, ms::kServoPeriodUs - widthUs, 0);
  widthUs_[servo] = widthUs;
  state_[servo] = PinState::Running;
}

void ServoDriver::poll(uint32_t /*nowMs*/) {
  if (bank_ == nullptr) return;
  const uint32_t nowUs = micros();
  for (int i = 0; i < ms::kServoCount; ++i) {
    const uint16_t wanted = bank_->widthUs(i);  // 0 when released
    switch (state_[i]) {
      case PinState::Untouched:
        if (wanted != 0) startOrRetime(i, wanted);
        break;
      case PinState::Running:
        if (wanted == 0) {
          releasePhase1(i);  // the hook normally did this within the release call
        } else if (wanted != widthUs_[i]) {
          startOrRetime(i, wanted);
        }
        break;
      case PinState::Releasing:
        if (wanted != 0) {
          startOrRetime(i, wanted);
        } else if (static_cast<uint32_t>(nowUs - releasedAtUs_[i]) >= kReleaseSettleUs) {
          stopWaveform(kServoPins[i]);
          digitalWrite(kServoPins[i], LOW);
          state_[i] = PinState::Untouched;
        }
        break;
    }
  }
}
