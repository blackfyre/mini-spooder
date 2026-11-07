#include "servo/output_gate.h"

#include <stdio.h>

namespace ms {

const char* modeName(OutputMode mode) {
  switch (mode) {
    case OutputMode::Idle: return "idle";
    case OutputMode::Active: return "active";
    case OutputMode::Safe: return "safe";
    case OutputMode::Fault: return "fault";
  }
  return "?";
}

const char* originName(Origin origin) {
  switch (origin) {
    case Origin::Link: return "link";
    case Origin::Portal: return "portal";
    case Origin::Check: return "check";
  }
  return "?";
}

const char* safeReasonName(SafeReason reason) {
  switch (reason) {
    case SafeReason::None: return "none";
    case SafeReason::LinkLoss: return "link-loss";
    case SafeReason::Check: return "check";
    case SafeReason::CalibrationOk: return "calibration-ok";
  }
  return "?";
}

OutputGate::OutputGate(ServoBank& bank, bool trimsValid, bool checkOriginPermitted)
    : bank_(bank),
      checkPermitted_(checkOriginPermitted),
      mode_(trimsValid ? OutputMode::Idle : OutputMode::Fault),
      safeReason_(SafeReason::None),
      reasonText_(trimsValid ? "reset" : "calibration"),
      token_(0),
      sessionLive_(false),
      sessionAfterDisable_(false),
      listeners_(),
      listenerCtx_(),
      listenerCount_(0) {}

bool OutputGate::addModeListener(ModeListener listener, void* ctx) {
  if (listener == nullptr || listenerCount_ >= kMaxListeners) return false;
  listeners_[listenerCount_] = listener;
  listenerCtx_[listenerCount_] = ctx;
  ++listenerCount_;
  return true;
}

size_t OutputGate::modeLine(char* buf, size_t size) const {
  const int n = snprintf(buf, size, "mode %s %s", modeName(mode_), reasonText_);
  return n < 0 ? 0 : static_cast<size_t>(n);
}

void OutputGate::linkSessionStarted() {
  sessionLive_ = true;
  sessionAfterDisable_ = true;
}

void OutputGate::linkSessionEnded() {
  sessionLive_ = false;
  sessionAfterDisable_ = false;
}

bool OutputGate::permittedIn(OutputMode mode, Origin origin) const {
  switch (mode) {
    case OutputMode::Fault:
      return false;
    case OutputMode::Idle:
    case OutputMode::Safe:
      switch (origin) {
        case Origin::Link: return sessionLive_ && sessionAfterDisable_;
        case Origin::Portal: return !sessionLive_;
        case Origin::Check: return checkPermitted_ && !sessionLive_;
      }
      return false;
    case OutputMode::Active:
      switch (origin) {
        // A link request needs the live session that the enable rule names.
        case Origin::Link: return sessionLive_ && sessionAfterDisable_;
        case Origin::Portal: return !sessionLive_;
        case Origin::Check: return checkPermitted_;
      }
      return false;
  }
  return false;
}

bool OutputGate::enablePermitted(Origin origin) const { return permittedIn(mode_, origin); }

EnableResult OutputGate::enable(Origin origin, const Pose& pose) {
  if (!permittedIn(mode_, origin) || !isAcceptedPose(pose)) return EnableResult{false, 0};
  bank_.setPose(pose);
  if (mode_ != OutputMode::Active) changeMode(OutputMode::Active, originName(origin));
  return EnableResult{true, token_};
}

bool OutputGate::setPose(WriterToken token, const Pose& pose) {
  if (mode_ != OutputMode::Active || token != token_) return false;
  return bank_.setPose(pose);
}

bool OutputGate::setServo(WriterToken token, int servo, int angle) {
  if (mode_ != OutputMode::Active || token != token_) return false;
  return bank_.setServo(servo, angle);
}

bool OutputGate::release(WriterToken token, int servo) {
  if (mode_ != OutputMode::Active || token != token_) return false;
  return bank_.release(servo);
}

bool OutputGate::releaseAll(WriterToken token) {
  if (mode_ != OutputMode::Active || token != token_) return false;
  bank_.releaseAll();
  return true;
}

bool OutputGate::directServo(int servo, int angle) {
  if (!isServoIndex(servo) || !isAcceptedAngle(angle)) return false;
  if (sessionLive_) return false;
  switch (mode_) {
    case OutputMode::Active:
      return bank_.setServo(servo, angle);
    case OutputMode::Idle:
    case OutputMode::Safe:
      if (!permittedIn(mode_, Origin::Portal)) return false;
      // The enable request with the enable pose, then the commanded angle, both
      // before any listener runs or the driver's next pass, so that every first
      // pulse already has its final width.
      bank_.setPose(kEnablePose);
      bank_.setServo(servo, angle);
      changeMode(OutputMode::Active, originName(Origin::Portal));
      return true;
    case OutputMode::Fault:
      return false;
  }
  return false;
}

void OutputGate::disable() {
  bank_.releaseAll();
  ++token_;
  sessionAfterDisable_ = false;
}

void OutputGate::enterSafe(SafeReason reason) {
  switch (mode_) {
    case OutputMode::Idle:
    case OutputMode::Active:
      safeReason_ = reason;
      disable();
      changeMode(OutputMode::Safe, safeReasonName(reason));
      return;
    case OutputMode::Safe:
      safeReason_ = reason;
      return;
    case OutputMode::Fault:
      return;
  }
}

void OutputGate::calibrationFault() {
  if (mode_ == OutputMode::Fault) return;
  disable();
  changeMode(OutputMode::Fault, "calibration");
}

void OutputGate::calibrationValid() {
  if (mode_ != OutputMode::Fault) return;
  safeReason_ = SafeReason::CalibrationOk;
  disable();
  changeMode(OutputMode::Safe, safeReasonName(SafeReason::CalibrationOk));
}

void OutputGate::changeMode(OutputMode mode, const char* reason) {
  mode_ = mode;
  reasonText_ = reason;
  for (int i = 0; i < listenerCount_; ++i) listeners_[i](listenerCtx_[i], mode, reason);
}

}  // namespace ms
