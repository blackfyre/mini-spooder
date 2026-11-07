// Diagnostic trace for on-robot debugging, compiled only in the robot-trace env
// (MINI_SPOODER_TRACE): access point station joins and leaves, and a heartbeat
// line every 5 s. Not part of any OpenSpec change's specified console output.
#pragma once

#include <stdint.h>

#include "servo/output_gate.h"
#include "system/loop_stats.h"

namespace trace {

void begin(const ms::OutputGate& gate, const ms::LoopStats& stats);
void poll(uint32_t nowMs);

}  // namespace trace
