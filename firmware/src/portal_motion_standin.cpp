// STAND-IN for motion-playback's portal binding (see portal_motion.h). Remove
// this file when motion-playback's glue provides portal_motion::*. Until then
// no motion or keyframe plays: requests are rejected, nothing is ever busy, and
// the catalogue is the stock id set that motion-playback's spec defines.
#include "portal_motion.h"

namespace portal_motion {

bool request(int32_t) { return false; }

bool playKeyframe(const uint8_t[ms::kServoCount], uint16_t) { return false; }

void stop() {}

bool busy() { return false; }

bool isMotionId(int32_t id) { return (id >= 1 && id <= 15) || id == 99 || id == 100; }

}  // namespace portal_motion
