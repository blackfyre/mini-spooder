// A caller's binding to the player with its origin fixed (motion-playback D4):
// the firmware gives drive-link one with origin `link` and web-portal one with
// origin `portal`, so that they call request(id) without naming an origin.
#pragma once

#include <stdint.h>

#include "motion/player.h"

namespace ms {

class MotionBinding {
 public:
  MotionBinding(MotionPlayer& player, Origin origin) : player_(player), origin_(origin) {}

  bool request(int id) { return player_.request(id, origin_); }
  uint32_t playKeyframe(const int angles[8], int32_t durationMs) {
    return player_.playKeyframe(angles, durationMs, origin_);
  }
  bool keyframeEnded(uint32_t id) const { return player_.keyframeEnded(id); }
  void stop() { player_.stop(); }
  PlayerState state() const { return player_.state(); }
  Origin origin() const { return origin_; }

 private:
  MotionPlayer& player_;
  Origin origin_;
};

}  // namespace ms
