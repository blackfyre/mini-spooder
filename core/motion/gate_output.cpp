#include "motion/gate_output.h"

namespace ms {

bool GateMotionOutput::enable(Origin origin, const uint8_t angles[8]) {
  Pose pose;
  for (int s = 0; s < kServoCount; ++s) pose.angle[s] = angles[s];
  const EnableResult r = gate_.enable(origin, pose);
  if (r.accepted) token_ = r.token;
  return r.accepted;
}

bool GateMotionOutput::write(int servo, int angle) { return gate_.setServo(token_, servo, angle); }

bool GateMotionOutput::active() const { return gate_.mode() == OutputMode::Active; }

int GateMotionOutput::commandedAngle(int servo) const {
  const ServoState s = gate_.bank().state(servo);
  // Every servo has a commanded angle in `active`; the enable pose otherwise.
  return s.hasAngle ? s.angle : kEnablePose.angle[servo];
}

}  // namespace ms
