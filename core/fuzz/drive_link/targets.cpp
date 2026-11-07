#include "targets.h"

#include <stdlib.h>
#include <string.h>

#include "drive_link/link.h"
#include "drive_link/protocol.h"
#include "drive_link/session.h"

using namespace ms::drive_link;

namespace {

void require(bool ok) {
  if (!ok) abort();
}

int frameSize(FrameType type) { return type == FrameType::Command ? COMMAND_FRAME_SIZE : TELEMETRY_FRAME_SIZE; }

int encodeFrame(const Frame& f, uint8_t* out) {
  return f.type == FrameType::Command ? encodeCommand(f.command, f.seq, out) : encodeTelemetry(f.telemetry, f.seq, out);
}

// A returned frame: it is the next unaccounted input bytes, it re-encodes to
// them, and those bytes decoded alone give the same frame.
void checkFrame(const Frame& f, const uint8_t* data, size_t pushed, uint32_t start) {
  const int n = frameSize(f.type);
  require(start + static_cast<uint32_t>(n) <= pushed);
  uint8_t enc[MAX_FRAME_SIZE];
  require(encodeFrame(f, enc) == n);
  require(memcmp(enc, data + start, n) == 0);
  Decoder alone;
  for (int i = 0; i < n; i++) alone.push(data[start + i]);
  Frame g;
  require(alone.flush(g));
  uint8_t again[MAX_FRAME_SIZE];
  require(encodeFrame(g, again) == n && memcmp(again, enc, n) == 0);
  Frame extra;
  require(!alone.flush(extra) && alone.discardedBytes() == 0);
}

// The reference rule for a datagram that reaches validation: exactly one
// version 1 command frame with a valid CRC and nothing else.
bool referenceCommand(const uint8_t* b, int n) {
  if (n != COMMAND_FRAME_SIZE) return false;
  if (b[0] != START || b[1] != VERSION || b[2] != 1 || b[4] != COMMAND_PAYLOAD_SIZE) return false;
  return crc16(b + 1, 6) == static_cast<uint16_t>(b[7] | (b[8] << 8));
}

// Records every call of the link, and checks the order rules as they happen.
struct CheckingPorts : LinkPorts {
  enum class Call { None, Request, Stop, Safe, Started, Ended };
  const Link* link = nullptr;
  bool live = false;  // the gate's view: started and not yet ended
  Call last = Call::None;
  uint32_t starts = 0;
  uint32_t ends = 0;

  bool requestMotion(uint8_t motion) override {
    require(live && link->live());
    last = Call::Request;
    return (motion >= 1 && motion <= 15) || motion == 99 || motion == 100;
  }
  void stopMotion() override { last = Call::Stop; }
  void enterSafeLinkLoss() override {
    require(live && last == Call::Stop);  // the failsafe's stop comes first
    last = Call::Safe;
  }
  void sessionStarted() override {
    require(!live && link->live());
    live = true;
    ++starts;
    last = Call::Started;
  }
  void sessionEnded() override {
    require(live && last == Call::Safe && !link->live());
    live = false;
    ++ends;
    last = Call::Ended;
  }
};

}  // namespace

FuzzDecoderSummary fuzzDecoderRun(const uint8_t* data, size_t size) {
  Decoder d;
  uint32_t frameBytes = 0;
  int frames = 0;
  Frame f;
  for (size_t i = 0; i < size; i++) {
    d.push(data[i]);
    require(d.pending() >= 0 && d.pending() <= MAX_FRAME_SIZE);
    while (d.next(f)) {
      checkFrame(f, data, i + 1, frameBytes + d.discardedBytes());
      frameBytes += static_cast<uint32_t>(frameSize(f.type));
      ++frames;
    }
    require(d.pending() <= MAX_FRAME_SIZE);
    require(frameBytes + d.discardedBytes() + static_cast<uint32_t>(d.pending()) == i + 1);
  }
  while (d.flush(f)) {
    checkFrame(f, data, size, frameBytes + d.discardedBytes());
    frameBytes += static_cast<uint32_t>(frameSize(f.type));
    ++frames;
  }
  require(d.pending() == 0);
  require(frameBytes + d.discardedBytes() == size);
  require(d.validFrames() == static_cast<uint32_t>(frames));
  return FuzzDecoderSummary{frames, d.discardedBytes(), d.discardedFrames()};
}

void fuzzDecoderOne(const uint8_t* data, size_t size) { (void)fuzzDecoderRun(data, size); }

