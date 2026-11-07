#include "drive_link.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <lwip/udp.h>
// The core's UDP context, used directly rather than through WiFiUDP: WiFiUDP's
// parsePacket() returns 0 both for an empty queue and for an empty datagram, so
// an empty datagram would end the drain unread and without an outcome.
#include <include/UdpContext.h>

#include "diag.h"
#include "drive_link/link.h"
#include "drive_link/protocol.h"
#include "drive_link/session.h"
#include "drive_link_player.h"
#include "servo/servo_bank.h"

namespace drive_link_glue {

namespace {

namespace dl = ms::drive_link;

// The calls the link makes (link.h), bound to motion playback with origin link
// and to the output gate. The link never enables servo output.
class Ports : public dl::LinkPorts {
 public:
  ms::OutputGate* gate = nullptr;

  bool requestMotion(uint8_t motion) override { return drive_link_player::request(motion); }
  void stopMotion() override { drive_link_player::stop(); }
  void enterSafeLinkLoss() override { gate->enterSafe(ms::SafeReason::LinkLoss); }
  void sessionStarted() override { gate->linkSessionStarted(); }
  void sessionEnded() override { gate->linkSessionEnded(); }
};

Ports ports;
dl::Link link(ports);
UdpContext* udp = nullptr;
bool started = false;

// Access point settings (design D7): applied at every boot, never stored by the SDK.
const char kSsid[] = "mini-spooder";
const char kPassword[] = "87654321";
constexpr int kChannel = 1;
constexpr int kMaxStations = 4;

}  // namespace

void begin(ms::OutputGate& gate) {
  ports.gate = &gate;
  WiFi.persistent(false);  // before any setting: nothing goes to the SDK's sectors
  WiFi.mode(WIFI_AP);      // access point only: no station, no DNS server, no mDNS
  const IPAddress address(192, 168, 4, 1);
  const IPAddress netmask(255, 255, 255, 0);
  bool ok = WiFi.softAPConfig(address, address, netmask);
  ok = WiFi.softAP(kSsid, kPassword, kChannel, false, kMaxStations) && ok;
  udp = new UdpContext;
  udp->ref();
  ok = udp->listen(IP_ADDR_ANY, dl::PORT) && ok;
  if (!ok) diag::line("drive-link start failed");
  started = true;
}

void receive() {
  if (!started) return;
  uint8_t buf[dl::DATAGRAM_BUFFER];
  for (int i = 0; i < dl::DRAIN_MAX; i++) {
    if (!udp->next()) break;  // false only when no datagram is queued
    const size_t size = udp->getSize();
    udp->read(reinterpret_cast<char*>(buf), sizeof buf);  // at most 10 bytes; next() drops the rest
    const dl::Sender sender = {static_cast<uint32_t>(udp->getRemoteAddress()), udp->getRemotePort()};
    // The datagram's own length, so a longer datagram stays too long; accept()
    // reads buf only for a 9-byte datagram.
    const int length = size > 0x7FFF ? 0x7FFF : static_cast<int>(size);
    link.receive(buf, length, sender, millis());
  }
  link.finishReceive(millis());
}

void sendTelemetry() {
  if (!started) return;
  const uint32_t now = millis();
  if (!link.live() || !link.session().telemetryDue(now)) return;
  const ms::ServoBank& bank = ports.gate->bank();
  uint8_t angles[dl::JOINT_COUNT];
  for (int i = 0; i < dl::JOINT_COUNT; i++) {
    const ms::ServoState s = bank.state(i);
    angles[i] = s.driven && s.hasAngle ? s.angle : dl::ANGLE_NONE;
  }
  uint8_t frame[dl::MAX_FRAME_SIZE];
  const int n = link.telemetry(now, drive_link_player::currentMotion(), drive_link_player::pendingMotion(), angles,
                               frame);
  if (n <= 0) return;
  const dl::Sender to = link.session().sender();
  const IPAddress address(to.address);
  if (udp->append(reinterpret_cast<const char*>(frame), static_cast<size_t>(n)) == static_cast<size_t>(n))
    udp->send(address, to.port);  // from the drive port; a refused send is dropped, not retried
  else
    udp->cancelBuffer();
}

bool live() { return link.live(); }

}  // namespace drive_link_glue
