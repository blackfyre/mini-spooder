// The drive link's view of motion playback (drive-link, design D5): the calls of
// motion-playback's binding with origin `link`, and its state report.
//
// INTEGRATION: motion-playback is implemented in parallel. Until it is merged,
// drive_link_player_stand_in.cpp defines these as weak stand-ins (every request
// rejected, nothing playing). motion-playback's glue defines them strongly on its
// player (`request(id)` with origin link, `stop()`, and the current and pending
// motion of its state report), which replaces the stand-ins at link time; the
// stand-in file can then be deleted.
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