void fuzzSessionOne(const uint8_t* data, size_t size) {
  static const Sender kSenders[4] = {
      {0xC0A80402, 40000}, {0xC0A80402, 40001}, {0xC0A80403, 40000}, {0xC0A80403, 40001}};
  CheckingPorts ports;
  Link link(ports);
  ports.link = &link;

  uint32_t now = 0xFFFFF000u;
  uint32_t datagrams = 0;
  uint32_t outcomes[5] = {};
  bool windowKnown = false;
  uint32_t window = 0;
  int decodedInWindow = 0;
  uint32_t lastAcceptedMs = 0;
  bool first = true;

  size_t at = 0;
  while (at + 3 <= size) {
    const uint8_t flags = data[at];
    now += data[at + 1];
    int length = data[at + 2] % (kFuzzRecordMaxLength + 1);
    at += 3;
    if (static_cast<size_t>(length) > size - at) length = static_cast<int>(size - at);
    const uint8_t* bytes = data + at;
    at += static_cast<size_t>(length);
    const Sender sender = kSenders[flags & 3];

    const bool wasLive = link.live();
    const Sender locked = link.session().sender();
    const Counts before = link.session().counts();
    const AcceptResult r = link.receive(bytes, length, sender, now);
    ++datagrams;
    ++outcomes[static_cast<int>(r.outcome)];

    // Exactly one outcome per datagram.
    const Counts c = link.session().counts();
    const int deltas = static_cast<uint16_t>(c.accepted - before.accepted) +
                       static_cast<uint16_t>(c.malformed - before.malformed) +
                       static_cast<uint16_t>(c.outOfSequence - before.outOfSequence) +
                       static_cast<uint16_t>(c.otherSender - before.otherSender) +
                       static_cast<uint16_t>(c.rateLimited - before.rateLimited);
    require(deltas == 1);

    // The first datagram of a run meets no session and a fresh budget: it is
    // accepted, starting a session with no request, exactly when it is one valid
    // command frame, and malformed otherwise.
    if (first) {
      first = false;
      if (referenceCommand(bytes, length)) {
        require(r.outcome == Outcome::Accepted && r.sessionStarted && r.request.kind == RequestKind::None);
      } else {
        require(r.outcome == Outcome::Malformed && !r.sessionStarted);
      }
    }

    // Decoding stays within the budget of each window of the fixed grid.
    const bool decoded =
        r.outcome == Outcome::Accepted || r.outcome == Outcome::Malformed || r.outcome == Outcome::OutOfSequence;
    require(decoded == r.decoded);
    if (!windowKnown || window != now / WINDOW_MS) {
      windowKnown = true;
      window = now / WINDOW_MS;
      decodedInWindow = 0;
    }
    if (decoded) ++decodedInWindow;
    require(decodedInWindow <= WINDOW_BUDGET);

    // Another sender is never decoded while a session it does not hold is live
    // (unless that session had lapsed at this datagram).
    const bool lapsedNow = wasLive && static_cast<uint32_t>(now - lastAcceptedMs) > LIVENESS_MS;
    if (wasLive && !lapsedNow && (sender.address != locked.address || sender.port != locked.port))
      require(r.outcome == Outcome::OtherSender);
    require(r.sessionEnded == (lapsedNow || r.jump));
    if (r.outcome == Outcome::Accepted || r.outcome == Outcome::OutOfSequence) require(referenceCommand(bytes, length));
    if (r.outcome == Outcome::Malformed) require(!referenceCommand(bytes, length));

    // Requests come only from accepted frames of the locked sender, at most one
    // per frame, never from a session's first frame.
    if (r.request.kind != RequestKind::None) {
      require(r.outcome == Outcome::Accepted && !r.sessionStarted && link.live());
      require(sender.address == link.session().sender().address && sender.port == link.session().sender().port);
      if (r.request.kind == RequestKind::Play) require(r.request.motion != MOTION_STOP);
    }
    if (r.outcome == Outcome::Accepted) {
      require(link.live());
      lastAcceptedMs = now;
    }
    if (r.sessionStarted) require(r.outcome == Outcome::Accepted && ports.last == CheckingPorts::Call::Stop);
    if (r.sessionEnded && !r.sessionStarted) require(ports.last == CheckingPorts::Call::Ended);

    // A session never stays live more than LIVENESS_MS after its newest
    // accepted frame.
    if (link.live()) require(static_cast<uint32_t>(now - lastAcceptedMs) <= LIVENESS_MS);

    if (flags & 0x80) {
      link.finishReceive(now);
      if (link.live()) require(static_cast<uint32_t>(now - lastAcceptedMs) <= LIVENESS_MS);
      uint8_t angles[JOINT_COUNT];
      memset(angles, ANGLE_NONE, sizeof angles);
      uint8_t out[MAX_FRAME_SIZE];
      const int n = link.telemetry(now, 0, 0, angles, out);
      require(n == 0 || (n == TELEMETRY_FRAME_SIZE && link.live()));
    }
    require(ports.live == link.live());
  }

  // One failsafe per session end, none without one.
  require(static_cast<uint16_t>(ports.ends) == link.session().counts().failsafes);
  require(ports.starts == ports.ends + (link.live() ? 1u : 0u));
  uint32_t sum = 0;
  for (uint32_t o : outcomes) sum += o;
  require(sum == datagrams);
  require(static_cast<uint16_t>(outcomes[static_cast<int>(Outcome::Accepted)]) == link.session().counts().accepted);
}
