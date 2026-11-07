// web-portal's calls into motion-playback (portal_motion.h), on the player's
// binding with origin `portal` (motion_service::portal()).
#include "portal_motion.h"

#include "motion/keyframe.h"
#include "motion_service.h"

namespace portal_motion {

bool request(int32_t id) { return motion_service::portal().request(static_cast<int>(id)); }

bool playKeyframe(const uint8_t angles[ms::kServoCount], uint16_t durationMs) {
  int a[ms::kServoCount];
  for (int i = 0; i < ms::kServoCount; ++i) a[i] = angles[i];
  return motion_service::portal().playKeyframe(a, durationMs) != 0;
}

void stop() { motion_service::portal().stop(); }

bool busy() {
  const ms::PlayerState s = motion_service::state();
  return s.activity != ms::PlayerActivity::Idle || s.pendingMotion != 0 || s.pendingKeyframe != 0;
}

bool isMotionId(int32_t id) { return ms::findMotion(static_cast<int>(id)) != nullptr; }

}  // namespace portal_motion
