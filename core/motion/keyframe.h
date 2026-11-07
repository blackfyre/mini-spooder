// Keyframe format and the stock motion catalogue (motion-playback D2).
// Hardware-free: no Arduino or ESP8266 headers.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ms {

// One keyframe: an angle per servo index 0-7 (before calibration) and a duration.
struct Keyframe {
  uint8_t angle[8];
  uint16_t duration_ms;
};

struct MotionTable {
  const Keyframe* frames;
  uint8_t count;
};

// A motion: one or two tables played in sequence, or a direct pose (99, 100).
struct Motion {
  uint8_t id;
  const char* name;
  uint8_t table_count;
  MotionTable tables[2];
  const uint8_t* direct_pose;  // set for 99 and 100 only
};

// The catalogue: ids 1-15, 99 and 100, in that order.
constexpr size_t kMotionCount = 17;
const Motion* motions();

// The catalogue entry for an id, or nullptr for any other id.
const Motion* findMotion(int id);

// The direct-pose routine: one servo per tick, then 30 hold ticks (38 in all).
constexpr int kDirectPoseHoldTicks = 30;
constexpr int kDirectPoseTicks = 8 + kDirectPoseHoldTicks;

// Ticks of one keyframe of the given duration: max(1, floor(T / 10)).
inline uint32_t keyframeTicks(uint32_t duration_ms) {
  return duration_ms < 10 ? 1 : duration_ms / 10;
}

// Ticks of a whole motion (without an enable tick).
uint32_t motionTicks(const Motion& motion);

}  // namespace ms
