// Stock pulse mapping, per-servo safe ranges and the enable pose (servo-output).
// Hardware-free: no Arduino or ESP8266 headers.
#pragma once

#include <stdint.h>

namespace ms {

constexpr int kServoCount = 8;

// The accepted range of commanded angles, in whole degrees.
constexpr int kAngleMin = 0;
constexpr int kAngleMax = 180;

// One pulse every 20 ms per driven servo.
constexpr uint32_t kServoPeriodUs = 20000;

// A complete pose: one commanded angle per servo index 0-7. Signed so that an
// out-of-range request (such as -1) can be represented and rejected.
struct Pose {
  int16_t angle[kServoCount];
};

// A per-servo range of output angles, 1 <= low < high <= 180.
struct SafeRange {
  uint8_t low;
  uint8_t high;
};

// The defaults of servo-output: the per-servo envelope of the stock motion tables
// and the stock zero pose, widened by 10 degrees each side, within 1-180.
constexpr SafeRange kDefaultSafeRange[kServoCount] = {
    {20, 180}, {35, 145}, {35, 145}, {35, 160},
    {35, 160}, {35, 145}, {35, 145}, {20, 145},
};

// The table in force. Equal to the defaults until the hardware check (task 6.4)
// measures the mechanical stops; each measured range replaces its default here.
constexpr SafeRange kSafeRange[kServoCount] = {
    {20, 180}, {35, 145}, {35, 145}, {35, 160},
    {35, 160}, {35, 145}, {35, 145}, {20, 145},
};

// The enable pose: the stock zero pose that stock attaches at boot. It is the boot
// pose (origin `boot`, main.cpp) and what the direct servo command enables with.
constexpr Pose kEnablePose = {{135, 45, 135, 45, 45, 135, 45, 135}};

inline bool isServoIndex(int servo) { return servo >= 0 && servo < kServoCount; }

inline bool isAcceptedAngle(int angle) { return angle >= kAngleMin && angle <= kAngleMax; }

bool isAcceptedPose(const Pose& pose);

// The output angle: commanded angle plus trim, limited to the given range.
int limitedAngle(const SafeRange& range, int commanded, int trim);

// The output angle with the table in force (kSafeRange) for that servo.
int outputAngle(int servo, int commanded, int trim);

// The stock conversion, Arduino map(out, 1, 180, 400, 2400) with truncating
// integer division: 400 + ((out - 1) * 2000) / 179.
uint16_t pulseWidthUs(int out);

}  // namespace ms
