// The check build's console commands (D7), parsed from one ASCII line. Pure
// parsing: whether a command is allowed is the output gate's and the trim
// table's concern, so angles and trim values are passed on as given.
#pragma once

#include <stddef.h>

#include "servo/pulse.h"

namespace ms {
namespace check {

enum class CommandKind {
  Enable,      // enable a0 ... a7
  Pose,        // pose a0 ... a7
  Servo,       // servo i a
  Release,     // release i
  ReleaseAll,  // release all
  Safe,        // safe
  Trim,        // trim i v
  TrimReset,   // trimreset
  Status,      // status
  Restart,     // restart
  Hang,        // hang
};

struct Command {
  CommandKind kind;
  int servo;  // Servo, Release, Trim
  int value;  // Servo: angle; Trim: trim
  Pose pose;  // Enable, Pose
};

// Number arguments are decimal integers within -999..999; servo indices must be
// 0-7. Words are separated by spaces or tabs; a trailing CR is ignored.
// Returns false, leaving `out` unspecified, for any malformed line.
bool parseLine(const char* line, size_t len, Command& out);

}  // namespace check
}  // namespace ms
