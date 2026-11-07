#include "servo/servo_bank.h"

namespace ms {

ServoBank::ServoBank() : hook_(nullptr), hookCtx_(nullptr) {
  for (int i = 0; i < kServoCount; ++i) {
    state_[i] = ServoState{false, false, 0};
    trim_[i] = 0;
  }
}

void ServoBank::setReleaseHook(ReleaseHook hook, void* ctx) {
  hook_ = hook;
  hookCtx_ = ctx;
}

bool ServoBank::setServo(int servo, int angle) {
  if (!isServoIndex(servo) || !isAcceptedAngle(angle)) return false;
  state_[servo] = ServoState{true, true, static_cast<uint8_t>(angle)};
  return true;
}

bool ServoBank::setPose(const Pose& pose) {
  if (!isAcceptedPose(pose)) return false;
  for (int i = 0; i < kServoCount; ++i) {
    state_[i] = ServoState{true, true, static_cast<uint8_t>(pose.angle[i])};
  }
  return true;
}

void ServoBank::releaseOne(int servo) {
  if (!state_[servo].driven) return;
  state_[servo].driven = false;
  if (hook_ != nullptr) hook_(hookCtx_, servo);
}

bool ServoBank::release(int servo) {
  if (!isServoIndex(servo)) return false;
  releaseOne(servo);
  return true;
}

void ServoBank::releaseAll() {
  for (int i = 0; i < kServoCount; ++i) releaseOne(i);
}

void ServoBank::setTrim(int servo, int trim) {
  if (!isServoIndex(servo)) return;
  trim_[servo] = trim;
}

ServoState ServoBank::state(int servo) const { return state_[servo]; }

bool ServoBank::anyDriven() const {
  for (int i = 0; i < kServoCount; ++i) {
    if (state_[i].driven) return true;
  }
  return false;
}

int ServoBank::outputAngleOf(int servo) const {
  return outputAngle(servo, state_[servo].angle, trim_[servo]);
}

uint16_t ServoBank::widthUs(int servo) const {
  if (!state_[servo].driven) return 0;
  return pulseWidthUs(outputAngleOf(servo));
}

}  // namespace ms
