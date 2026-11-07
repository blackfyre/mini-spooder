// The player's servo-output interface on the output gate (motion-playback D4):
// keeps the writer token that an accepted enable request returns and passes it
// with every write, so the player sees only accepted or refused.
#pragma once

#include "motion/player.h"
#include "servo/output_gate.h"

namespace ms {

class GateMotionOutput final : public MotionOutput {
 public:
  explicit GateMotionOutput(OutputGate& gate) : gate_(gate), token_(0) {}

  bool enable(Origin origin, const uint8_t angles[8]) override;
  bool write(int servo, int angle) override;
  bool active() const override;
  int commandedAngle(int servo) const override;

 private:
  OutputGate& gate_;
  WriterToken token_;
};

}  // namespace ms
