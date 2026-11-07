#include "calibration_store.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "diag.h"

void CalibrationStore::begin(ms::ServoBank& bank) {
  bank_ = &bank;
  EEPROM.begin(512);  // the stock firmware's buffer size
  uint8_t bytes[ms::kTrimBytes];
  for (int i = 0; i < ms::kTrimBytes; ++i) bytes[i] = EEPROM.read(i);
  table_.decode(bytes);
  for (int i = 0; i < ms::kServoCount; ++i) bank.setTrim(i, table_.trim(i));
  printLine();
}

ms::TrimSetResult CalibrationStore::setTrim(int servo, int value) {
  const ms::TrimTable before = table_;
  const ms::TrimSetResult r = table_.set(servo, value);
  if (r == ms::TrimSetResult::Accepted && !applyChange(before)) return ms::TrimSetResult::Rejected;
  return r;
}

bool CalibrationStore::resetAll() {
  const ms::TrimTable before = table_;
  if (!table_.reset()) return true;
  return applyChange(before);
}

bool CalibrationStore::applyChange(const ms::TrimTable& before) {
  // Stored before the result is returned, and only when a trim changed.
  if (table_.writeNeeded()) {
    uint8_t bytes[ms::kTrimBytes];
    table_.encode(bytes);
    for (int i = 0; i < ms::kTrimBytes; ++i) {
      if (EEPROM.read(i) != bytes[i]) EEPROM.write(i, bytes[i]);
    }
    const bool stored = EEPROM.commit();
    committed_ = true;  // the pass erased and programmed flash either way
    if (!stored) {
      // Not stored: the change is not accepted. Keep the table, the buffer and the
      // output at the values held before; the sector's content is now unknown.
      table_ = before;
      table_.encode(bytes);
      for (int i = 0; i < ms::kTrimBytes; ++i) EEPROM.write(i, bytes[i]);
      diag::line("calibration write failed");
      printLine();
      return false;
    }
    table_.clearWriteNeeded();
  }

  // On the output at once: a driven servo takes its new width at the next pass.
  for (int i = 0; i < ms::kServoCount; ++i) bank_->setTrim(i, table_.trim(i));

  printLine();
  if (gate_ != nullptr && gate_->mode() == ms::OutputMode::Fault && table_.allValid()) {
    gate_->calibrationValid();
  }
  return true;
}

bool CalibrationStore::takeCommitFlag() {
  const bool c = committed_;
  committed_ = false;
  return c;
}

void CalibrationStore::printLine() const {
  char line[96];
  table_.formatLine(line, sizeof line);
  diag::line(line);
}
