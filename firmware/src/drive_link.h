// The drive link's firmware glue (drive-link, design D1, D4, D6, D7): the access
// point, the UDP drive port and the two loop steps over core/drive_link's Link.
#pragma once

#include <stdint.h>

#include "servo/output_gate.h"

namespace drive_link_glue {

// Starts the access point and binds the drive port. Called once in setup().
void begin(ms::OutputGate& gate);

// The receive and failsafe step: drains the drive port and applies each result,
// then the pass's final judge. Called every pass before motion playback's step.
void receive();

// The telemetry step: sends a due telemetry frame to the locked sender. Called
// every pass after motion playback's step.
void sendTelemetry();

// Whether a session is live: the session state that the web portal's lock reads.
// It changes only in receive(), together with the output gate's session calls.
bool live();

}  // namespace drive_link_glue
