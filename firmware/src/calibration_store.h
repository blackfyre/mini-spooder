// The calibration trims in the stock format (design D6): EEPROM bytes 0-7 of the
// 512-byte emulation buffer in the sector at 0x3FB000.
#pragma once

#include "calibration/trim_table.h"
#include "servo/output_gate.h"
#include "servo/servo_bank.h"

class CalibrationStore {
 public:
  // EEPROM.begin(512), decode bytes 0-7, hand the trims to the bank and print the
  // calibration line. Called in setup() before any servo is driven.
  void begin(ms::ServoBank& bank);

  // The gate that leaves `fault` for `safe` once every trim is valid.
  void attachGate(ms::OutputGate& gate) { gate_ = &gate; }

  const ms::TrimTable& table() const { return table_; }

  // Set one trim; allowed in every output mode. An accepted change takes effect
  // on the output at once and is committed before this returns. A change that
  // the flash commit fails to store is not accepted (Rejected, nothing changed).
  ms::TrimSetResult setTrim(int servo, int value);

  // Set all trims to 0, apply and store them. False only when the commit failed.
  bool resetAll();

  // True once after a pass that committed the sector (for the loop statistics).
  bool takeCommitFlag();

 private:
  bool applyChange(const ms::TrimTable& before);
  void printLine() const;

  ms::TrimTable table_;
  ms::ServoBank* bank_ = nullptr;
  ms::OutputGate* gate_ = nullptr;
  bool committed_ = false;
};
