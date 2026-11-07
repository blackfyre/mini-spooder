// The web portal's request handler (web-portal, design decision 1): a pure
// function from a request and a read-only context to a response and at most one
// action. Hardware-free: the firmware glue maps ESP8266WebServer onto it and
// carries the action out through motion-playback, servo-output and
// servo-calibration.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "servo/output_gate.h"
#include "servo/pulse.h"

namespace ms {
namespace portal {

// Longest integer argument text the portal reads (sign, leading zeros and
// digits). Longer text is answered as malformed (400).
constexpr size_t kMaxIntegerText = 32;

// A complete, well-formed decimal integer: an optional sign followed by one or
// more digits, nothing else, within the 32-bit signed range.
bool parseInteger(const char* text, int32_t& out);

enum class Method : uint8_t { Get, Other };

struct Arg {
  const char* name;
  const char* value;
};

struct Request {
  Method method;
  const char* path;
  const Arg* args;
  size_t argCount;
};

// The facts the handler reads, filled by the glue on every request.
struct Context {
  bool sessionLive;             // drive-link reports a live session
  OutputMode mode;              // servo-output's mode
  bool motionBusy;              // motion-playback: a motion or keyframe playing or pending
  bool (*isMotionId)(int32_t);  // motion-playback's catalogue
  int trimMin;                  // servo-calibration's accepted trim range
  int trimMax;
};

enum class Page : uint8_t { None, Controller, Zero, Setting, Editor };

enum class ActionKind : uint8_t {
  None,
  RequestMotion,  // motion-playback's request(motion), origin portal
  DirectServo,    // motion-playback's stop, then servo-output's direct servo command
  StoreTrim,      // servo-calibration: set trim `servo` to `value`
  ResetTrims,     // servo-calibration: reset all trims to 0
  PlayKeyframe,   // motion-playback's single keyframe: angles, durationMs
};

struct Action {
  ActionKind kind;
  int32_t motion;
  int servo;
  int value;
  uint8_t angles[kServoCount];
  uint16_t durationMs;
};

enum class ContentType : uint8_t { Html, Plain };

// The response. A page result (page != None) is served by the glue from flash;
// otherwise the body is head + echo + tail. `echo` points into the request.
struct Result {
  int status;
  ContentType type;
  Page page;
  Action action;
  char head[48];
  const char* echo;
  const char* tail;
};

// Bodies of the refusals the glue and the pages share.
constexpr const char* kRemoteInControl = "remote in control";
constexpr const char* kMotionPlaying = "motion playing";
constexpr const char* kServoOutputFault = "servo output fault";
constexpr const char* kServoOutputOff = "servo output off";
constexpr const char* kSaveFailed = "calibration not stored";

Result handle(const Request& request, const Context& context);

// The calls an action makes, implemented by the firmware glue on
// motion-playback (binding with origin portal), servo-output and
// servo-calibration, and by a recording model in the host tests.
class Executor {
 public:
  virtual ~Executor() = default;
  virtual void requestMotion(int32_t motion) = 0;  // the answer is motion-playback's
  virtual void stopMotion() = 0;
  virtual bool directServo(int servo, int angle) = 0;  // servo-output, no token
  virtual bool storeTrim(int servo, int value) = 0;    // false: not stored
  virtual bool resetTrims() = 0;                       // false: not stored
  virtual bool playKeyframe(const uint8_t angles[kServoCount], uint16_t durationMs) = 0;
};

// Carries out the result's action and returns the response to send: the
// result itself, or the refusal when the action's owner rejects it (direct
// servo command: 503 `servo output off`; keyframe: 400; a trim that could not be
// stored: 500). A direct servo command stops motion-playback first.
Result carryOut(const Result& result, Executor& executor);

// The answers when servo-output or motion-playback rejects an action.
Result directServoRejected();
Result keyframeRejected();
// servo-calibration could not store an accepted trim change (flash commit).
Result saveFailed();

// Writes the body (head + echo + tail) into buf; returns its length, truncated
// to size - 1.
size_t formatBody(const Result& result, char* buf, size_t size);

}  // namespace portal
}  // namespace ms
