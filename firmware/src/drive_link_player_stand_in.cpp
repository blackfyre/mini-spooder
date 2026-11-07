// Weak stand-ins for motion playback until motion-playback is merged (see
// drive_link_player.h). They reject every request, so the drive link runs, its
// failsafe applies and its telemetry reports nothing playing, but no motion plays.
#include "drive_link_player.h"

namespace drive_link_player {

__attribute__((weak)) bool request(uint8_t) { return false; }
__attribute__((weak)) void stop() {}
__attribute__((weak)) uint8_t currentMotion() { return 0; }
__attribute__((weak)) uint8_t pendingMotion() { return 0; }

}  // namespace drive_link_player
