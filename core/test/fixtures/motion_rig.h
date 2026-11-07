// A recording servo-output model and a simulated clock for the motion player's
// host tests (motion-playback task 2.3).
#pragma once

#include <stdint.h>

#include <vector>

#include "motion/player.h"
#include "motion/stock_motions.h"

namespace rig {

struct Write {
  uint32_t t;
  int servo;
  int angle;
};

struct EnableRequest {
  uint32_t t;
  ms::Origin origin;
  uint8_t angle[8];
};

// Commanded angles, a mode, a token that goes stale on the safe state, and
// refusal of an enable request or a write on demand.
class Model : public ms::MotionOutput {
 public:
  uint32_t now = 0;
  bool isActive = false;
  bool refuseEnable = false;
  bool refuseWrites = false;
  uint32_t token = 1;
  uint32_t held = 0;
  int commanded[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  std::vector<Write> writes;
  std::vector<EnableRequest> enables;
  int refusedWrites = 0;

  bool enable(ms::Origin origin, const uint8_t angles[8]) override {
    EnableRequest e{now, origin, {}};
    for (int s = 0; s < 8; ++s) e.angle[s] = angles[s];
    enables.push_back(e);
    if (refuseEnable) return false;
    for (int s = 0; s < 8; ++s) commanded[s] = angles[s];
    isActive = true;
    held = token;
    return true;
  }
  bool write(int servo, int angle) override {
    if (refuseWrites || !isActive || held != token) {
      ++refusedWrites;
      return false;
    }
    writes.push_back(Write{now, servo, angle});
    commanded[servo] = angle;
    return true;
  }
  bool active() const override { return isActive; }
  int commandedAngle(int servo) const override { return commanded[servo]; }

  // Test set-up: output already active at a pose, with a current token held by
  // nobody yet.
  void activeAt(const uint8_t pose[8]) {
    isActive = true;
    for (int s = 0; s < 8; ++s) commanded[s] = pose[s];
  }
  void enterSafe() {
    isActive = false;
    ++token;
  }
  // servo-output's direct servo command: from idle or safe it enables with the
  // zero pose first; the token is not renewed.
  void directServo(int servo, int angle) {
    if (!isActive) {
      for (int s = 0; s < 8; ++s) commanded[s] = ms::stock::kZeroPose[s];
      isActive = true;
    }
    commanded[servo] = angle;
  }
  bool poseIs(const uint8_t pose[8]) const {
    for (int s = 0; s < 8; ++s) {
      if (commanded[s] != pose[s]) return false;
    }
    return true;
  }
};

struct Rig {
  Model m;
  ms::MotionPlayer p{m};
  std::vector<ms::RunRecord> runs;
  std::vector<uint32_t> tickTimes;  // every tick that ran, an enable tick included
  uint32_t now = 0;

  Rig() { p.setRunListener(&Rig::onRun, this); }

  static void onRun(void* ctx, const ms::RunRecord& r) { static_cast<Rig*>(ctx)->runs.push_back(r); }

  // One chance to run at time t.
  void at(uint32_t t) {
    now = t;
    m.now = t;
    const uint32_t before = p.tickCount();
    p.service(t);
    if (p.tickCount() != before) tickTimes.push_back(t);
  }
  // A chance to run every 1 ms from `from` to `to`, inclusive.
  void every1ms(uint32_t from, uint32_t to) {
    for (uint32_t t = from; t <= to; ++t) at(t);
  }
  // Every 1 ms from now + 1 until the player is idle with nothing pending (or a limit).
  void untilIdle(uint32_t limit = 60000) {
    const uint32_t end = now + limit;
    while (now < end) {
      at(now + 1);
      const ms::PlayerState s = p.state();
      if (s.activity == ms::PlayerActivity::Idle && s.pendingMotion == 0 && s.pendingKeyframe == 0) return;
    }
  }
  size_t writesBetween(uint32_t from, uint32_t to) const {
    size_t n = 0;
    for (const Write& w : m.writes) {
      if (w.t >= from && w.t <= to) ++n;
    }
    return n;
  }
};

}  // namespace rig
