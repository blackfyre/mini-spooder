// The drive link's view of motion playback (drive-link, design D5): the calls of
// motion-playback's binding with origin `link`, and its state report.
//
// Defined in drive_link_player.cpp on motion-playback's link binding.
#pragma once

#include <stdint.h>

namespace drive_link_player {

// motion-playback's request(id) with origin link: accepted by its catalogue?
bool request(uint8_t motion);
// motion-playback's stop().
void stop();
// The state report: the current motion (0 when idle or playing a keyframe) and
// the pending motion (0 when none).
uint8_t currentMotion();
uint8_t pendingMotion();

}  // namespace drive_link_player
