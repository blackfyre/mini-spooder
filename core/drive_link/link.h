// The drive link's two loop steps (drive-link, design D1, D5, D6) over a
// Session, with the calls they make to the rest of the firmware behind an
// interface, so their order is host-tested.
// - Receive and failsafe step (before motion playback's step): receive() for
//   each drained datagram, then finishReceive(). Each result is applied as soon
//   as accept() returns it, so the firmware sees session events in datagram
//   order; within one result a session end is applied before a session start.
//     end:   stop, enterSafe(link-loss), live() false, linkSessionEnded()
//     start: live() true, linkSessionStarted(), stop
//     Play(id): request(id), and its answer recorded; Stop: stop
// - Telemetry step (after motion playback's step): telemetry().
// live() changes only together with the linkSessionStarted() and
// linkSessionEnded() calls, so every reader after the receive step sees the same
// session state as the output gate. The link never enables servo output.
#pragma once

#include <stdint.h>

#include "drive_link/protocol.h"
#include "drive_link/session.h"

namespace ms {
namespace drive_link {

// The calls the drive link makes. The firmware binds them to motion playback
// (with origin link) and to the output gate; the host tests record them.
class LinkPorts {
 public:
  virtual bool requestMotion(uint8_t motion) = 0;  // motion playback's request: accepted?
  virtual void stopMotion() = 0;                   // motion playback's stop
  virtual void enterSafeLinkLoss() = 0;            // enterSafe(SafeReason::LinkLoss)
  virtual void sessionStarted() = 0;               // linkSessionStarted()
  virtual void sessionEnded() = 0;                 // linkSessionEnded()

 protected:
  ~LinkPorts() = default;
};

class Link {
 public:
  explicit Link(LinkPorts& ports) : ports_(ports) {}

  // One datagram (see Session::accept), applied at once.
  AcceptResult receive(const uint8_t* datagram, int length, Sender sender, uint32_t nowMs);
  // The pass's final judge, applied at once. True when it ended a session.
  bool finishReceive(uint32_t nowMs);

  // Whether a session is live, as reported to the rest of the firmware.
  bool live() const { return live_; }

  // When a telemetry frame is due at `nowMs` in a live session, encode it into
  // `out` (at least TELEMETRY_FRAME_SIZE bytes), advance the schedule and return
  // its size; otherwise return 0. The frame goes to session().sender().
  int telemetry(uint32_t nowMs, uint8_t playing, uint8_t pending, const uint8_t angles[JOINT_COUNT],
                uint8_t* out);

  const Session& session() const { return session_; }

 private:
  void applyEnd();
  void applyStart();

  LinkPorts& ports_;
  Session session_;
  bool live_ = false;
};

}  // namespace drive_link
}  // namespace ms
