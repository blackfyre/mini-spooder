// The hardware-free servo model (servo-output): per servo, driven or released,
// the last commanded angle and the trim; the wanted pulse width; the state report.
#pragma once

#include <stdint.h>

#include "servo/pulse.h"

namespace ms {

struct ServoState {
  bool driven;        // pulses wanted
  bool hasAngle;      // a commanded angle exists (false after a reset)
  uint8_t angle;      // last commanded angle without the trim, valid when hasAngle
};

class ServoBank {
 public:
  // Called within a release call, once per servo that was driven and is now
  // released (the driver's release phase 1).
  using ReleaseHook = void (*)(void* ctx, int servo);

  ServoBank();

  void setReleaseHook(ReleaseHook hook, void* ctx);

  // Drive one servo at a commanded angle. Rejected (false, nothing changed) for an
  // index outside 0-7 or an angle outside 0-180.
  bool setServo(int servo, int angle);

  // Drive every servo at a complete pose. Rejected (false, nothing changed) when
  // any angle is outside 0-180.
  bool setPose(const Pose& pose);

  // Release one servo. False for an index outside 0-7. A released servo is left
  // as it is, and the hook is not called for it.
  bool release(int servo);
  void releaseAll();

  // The trim applied before the safe range. Any value is held; validity is the
  // trim table's concern.
  void setTrim(int servo, int trim);
  int trim(int servo) const { return trim_[servo]; }

  ServoState state(int servo) const;
  bool anyDriven() const;

  // The output angle and pulse width for a servo with a commanded angle.
  int outputAngleOf(int servo) const;
  uint16_t widthUs(int servo) const;  // 0 when released

 private:
  void releaseOne(int servo);

  ServoState state_[kServoCount];
  int trim_[kServoCount];
  ReleaseHook hook_;
  void* hookCtx_;
};

}  // namespace ms
