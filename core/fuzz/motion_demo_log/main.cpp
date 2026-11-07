// Host log generator for tools/check_motion_log.py (motion-playback task 4.2):
// runs the motion-demo sequence on the host player, over the real output gate
// (check build, no drive-link session), on a simulated clock with a chance to
// run every 1 ms, and prints the log the demo build prints on the robot.
//
// Built by tools/test_check_motion_log.py (see SOURCES there) with the host
// compiler, from this file and core/motion/ and core/servo/ sources.
#include <stdio.h>

#include "motion/demo_script.h"
#include "motion/gate_output.h"
#include "motion/player.h"
#include "servo/output_gate.h"
#include "servo/servo_bank.h"

namespace {

void printLine(void*, const char* line) { printf("%s\n", line); }

void printMode(void*, ms::OutputMode mode, const char* reason) { printf("mode %s %s\n", ms::modeName(mode), reason); }

}  // namespace

int main() {
  ms::ServoBank bank;
  ms::OutputGate gate(bank, true, true);
  gate.addModeListener(printMode, nullptr);
  ms::GateMotionOutput output(gate);
  ms::MotionPlayer player(output);
  ms::MotionDemo demo(player, printLine, nullptr);
  printf("mini-spooder host\n");
  for (uint32_t now = 0; now < 200000 && !demo.done(); ++now) {
    if (now == 1000) demo.go(now);
    player.service(now);
    demo.poll(now);
  }
  return demo.done() ? 0 : 1;
}
