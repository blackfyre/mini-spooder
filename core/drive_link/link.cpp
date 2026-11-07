#include "drive_link/link.h"

namespace ms {
namespace drive_link {

void Link::applyEnd() {
  // The link-loss failsafe (D5): stop unconditionally, so a request passed on
  // but not yet started never starts, then the safe state, then the session end.
  ports_.stopMotion();
  ports_.enterSafeLinkLoss();
  live_ = false;
  ports_.sessionEnded();
}

void Link::applyStart() {
  live_ = true;
  ports_.sessionStarted();
  ports_.stopMotion();  // ends what another caller started before the session
}

AcceptResult Link::receive(const uint8_t* datagram, int length, Sender sender, uint32_t nowMs) {
  AcceptResult r = session_.accept(datagram, length, sender, nowMs);
  if (r.sessionEnded) applyEnd();
  if (r.sessionStarted) applyStart();
  switch (r.request.kind) {
    case RequestKind::Play:
      session_.recordResult(ports_.requestMotion(r.request.motion));
      break;
    case RequestKind::Stop:
      ports_.stopMotion();
      break;
    case RequestKind::None:
      break;
  }
  return r;
}

bool Link::finishReceive(uint32_t nowMs) {
  if (!session_.judge(nowMs)) return false;
  applyEnd();
  return true;
}

int Link::telemetry(uint32_t nowMs, uint8_t playing, uint8_t pending, const uint8_t angles[JOINT_COUNT],
                    uint8_t* out) {
  if (!live_ || !session_.telemetryDue(nowMs)) return 0;
  const Telemetry t = session_.makeTelemetry(playing, pending, angles, nowMs);
  const int n = encodeTelemetry(t, session_.telemetrySeq(), out);
  session_.telemetrySent(nowMs);
  return n;
}

}  // namespace drive_link
}  // namespace ms
