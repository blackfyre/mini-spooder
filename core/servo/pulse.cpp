#include "servo/pulse.h"

namespace ms {

bool isAcceptedPose(const Pose& pose) {
  for (int i = 0; i < kServoCount; ++i) {
    if (!isAcceptedAngle(pose.angle[i])) return false;
  }
  return true;
}

int limitedAngle(const SafeRange& range, int commanded, int trim) {
  const int out = commanded + trim;
  if (out < range.low) return range.low;
  if (out > range.high) return range.high;
  return out;
}

int outputAngle(int servo, int commanded, int trim) {
  return limitedAngle(kSafeRange[servo], commanded, trim);
}

uint16_t pulseWidthUs(int out) {
  // Callers pass output angles, which the safe range keeps within 1-180.
  return static_cast<uint16_t>(400 + ((out - 1) * 2000) / 179);
}

}  // namespace ms
