// The single output gate (servo-output): the output modes, the overriding safe
// state, the writer token, the enable rule by origin and the direct servo command.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "servo/pulse.h"
#include "servo/servo_bank.h"

namespace ms {

enum class OutputMode : uint8_t { Idle, Active, Safe, Fault };

// The origin of an enable request. Callers pass it; the gate never infers it.
enum class Origin : uint8_t { Link, Portal, Check };

// The reason recorded for the last safe state.
enum class SafeReason : uint8_t { None, LinkLoss, Check, CalibrationOk };

using WriterToken = uint32_t;

struct EnableResult {
  bool accepted;
  WriterToken token;  // the current token when accepted
};

const char* modeName(OutputMode mode);
const char* originName(Origin origin);
const char* safeReasonName(SafeReason reason);

class OutputGate {
 public:
  // Called within the call that changes the mode, before it returns. `reason` is
  // the text of the mode line ("portal", "link-loss", "calibration", ...).
  using ModeListener = void (*)(void* ctx, OutputMode mode, const char* reason);
  static constexpr int kMaxListeners = 4;

  // Starts in `idle` when every trim is valid, otherwise in `fault`.
  // `checkOriginPermitted` comes from MINI_SPOODER_CHECK_BUILD.
  OutputGate(ServoBank& bank, bool trimsValid, bool checkOriginPermitted);

  bool addModeListener(ModeListener listener, void* ctx);

  OutputMode mode() const { return mode_; }
  SafeReason safeReason() const { return safeReason_; }
  WriterToken token() const { return token_; }
  const ServoBank& bank() const { return bank_; }

  // The mode line for the current mode: "mode <mode> <reason>".
  size_t modeLine(char* buf, size_t size) const;

  // Session facts from drive-link.
  void linkSessionStarted();
  void linkSessionEnded();
  bool sessionLive() const { return sessionLive_; }

  // An enable request with a complete pose. In `idle` or `safe` an accepted one
  // moves to `active`; in `active` an accepted one applies the pose.
  EnableResult enable(Origin origin, const Pose& pose);

  // Whether an enable request from this origin would be accepted now.
  bool enablePermitted(Origin origin) const;

  // Writes with a token: accepted only in `active` with the current token.
  bool setPose(WriterToken token, const Pose& pose);
  bool setServo(WriterToken token, int servo, int angle);
  bool release(WriterToken token, int servo);
  bool releaseAll(WriterToken token);

  // The token-free direct servo command of the web portal (origin `portal`).
  bool directServo(int servo, int angle);

  // A safe-state request, from any mode.
  void enterSafe(SafeReason reason);

  // A calibration fault: any mode to `fault`.
  void calibrationFault();
  // servo-calibration reports every trim valid: `fault` to `safe`.
  void calibrationValid();

 private:
  bool permittedIn(OutputMode mode, Origin origin) const;
  void disable();  // release all, stale token, clear the session fact
  void changeMode(OutputMode mode, const char* reason);

  ServoBank& bank_;
  const bool checkPermitted_;
  OutputMode mode_;
  SafeReason safeReason_;
  const char* reasonText_;
  WriterToken token_;
  bool sessionLive_;
  bool sessionAfterDisable_;
  ModeListener listeners_[kMaxListeners];
  void* listenerCtx_[kMaxListeners];
  int listenerCount_;
};

}  // namespace ms
