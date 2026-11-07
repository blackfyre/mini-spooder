// mini-spooder firmware entry point (design D4): start-up diagnostics, then one
// cooperative loop of poll services that returns to the core on every pass.
// There is no boot pose: the servos stay still until a caller asks the output gate
// for output with a complete pose.
#include <Arduino.h>
#include <user_interface.h>

#include "calibration_store.h"
#include "diag.h"
#include "drive_link.h"  // drive-link: access point, UDP drive port, session report
#include "servo/output_gate.h"
#include "servo/servo_bank.h"
#include "servo_driver.h"
#include "system/loop_stats.h"
#include "system/reset_cause.h"

#ifdef MINI_SPOODER_CHECK_CONSOLE
#include "check_console.h"
#endif

#ifndef MINI_SPOODER_VERSION
#define MINI_SPOODER_VERSION "unknown"
#endif

#ifdef MINI_SPOODER_CHECK_BUILD
constexpr bool kCheckBuild = true;
#else
constexpr bool kCheckBuild = false;
#endif

namespace {

ms::ServoBank bank;
ServoDriver driver;
CalibrationStore calibration;
ms::LoopStats loopStats;
ms::OutputGate* gate = nullptr;

#ifdef MINI_SPOODER_CHECK_CONSOLE
CheckConsole console;
#endif

void printModeLine(void*, ms::OutputMode mode, const char* reason) {
  char line[48];
  snprintf(line, sizeof line, "mode %s %s", ms::modeName(mode), reason);
  diag::line(line);
}

}  // namespace

void setup() {
  diag::begin();
  diag::line("mini-spooder " MINI_SPOODER_VERSION);

  const rst_info* rst = ESP.getResetInfoPtr();
  char line[64];
  ms::formatResetLine(line, sizeof line, rst->reason, rst->exccause, rst->epc1);
  diag::line(line);

  calibration.begin(bank);  // prints the calibration line
  driver.begin(bank);       // touches no pin until a servo is first driven

  static ms::OutputGate outputGate(bank, calibration.table().allValid(), kCheckBuild);
  gate = &outputGate;
  gate->addModeListener(printModeLine, nullptr);
  calibration.attachGate(outputGate);
  if (gate->mode() == ms::OutputMode::Fault) {
    gate->modeLine(line, sizeof line);
    diag::line(line);
  }

#ifdef MINI_SPOODER_CHECK_CONSOLE
  console.begin(outputGate, calibration, loopStats);
#endif

  // drive-link: the access point and the drive port (no output change).
  drive_link_glue::begin(outputGate);

  diag::flush();
}

void loop() {
  const uint32_t startUs = micros();
  const uint32_t nowMs = millis();

  // drive-link's receive and failsafe step: before motion playback's step, so a
  // request, stop or failsafe found in this pass applies before the player moves.
  drive_link_glue::receive();

  // Later changes add their poll services here (design D4).
#ifdef MINI_SPOODER_CHECK_CONSOLE
  console.poll(nowMs);
#endif
  // drive-link's telemetry step: after motion playback's step, so a frame reports
  // the player's state after this pass's tick.
  drive_link_glue::sendTelemetry();

  driver.poll(nowMs);  // after the services, so their commands reach the pins this pass
  diag::poll();

  loopStats.record(micros() - startUs, calibration.takeCommitFlag());
}
