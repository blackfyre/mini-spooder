// The check build's line console on UART0 (design D7). Built only in robot-check;
// the robot env's build_src_filter excludes check_console.*.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "servo/output_gate.h"
#include "system/loop_stats.h"

class CalibrationStore;

class CheckConsole {
 public:
  void begin(ms::OutputGate& gate, CalibrationStore& calibration, const ms::LoopStats& stats);

  // Reads the bytes waiting on UART0 and runs each complete line. Called every pass.
  void poll(uint32_t nowMs);

 private:
  void run(const char* line, size_t len);
  void status();

  ms::OutputGate* gate_ = nullptr;
  CalibrationStore* calibration_ = nullptr;
  const ms::LoopStats* stats_ = nullptr;
  ms::WriterToken token_ = 0;
  char line_[96] = {};
  size_t len_ = 0;
  bool overflow_ = false;
};
