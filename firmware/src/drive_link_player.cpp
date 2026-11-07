// The drive link's view of motion playback (drive_link_player.h), on
// motion-playback's binding with origin `link` (motion_service::link()).
#include "drive_link_player.h"

#include "motion_service.h"

namespace drive_link_player {

bool request(uint8_t motion) { return motion_service::link().request(motion); }
void stop() { motion_service::link().stop(); }
uint8_t currentMotion() { return motion_service::state().motion; }
uint8_t pendingMotion() { return motion_service::state().pendingMotion; }

}  // namespace drive_link_player
