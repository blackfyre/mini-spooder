#include "check_console.h"

#include <Arduino.h>
#include <stdio.h>

#include "calibration_store.h"
#include "check/console_parse.h"
#include "diag.h"
#include "servo/pulse.h"

void CheckConsole::begin(ms::OutputGate& gate, CalibrationStore& calibration,
                         const ms::LoopStats& stats) {
  gate_ = &gate;
  calibration_ = &calibration;
  stats_ = &stats;
  diag::line("check console ready");
}

void CheckConsole::poll(uint32_t /*nowMs*/) {
  // Bounded per pass: at most the bytes already received.
  int avail = Serial.available();
  while (avail-- > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\n') {
      if (overflow_) {
        diag::line("error line too long");
      } else {
        run(line_, len_);
      }
      len_ = 0;
      overflow_ = false;
    } else if (len_ < sizeof line_) {
      line_[len_++] = static_cast<char>(c);
    } else {
      overflow_ = true;
    }
  }
}

namespace {

void reply(bool ok) { diag::line(ok ? "ok" : "rejected"); }

}  // namespace

void CheckConsole::run(const char* line, size_t len) {
  ms::check::Command cmd;
  if (!ms::check::parseLine(line, len, cmd)) {
    diag::line("error malformed command");
    return;
  }
  using K = ms::check::CommandKind;
  switch (cmd.kind) {
    case K::Enable: {
      const ms::EnableResult r = gate_->enable(ms::Origin::Check, cmd.pose);
      if (r.accepted) token_ = r.token;
      reply(r.accepted);
      return;
    }
    case K::Pose:
      reply(gate_->setPose(token_, cmd.pose));
      return;
    case K::Servo:
      reply(gate_->setServo(token_, cmd.servo, cmd.value));
      return;
    case K::Release:
      reply(gate_->release(token_, cmd.servo));
      return;
    case K::ReleaseAll:
      reply(gate_->releaseAll(token_));
      return;
    case K::Safe:
      gate_->enterSafe(ms::SafeReason::Check);
      reply(true);
      return;
    case K::Trim: {
      const ms::TrimSetResult r = calibration_->setTrim(cmd.servo, cmd.value);
      diag::line(r == ms::TrimSetResult::Rejected ? "rejected"
                 : r == ms::TrimSetResult::Unchanged ? "ok unchanged"
                                                     : "ok");
      return;
    }
    case K::TrimReset:
      reply(calibration_->resetAll());
      return;
    case K::Status:
      status();
      return;
    case K::Restart:
      diag::line("restarting");
      diag::flush();
      ESP.restart();
      return;
    case K::Hang:
      diag::line("hanging");
      diag::flush();
      // Busy-wait without yielding: the software watchdog resets the chip.
      for (;;) {
        __asm__ __volatile__("" ::: "memory");  // keeps the empty loop
      }
  }
}

void CheckConsole::status() {
  char buf[96];
  snprintf(buf, sizeof buf, "status mode %s reason %s session %s", ms::modeName(gate_->mode()),
           ms::safeReasonName(gate_->safeReason()), gate_->sessionLive() ? "live" : "none");
  diag::line(buf);
  const ms::ServoBank& bank = gate_->bank();
  const ms::TrimTable& trims = calibration_->table();
  for (int i = 0; i < ms::kServoCount; ++i) {
    const ms::ServoState s = bank.state(i);
    char angle[8];
    if (s.hasAngle) {
      snprintf(angle, sizeof angle, "%u", static_cast<unsigned>(s.angle));
    } else {
      snprintf(angle, sizeof angle, "-");
    }
    snprintf(buf, sizeof buf, "servo %d %s angle %s width %u trim %d%s range %u-%u", i,
             s.driven ? "driven" : "released", angle, static_cast<unsigned>(bank.widthUs(i)),
             trims.trim(i), trims.valid(i) ? "" : "!", static_cast<unsigned>(ms::kSafeRange[i].low),
             static_cast<unsigned>(ms::kSafeRange[i].high));
    diag::line(buf);
  }
  snprintf(buf, sizeof buf, "loop passes %lu longest %lu us over-budget %lu commit %lu us",
           static_cast<unsigned long>(stats_->passes()),
           static_cast<unsigned long>(stats_->longestPassUs()),
           static_cast<unsigned long>(stats_->overBudgetCount()),
           static_cast<unsigned long>(stats_->longestCommitPassUs()));
  diag::line(buf);
}
