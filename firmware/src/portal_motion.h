// web-portal's calls into motion-playback, through its binding with origin
// `portal` (motion-playback design D4, D10). motion-playback is a separate
// OpenSpec change: until its glue exists, portal_motion_standin.cpp implements
// these calls inertly. The integrator replaces that file with the real binding.
#pragma once

#include <stdint.h>

#include "servo/pulse.h"

namespace portal_motion {

// request(id) with origin portal; true when the player accepted the id.
bool request(int32_t id);

// play_keyframe(angles, durationMs) with origin portal; true when accepted
// (a non-zero keyframe identifier).
bool playKeyframe(const uint8_t angles[ms::kServoCount], uint16_t durationMs);

// stop(): ends the current motion or keyframe and discards the pending request.
void stop();

// The player's state report: a motion or keyframe is playing or pending.
bool busy();

// Whether id is in the catalogue (motions(): stock ids 1-15, 99 and 100).
bool isMotionId(int32_t id);

}  // namespace portal_motion
