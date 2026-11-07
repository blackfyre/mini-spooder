// The motion player in the firmware (motion-playback D4): the player on the
// output gate, one binding per caller with its origin fixed, and the loop's
// service call. In the motion-demo build it also runs the demo sequence (D9).
#pragma once

#include <stdint.h>

#include "motion/binding.h"
#include "motion/player.h"
#include "servo/output_gate.h"

namespace motion_service {

// Called once in setup(), after the output gate exists.
void begin(ms::OutputGate& gate);

// Called on every loop pass, after drive-link's receive and failsafe step and
// before the servo driver's poll.
void poll(uint32_t nowMs);

// drive-link's binding (origin `link`) and web-portal's (origin `portal`).
ms::MotionBinding& link();
ms::MotionBinding& portal();

// The player's state report, for the portal and the drive link's telemetry.
ms::PlayerState state();

}  // namespace motion_service
