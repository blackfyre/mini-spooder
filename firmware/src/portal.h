// The web portal (web-portal): ESP8266WebServer on port 80, its routes passed
// to the hardware-free handler in core/portal/, and the handler's action carried
// out through motion-playback, servo-output and servo-calibration.
#pragma once

#include <stdint.h>

#include "calibration_store.h"
#include "servo/output_gate.h"

namespace portal {

// Starts the server. Called once in setup(), after the output gate exists.
void begin(ms::OutputGate& gate, CalibrationStore& calibration);

// The HTTP step of the main loop: serves what has arrived; never waits for a motion.
void poll(uint32_t nowMs);

}  // namespace portal
