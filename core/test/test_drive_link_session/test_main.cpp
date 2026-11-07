// Drive link session and its loop steps (drive-link: Drive port and datagrams,
// Command frames, Motion requests, Sequence numbers, One remote at a time,
// Datagram rate limit, Session start, Link loss, Telemetry frames, Coexistence
// with the web portal). The output gate is firmware-foundation's real OutputGate
// behind a recorder; motion playback is a stand-in with one pending slot.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "drive_link/link.h"
#include "drive_link/protocol.h"
#include "drive_link/session.h"
#include "servo/output_gate.h"
#include "servo/pulse.h"
#include "servo/servo_bank.h"

using namespace ms;
using namespace ms::drive_link;

void setUp() {}
void tearDown() {}

namespace {

constexpr Sender kRemote = {0xC0A80402, 40000};  // 192.168.4.2:40000
constexpr Sender kOther = {0xC0A80403, 40000};   // 192.168.4.3:40000
constexpr Sender kRemoteNewPort = {0xC0A80402, 40001};
const Pose kZeroPose = {{135, 45, 135, 45, 45, 135, 45, 135}};

bool inCatalogue(int id) { return (id >= 1 && id <= 15) || id == 99 || id == 100; }

// A stand-in of motion playback: accepts 1-15, 99 and 100, keeps one pending
// request, starts it at its next step (enabling output with the zero pose and the
// request's origin when output is not active), ends with the mode check.
struct Player {
  OutputGate* gate = nullptr;
  uint8_t current = 0;
  uint8_t pending = 0;
  Origin pendingOrigin = Origin::Link;
  int requests = 0;
  int stops = 0;
  int enableCalls = 0;

  bool request(int id, Origin origin) {
    ++requests;
    if (!inCatalogue(id)) return false;
    pending = static_cast<uint8_t>(id);
    pendingOrigin = origin;
    return true;
  }
  void stop() {
    ++stops;
    current = 0;
    pending = 0;
  }
  // The player's step in a loop pass.
  void step() {
    if (current != 0 && gate->mode() != OutputMode::Active) {
      current = 0;
      pending = 0;
      return;
    }
    if (current == 0 && pending != 0) {
      if (gate->mode() != OutputMode::Active) {
        ++enableCalls;
        if (!gate->enable(pendingOrigin, kZeroPose).accepted) {
          pending = 0;
          return;
        }
      }
      current = pending;
      pending = 0;
    }
  }
  // The current motion's last keyframe ends: the pending one starts at the next step.
  void finish() { current = 0; }
};

// The calls the link makes, recorded in order and forwarded to the real gate and
// the stand-in player. After each call, the link's live() is compared with the
// gate's session state, as a portal lock that reads only live() would see it.
struct Ports : LinkPorts {
  OutputGate* gate = nullptr;
  Player* player = nullptr;
  const Link* link = nullptr;
  char log[2048] = {};
  int liveMismatches = 0;

  void note(const char* event) {
    if (log[0]) strncat(log, " ", sizeof(log) - strlen(log) - 1);
    strncat(log, event, sizeof(log) - strlen(log) - 1);
    if (link->live() != gate->sessionLive()) ++liveMismatches;
  }
  bool requestMotion(uint8_t motion) override {
    const bool ok = player->request(motion, Origin::Link);
    char e[24];
    snprintf(e, sizeof e, "request%u", motion);
    note(e);
    return ok;
  }
  void stopMotion() override {
    player->stop();
    note("stop");
  }
  void enterSafeLinkLoss() override {
    gate->enterSafe(SafeReason::LinkLoss);
    note("safe");
  }
  void sessionStarted() override {
    gate->linkSessionStarted();
    note("started");
  }
  void sessionEnded() override {
    gate->linkSessionEnded();
    note("ended");
  }
  void clear() { log[0] = 0; }
};

struct Rig {
  ServoBank bank;
  OutputGate gate;
  Player player;
  Ports ports;
  Link link;
  uint8_t remoteSeq = 0;

  Rig() : bank(), gate(bank, true, false), link(ports) {
    player.gate = &gate;
    ports.gate = &gate;
    ports.player = &player;
    ports.link = &link;
  }

  AcceptResult send(Sender s, uint8_t seq, uint8_t motion, uint8_t counter, uint32_t now) {
    uint8_t buf[COMMAND_FRAME_SIZE];
    encodeCommand(Command{motion, counter}, seq, buf);
    return link.receive(buf, COMMAND_FRAME_SIZE, s, now);
  }
  AcceptResult raw(Sender s, const uint8_t* bytes, int n, uint32_t now) { return link.receive(bytes, n, s, now); }

  // One loop pass: the receive and failsafe step's final judge, the player's
  // step, then the telemetry step. Returns the telemetry frame's size (0: none).
  int pass(uint32_t now, Frame* telemetryOut = nullptr) {
    link.finishReceive(now);
    player.step();
    uint8_t angles[JOINT_COUNT];
    for (int i = 0; i < JOINT_COUNT; i++) {
      const ServoState st = bank.state(i);
      angles[i] = st.driven && st.hasAngle ? st.angle : ANGLE_NONE;
    }
    uint8_t out[MAX_FRAME_SIZE];
    const int n = link.telemetry(now, player.current, player.pending, angles, out);
    if (n > 0 && telemetryOut) {
      Decoder d;
      for (int i = 0; i < n; i++) d.push(out[i]);
      TEST_ASSERT_TRUE(d.flush(*telemetryOut));
      TEST_ASSERT_EQUAL(FrameType::Telemetry, telemetryOut->type);
    }
    return n;
  }

  // Starts a session from kRemote at `now` (seq 10, counter 4) and runs the pass.
  void startSession(uint32_t now = 1000) {
    send(kRemote, 10, 0, 4, now);
    remoteSeq = 10;
    pass(now);
  }
};

void assertNoRequest(const AcceptResult& r) { TEST_ASSERT_EQUAL(RequestKind::None, r.request.kind); }

}  // namespace

// --- Drive port and datagrams -------------------------------------------------

void test_constants() {
  TEST_ASSERT_EQUAL_UINT16(52041, PORT);
  TEST_ASSERT_EQUAL_UINT32(50, COMMAND_PERIOD_MS);
  TEST_ASSERT_EQUAL_UINT32(300, LIVENESS_MS);
  TEST_ASSERT_EQUAL_UINT32(100, TELEMETRY_PERIOD_MS);
  TEST_ASSERT_EQUAL_UINT32(20, WINDOW_MS);
  TEST_ASSERT_EQUAL(4, WINDOW_BUDGET);
  TEST_ASSERT_EQUAL(8, SEQ_WINDOW);
  TEST_ASSERT_EQUAL(128, DRAIN_MAX);
  TEST_ASSERT_EQUAL(10, DATAGRAM_BUFFER);
}

// Scenario: Valid command datagram.
void test_valid_command_datagram_accepted() {
  Rig r;
  const AcceptResult a = r.send(kRemote, 10, 0, 4, 1000);
  TEST_ASSERT_EQUAL(Outcome::Accepted, a.outcome);
  TEST_ASSERT_TRUE(a.sessionStarted);
  TEST_ASSERT_TRUE(r.link.live());
  TEST_ASSERT_EQUAL_UINT16(1, r.link.session().counts().accepted);
}

// Scenario: Corrupt or foreign datagram (from the locked sender, and with no session).
void test_corrupt_or_foreign_datagram_is_malformed() {
  uint8_t bad[5][MAX_FRAME_SIZE + 1];
  int len[5];
  // Bad CRC.
  len[0] = encodeCommand(Command{2, 5}, 11, bad[0]);
  bad[0][8] ^= 0x10;
  // Version 2 with a valid CRC.
  len[1] = encodeCommand(Command{2, 5}, 11, bad[1]);
  bad[1][1] = 2;
  {
    const uint16_t c = crc16(bad[1] + 1, 6);
    bad[1][7] = static_cast<uint8_t>(c);
    bad[1][8] = static_cast<uint8_t>(c >> 8);
  }
  // A valid telemetry frame.
  Telemetry t = {};
  len[2] = encodeTelemetry(t, 11, bad[2]);
  // Payload length 3.
  const uint8_t l3[] = {START, VERSION, 1, 11, 3, 2, 5, 0, 0, 0};
  memcpy(bad[3], l3, sizeof l3);
  {
    const uint16_t c = crc16(bad[3] + 1, 7);
    bad[3][8] = static_cast<uint8_t>(c);
    bad[3][9] = static_cast<uint8_t>(c >> 8);
  }
  len[3] = 10;
  // One byte after a valid command frame.
  len[4] = encodeCommand(Command{2, 5}, 11, bad[4]);
  bad[4][len[4]++] = 0x00;

  for (int i = 0; i < 5; i++) {
    // With no session live: nothing starts.
    Rig none;
    const AcceptResult a = none.raw(kRemote, bad[i], len[i], 1000);
    TEST_ASSERT_EQUAL_MESSAGE(Outcome::Malformed, a.outcome, "no session");
    TEST_ASSERT_FALSE(none.link.live());
    TEST_ASSERT_EQUAL_STRING("", none.ports.log);

    // From the locked sender: no request, liveness not refreshed.
    Rig r;
    r.startSession(1000);
    r.ports.clear();
    const AcceptResult b = r.raw(kRemote, bad[i], len[i], 1200);
    TEST_ASSERT_EQUAL_MESSAGE(Outcome::Malformed, b.outcome, "locked sender");
    assertNoRequest(b);
    TEST_ASSERT_EQUAL_STRING("", r.ports.log);
    TEST_ASSERT_EQUAL_UINT16(1, r.link.session().counts().malformed);
    TEST_ASSERT_TRUE(r.link.finishReceive(1301));  // lapses 300 ms after the accepted frame
  }
}

// Scenario: Corrupt datagram from another sender.
void test_corrupt_datagram_from_other_sender_counts_other_sender() {
  Rig r;
  r.startSession(1000);
  uint8_t buf[COMMAND_FRAME_SIZE];
  encodeCommand(Command{2, 5}, 11, buf);
  buf[8] ^= 0xFF;
  const AcceptResult a = r.raw(kOther, buf, sizeof buf, 1010);
  TEST_ASSERT_EQUAL(Outcome::OtherSender, a.outcome);
  TEST_ASSERT_EQUAL_UINT16(1, r.link.session().counts().otherSender);
  TEST_ASSERT_EQUAL_UINT16(0, r.link.session().counts().malformed);
}

void test_longer_datagram_length_from_socket_is_malformed() {
  // The glue passes the length parsePacket() reports with a 10-byte buffer.
  Rig r;
  uint8_t buf[DATAGRAM_BUFFER];
  encodeCommand(Command{0, 1}, 1, buf);
  const AcceptResult a = r.raw(kRemote, buf, 1000, 1000);
  TEST_ASSERT_EQUAL(Outcome::Malformed, a.outcome);
  TEST_ASSERT_FALSE(r.link.live());
}

// --- Command frames --------------------------------------------------------------

// Scenario: Lost frame loses nothing.
void test_lost_frame_loses_nothing() {
  Rig r;
  r.startSession(1000);
  r.ports.clear();
  // Frame seq 11 with counter 5 is lost; seq 12 carries the same state.
  const AcceptResult a = r.send(kRemote, 12, 2, 5, 1100);
  TEST_ASSERT_EQUAL(RequestKind::Play, a.request.kind);
  TEST_ASSERT_EQUAL_UINT8(2, a.request.motion);
  r.send(kRemote, 13, 2, 5, 1150);
  TEST_ASSERT_EQUAL_STRING("request2", r.ports.log);
}

// Scenario: Repeated state.
void test_repeated_state_makes_no_request() {
  Rig r;
  r.startSession(1000);
  r.ports.clear();
  for (int i = 1; i <= 6; i++) {
    const AcceptResult a = r.send(kRemote, static_cast<uint8_t>(10 + i), static_cast<uint8_t>(i), 4, 1000 + 50 * i);
    TEST_ASSERT_EQUAL(Outcome::Accepted, a.outcome);
    assertNoRequest(a);
  }
  TEST_ASSERT_EQUAL_STRING("", r.ports.log);
}

// --- Motion requests -------------------------------------------------------------

// Scenario: Request a motion while idle.
void test_request_motion_while_idle() {
  Rig r;
  r.startSession(1000);  // telemetry sent at 1000
  r.ports.clear();
  r.send(kRemote, 11, 2, 5, 1050);
  r.pass(1050);
  TEST_ASSERT_EQUAL_STRING("request2", r.ports.log);
  Frame f;
  TEST_ASSERT_TRUE(r.pass(1100, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(5, f.telemetry.counter);
  TEST_ASSERT_EQUAL_UINT8(1, f.telemetry.result);
  TEST_ASSERT_EQUAL_UINT8(2, f.telemetry.playing);
  TEST_ASSERT_EQUAL(OutputMode::Active, r.gate.mode());
}

// Scenario: Request a motion while another plays.
void test_request_motion_while_another_plays() {
  Rig r;
  r.startSession(1000);
  r.send(kRemote, 11, 11, 5, 1050);  // Push up
  r.pass(1050);
  r.ports.clear();
  r.send(kRemote, 12, 9, 6, 1090);  // Say Hi
  Frame f;
  TEST_ASSERT_TRUE(r.pass(1100, &f) > 0);
  TEST_ASSERT_EQUAL_STRING("request9", r.ports.log);
  TEST_ASSERT_EQUAL_UINT8(1, f.telemetry.result);
  TEST_ASSERT_EQUAL_UINT8(11, f.telemetry.playing);
  TEST_ASSERT_EQUAL_UINT8(9, f.telemetry.pending);
  r.player.finish();
  TEST_ASSERT_TRUE(r.pass(1200, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(9, f.telemetry.playing);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.pending);
}

// Scenario: Request a stop.
void test_request_stop() {
  Rig r;
  r.startSession(1000);
  r.send(kRemote, 11, 2, 5, 1050);
  r.pass(1050);
  r.send(kRemote, 12, 6, 6, 1060);
  r.pass(1060);
  TEST_ASSERT_EQUAL_UINT8(6, r.player.pending);
  r.ports.clear();
  r.send(kRemote, 13, 0, 7, 1090);
  TEST_ASSERT_EQUAL_STRING("stop", r.ports.log);  // before the player's next step
  Frame f;
  TEST_ASSERT_TRUE(r.pass(1100, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(2, f.telemetry.result);
  TEST_ASSERT_EQUAL_UINT8(7, f.telemetry.counter);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.playing);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.pending);
}

// Scenario: Rejected motion id.
void test_rejected_motion_id() {
  Rig r;
  r.startSession(1000);
  r.ports.clear();
  r.send(kRemote, 11, 50, 5, 1050);
  Frame f;
  TEST_ASSERT_TRUE(r.pass(1100, &f) > 0);
  TEST_ASSERT_EQUAL_STRING("request50", r.ports.log);
  TEST_ASSERT_EQUAL_UINT8(3, f.telemetry.result);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.playing);
}

// Scenario: First frame of a session.
void test_first_frame_of_session_makes_no_request() {
  Rig r;
  const AcceptResult a = r.send(kRemote, 77, 9, 123, 1000);
  assertNoRequest(a);
  TEST_ASSERT_EQUAL_STRING("started stop", r.ports.log);
  Frame f;
  TEST_ASSERT_TRUE(r.pass(1000, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(123, f.telemetry.counter);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.result);
}

// Scenario: Counter wraps.
void test_counter_wrap_makes_one_request() {
  Rig r;
  r.send(kRemote, 10, 0, 255, 1000);
  r.pass(1000);
  r.ports.clear();
  const AcceptResult a = r.send(kRemote, 11, 1, 0, 1050);
  TEST_ASSERT_EQUAL(RequestKind::Play, a.request.kind);
  r.send(kRemote, 12, 1, 0, 1100);
  TEST_ASSERT_EQUAL_STRING("request1", r.ports.log);
}

// --- Sequence numbers ------------------------------------------------------------

// Scenario: Reordered datagram.
void test_reordered_datagram_out_of_sequence() {
  Rig r;
  r.send(kRemote, 42, 0, 4, 1000);
  const AcceptResult a = r.send(kRemote, 41, 2, 5, 1010);
  TEST_ASSERT_EQUAL(Outcome::OutOfSequence, a.outcome);
  TEST_ASSERT_FALSE(a.jump);
  assertNoRequest(a);
  TEST_ASSERT_EQUAL_UINT16(1, r.link.session().counts().outOfSequence);
  // It does not refresh liveness.
  TEST_ASSERT_TRUE(r.link.finishReceive(1301));
}

// Scenario: Duplicated datagram.
void test_duplicated_datagram_one_request() {
  Rig r;
  r.startSession(1000);
  r.ports.clear();
  r.send(kRemote, 11, 2, 5, 1050);
  const AcceptResult a = r.send(kRemote, 11, 2, 5, 1051);
  TEST_ASSERT_EQUAL(Outcome::OutOfSequence, a.outcome);
  TEST_ASSERT_EQUAL_STRING("request2", r.ports.log);
}

// Scenario: Sequence wrap.
void test_sequence_wrap_accepted() {
  Rig r;
  r.send(kRemote, 255, 0, 4, 1000);
  const AcceptResult a = r.send(kRemote, 0, 0, 4, 1050);
  TEST_ASSERT_EQUAL(Outcome::Accepted, a.outcome);
}

void test_sequence_window_edges() {
  {
    Rig r;
    r.send(kRemote, 100, 0, 4, 1000);
    TEST_ASSERT_EQUAL(Outcome::Accepted, r.send(kRemote, 108, 0, 4, 1010).outcome);  // d = 8
  }
  {
    Rig r;
    r.send(kRemote, 100, 0, 4, 1000);
    const AcceptResult a = r.send(kRemote, 109, 0, 4, 1010);  // d = 9
    TEST_ASSERT_TRUE(a.jump);
    TEST_ASSERT_TRUE(a.sessionEnded);
  }
  {
    Rig r;
    r.send(kRemote, 100, 0, 4, 1000);
    const AcceptResult a = r.send(kRemote, 227, 0, 4, 1010);  // d = 127
    TEST_ASSERT_TRUE(a.jump);
  }
  {
    Rig r;
    r.send(kRemote, 100, 0, 4, 1000);
    const AcceptResult a = r.send(kRemote, 228, 0, 4, 1010);  // d = 128: stale
    TEST_ASSERT_EQUAL(Outcome::OutOfSequence, a.outcome);
    TEST_ASSERT_FALSE(a.jump);
    TEST_ASSERT_TRUE(r.link.live());
  }
}

// Scenario: Sequence jump.
void test_sequence_jump_ends_session_with_failsafe() {
  Rig r;
  r.send(kRemote, 200, 0, 4, 1000);
  r.pass(1000);
  r.ports.clear();
  const AcceptResult a = r.send(kRemote, 0, 2, 5, 1100);
  TEST_ASSERT_EQUAL(Outcome::OutOfSequence, a.outcome);
  TEST_ASSERT_TRUE(a.jump);
  assertNoRequest(a);
  TEST_ASSERT_FALSE(r.link.live());
  TEST_ASSERT_EQUAL_STRING("stop safe ended", r.ports.log);
  TEST_ASSERT_EQUAL(SafeReason::LinkLoss, r.gate.safeReason());
  TEST_ASSERT_EQUAL_UINT16(1, r.link.session().counts().failsafes);
  r.ports.clear();
  const AcceptResult b = r.send(kRemote, 1, 2, 6, 1150);
  TEST_ASSERT_TRUE(b.sessionStarted);
  assertNoRequest(b);
  TEST_ASSERT_EQUAL_STRING("started stop", r.ports.log);
}

// --- One remote at a time --------------------------------------------------------

// Scenario: Second sender ignored.
void test_second_sender_ignored() {
  Rig r;
  r.startSession(1000);
  r.ports.clear();
  for (int i = 0; i < 5; i++) {
    const AcceptResult a = r.send(kOther, static_cast<uint8_t>(i), 2, static_cast<uint8_t>(i), 1010 + 10 * i);
    TEST_ASSERT_EQUAL(Outcome::OtherSender, a.outcome);
  }
  TEST_ASSERT_EQUAL_STRING("", r.ports.log);
  TEST_ASSERT_EQUAL_UINT16(5, r.link.session().counts().otherSender);
  // The same address on another port is another sender too.
  TEST_ASSERT_EQUAL(Outcome::OtherSender, r.send(kRemoteNewPort, 11, 2, 5, 1070).outcome);
  const AcceptResult a = r.send(kRemote, 11, 3, 5, 1080);
  TEST_ASSERT_EQUAL(RequestKind::Play, a.request.kind);
  TEST_ASSERT_EQUAL_UINT8(3, a.request.motion);
}

// Session events in one pass (a): a lapse, then the first datagram read starts
// the next session. Scenario: Lapse and new session in one pass.
void test_lapse_and_new_session_in_one_pass() {
  Rig r;
  r.startSession(1000);
  r.send(kRemote, 11, 1, 5, 1000);
  r.pass(1000);  // Standby plays with output enabled by the link
  TEST_ASSERT_EQUAL(OutputMode::Active, r.gate.mode());
  r.ports.clear();
  // 301 ms later, no pass has judged the lapse; a frame from another sender.
  const AcceptResult a = r.send(kOther, 50, 2, 9, 1301);
  TEST_ASSERT_TRUE(a.sessionEnded);
  TEST_ASSERT_TRUE(a.sessionStarted);
  assertNoRequest(a);
  TEST_ASSERT_EQUAL_STRING("stop safe ended started stop", r.ports.log);
  TEST_ASSERT_EQUAL(0, r.ports.liveMismatches);
  TEST_ASSERT_EQUAL(OutputMode::Safe, r.gate.mode());
  // The new session's first motion request is permitted by the gate.
  TEST_ASSERT_TRUE(r.gate.enablePermitted(Origin::Link));
  r.send(kOther, 51, 2, 10, 1320);
  r.pass(1320);
  TEST_ASSERT_EQUAL_UINT8(2, r.player.current);
  TEST_ASSERT_EQUAL(OutputMode::Active, r.gate.mode());
}

// Session events in one pass (b): a jump ends a session, and the next datagram in
// the same pass starts a new one.
void test_jump_then_new_session_in_one_pass() {
  Rig r;
  r.startSession(1000);
  r.send(kRemote, 11, 1, 5, 1050);
  r.pass(1050);
  r.ports.clear();
  r.send(kRemote, 100, 2, 6, 1100);  // jump
  r.send(kRemote, 101, 2, 6, 1100);  // the restarted remote's next frame
  TEST_ASSERT_EQUAL_STRING("stop safe ended started stop", r.ports.log);
  TEST_ASSERT_EQUAL(0, r.ports.liveMismatches);
  TEST_ASSERT_TRUE(r.link.live());
  TEST_ASSERT_TRUE(r.gate.enablePermitted(Origin::Link));
  r.send(kRemote, 102, 2, 7, 1110);
  r.pass(1110);
  TEST_ASSERT_EQUAL_UINT8(2, r.player.current);
}

// Session events in one pass (c). Scenario: Session starts and ends in one pass.
void test_session_starts_and_ends_in_one_pass() {
  Rig r;
  r.send(kRemote, 10, 0, 4, 1000);
  r.send(kRemote, 100, 0, 4, 1001);
  r.link.finishReceive(1002);
  TEST_ASSERT_EQUAL_STRING("started stop stop safe ended", r.ports.log);
  TEST_ASSERT_EQUAL(0, r.ports.liveMismatches);
  TEST_ASSERT_FALSE(r.gate.sessionLive());
  TEST_ASSERT_FALSE(r.link.live());
}

// Scenario: Handover after a lapse.
void test_handover_after_lapse() {
  Rig r;
  r.startSession(1000);
  TEST_ASSERT_TRUE(r.link.finishReceive(1301));
  r.ports.clear();
  const AcceptResult a = r.send(kOther, 3, 2, 8, 1350);
  TEST_ASSERT_TRUE(a.sessionStarted);
  assertNoRequest(a);
  TEST_ASSERT_EQUAL_UINT32(kOther.address, r.link.session().sender().address);
  TEST_ASSERT_EQUAL(Outcome::OtherSender, r.send(kRemote, 11, 2, 5, 1360).outcome);
}

// A further case: the gate enters safe with no session end (the check console's
// safe); the same session then requests Forward. The link makes no enable call,
// and telemetry reports the player's state after the refused enable.
void test_safe_without_session_end_then_request() {
  Rig r;
  r.startSession(1000);
  r.send(kRemote, 11, 1, 5, 1050);
  r.pass(1050);
  TEST_ASSERT_EQUAL(OutputMode::Active, r.gate.mode());
  r.gate.enterSafe(SafeReason::Check);
  r.player.step();  // the mode check ends Standby
  r.ports.clear();
  const int enablesBefore = r.player.enableCalls;
  r.send(kRemote, 12, 2, 6, 1090);
  Frame f;
  TEST_ASSERT_TRUE(r.pass(1100, &f) > 0);
  TEST_ASSERT_EQUAL_STRING("request2", r.ports.log);  // no other call from the link
  TEST_ASSERT_EQUAL(enablesBefore + 1, r.player.enableCalls);  // the player's own, refused
  TEST_ASSERT_EQUAL(OutputMode::Safe, r.gate.mode());
  TEST_ASSERT_EQUAL_UINT8(1, f.telemetry.result);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.playing);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.pending);
}

// --- Datagram rate limit ---------------------------------------------------------

// Scenario: Flood from the locked sender.
void test_flood_from_locked_sender() {
  Rig r;
  r.send(kRemote, 0, 0, 0, 1000);  // window 50 (1000-1019), one decoded
  int decoded = 1;
  int limited = 0;
  for (int i = 1; i < 200; i++) {
    const AcceptResult a = r.send(kRemote, static_cast<uint8_t>(i), 0, 0, 1000 + static_cast<uint32_t>(i % 20));
    if (a.decoded) ++decoded;
    if (a.outcome == Outcome::RateLimited) ++limited;
  }
  TEST_ASSERT_EQUAL(4, decoded);
  TEST_ASSERT_EQUAL(196, limited);
  TEST_ASSERT_EQUAL_UINT16(196, r.link.session().counts().rateLimited);
  // The next window has its own budget.
  TEST_ASSERT_TRUE(r.send(kRemote, 4, 0, 0, 1020).decoded);
}

// Scenario: Flood from another station.
void test_flood_from_other_station_keeps_session() {
  Rig r;
  r.startSession(1000);
  r.ports.clear();
  uint8_t seq = 10;
  uint8_t counter = 4;
  for (uint32_t t = 1001; t <= 2000; t++) {
    for (int k = 0; k < 3; k++) r.send(kOther, static_cast<uint8_t>(t), 2, static_cast<uint8_t>(t), t);
    if (t % 50 == 0) {
      ++seq;
      if (t % 200 == 0) ++counter;
      const AcceptResult a = r.send(kRemote, seq, 1, counter, t);
      TEST_ASSERT_EQUAL(Outcome::Accepted, a.outcome);
    }
    r.pass(t);
  }
  TEST_ASSERT_TRUE(r.link.live());
  TEST_ASSERT_EQUAL_UINT16(0, r.link.session().counts().failsafes);
  TEST_ASSERT_EQUAL_UINT16(0, r.link.session().counts().rateLimited);
  TEST_ASSERT_EQUAL_UINT16(3000, r.link.session().counts().otherSender);
  TEST_ASSERT_EQUAL(5, r.player.requests);  // counter changes at 1200, 1400, ... 2000
}

// --- Session start ---------------------------------------------------------------

// Scenario: Remote joins during a portal motion.
void test_remote_joins_during_portal_motion() {
  Rig r;
  TEST_ASSERT_TRUE(r.player.request(13, Origin::Portal));  // Dance 1
  r.player.step();
  TEST_ASSERT_EQUAL_UINT8(13, r.player.current);
  r.player.request(1, Origin::Portal);  // and a pending one
  r.ports.clear();
  const int requestsBefore = r.player.requests;
  r.send(kRemote, 10, 5, 4, 1000);
  TEST_ASSERT_EQUAL_STRING("started stop", r.ports.log);
  TEST_ASSERT_EQUAL(requestsBefore, r.player.requests);
  r.player.step();
  TEST_ASSERT_EQUAL_UINT8(0, r.player.current);
  TEST_ASSERT_EQUAL_UINT8(0, r.player.pending);
}

// --- Link loss -------------------------------------------------------------------

void test_live_at_300_ended_at_301() {
  Rig r;
  r.startSession(1000);
  TEST_ASSERT_FALSE(r.link.finishReceive(1300));
  TEST_ASSERT_TRUE(r.link.live());
  r.ports.clear();
  TEST_ASSERT_TRUE(r.link.finishReceive(1301));
  TEST_ASSERT_FALSE(r.link.live());
  TEST_ASSERT_EQUAL_STRING("stop safe ended", r.ports.log);
  TEST_ASSERT_FALSE(r.link.finishReceive(1302));  // reported once
  TEST_ASSERT_EQUAL_UINT16(1, r.link.session().counts().failsafes);
}

// Scenario: Remote goes quiet while walking.
void test_remote_goes_quiet_while_walking() {
  Rig r;
  r.startSession(1000);
  r.send(kRemote, 11, 2, 5, 1050);
  for (uint32_t t = 1050; t <= 1350; t += 10) r.pass(t);
  TEST_ASSERT_EQUAL_UINT8(2, r.player.current);
  r.ports.clear();
  r.link.finishReceive(1351);  // the first pass after 300 ms
  TEST_ASSERT_EQUAL_STRING("stop safe ended", r.ports.log);
  TEST_ASSERT_EQUAL_UINT8(0, r.player.current);  // before the player's next step
  TEST_ASSERT_EQUAL(OutputMode::Safe, r.gate.mode());
  TEST_ASSERT_FALSE(r.bank.anyDriven());
  TEST_ASSERT_EQUAL_UINT16(1, r.link.session().counts().failsafes);
}

// Scenario: Pending request at the end of a session.
void test_pending_request_at_end_of_session() {
  Rig r;
  r.startSession(1000);
  r.ports.clear();
  r.send(kRemote, 11, 2, 5, 1050);  // passed on while idle: pending before its first tick
  TEST_ASSERT_EQUAL_UINT8(2, r.player.pending);
  r.send(kRemote, 60, 2, 5, 1051);  // a jump before the player's step
  TEST_ASSERT_EQUAL_STRING("request2 stop safe ended", r.ports.log);
  r.player.step();
  TEST_ASSERT_EQUAL_UINT8(0, r.player.current);
  TEST_ASSERT_EQUAL(0, r.player.enableCalls);
}

// Scenario: Lapse while servos hold a pose.
void test_lapse_while_servos_hold_pose() {
  Rig r;
  r.startSession(1000);
  r.send(kRemote, 11, 1, 5, 1050);
  r.pass(1050);
  r.player.finish();  // Standby ended: the servos hold its pose
  r.pass(1060);
  TEST_ASSERT_TRUE(r.bank.anyDriven());
  r.ports.clear();
  TEST_ASSERT_TRUE(r.link.finishReceive(1351));
  TEST_ASSERT_EQUAL_STRING("stop safe ended", r.ports.log);
  TEST_ASSERT_FALSE(r.bank.anyDriven());
}

// Scenario: Undecodable frames do not keep the session alive.
void test_undecodable_frames_do_not_keep_session_alive() {
  Rig r;
  r.startSession(1000);
  uint8_t bad[COMMAND_FRAME_SIZE];
  for (uint32_t t = 1050; t <= 1300; t += 50) {
    encodeCommand(Command{0, 4}, static_cast<uint8_t>(t / 50), bad);
    bad[7] ^= 0x01;
    TEST_ASSERT_EQUAL(Outcome::Malformed, r.raw(kRemote, bad, sizeof bad, t).outcome);
    TEST_ASSERT_FALSE(r.link.finishReceive(t));
  }
  TEST_ASSERT_TRUE(r.link.finishReceive(1301));
  TEST_ASSERT_EQUAL_UINT16(6, r.link.session().counts().malformed);
}

// Scenario: Occasional losses.
void test_five_losses_tolerated() {
  Rig r;
  r.startSession(1000);
  // Frames 11-15 lost; frame 16 read 300 ms after frame 10.
  for (uint32_t t = 1000; t <= 1300; t += 10) TEST_ASSERT_FALSE(r.link.finishReceive(t));
  TEST_ASSERT_EQUAL(Outcome::Accepted, r.send(kRemote, 16, 0, 4, 1300).outcome);
  TEST_ASSERT_FALSE(r.link.finishReceive(1599));
  TEST_ASSERT_EQUAL_UINT16(0, r.link.session().counts().failsafes);
}

// Scenario: Gap past the timeout.
void test_gap_past_timeout() {
  Rig r;
  r.startSession(1000);
  r.ports.clear();
  const AcceptResult a = r.send(kRemote, 11, 2, 5, 1301);
  TEST_ASSERT_TRUE(a.sessionEnded);
  TEST_ASSERT_TRUE(a.sessionStarted);
  assertNoRequest(a);
  TEST_ASSERT_EQUAL_STRING("stop safe ended started stop", r.ports.log);
}

// A restart on the same sender port 301 ms after the last accepted frame with a
// changed counter: a failsafe, then a new session, baseline only, no request.
void test_restart_same_port_after_lapse_changed_counter() {
  Rig r;
  r.startSession(1000);
  r.ports.clear();
  const AcceptResult a = r.send(kRemote, 12, 3, 99, 1301);
  TEST_ASSERT_TRUE(a.sessionEnded);
  TEST_ASSERT_TRUE(a.sessionStarted);
  assertNoRequest(a);
  TEST_ASSERT_EQUAL_STRING("stop safe ended started stop", r.ports.log);
  TEST_ASSERT_EQUAL_UINT16(1, r.link.session().counts().failsafes);
}

// Scenario: No session, no failsafe.
void test_no_session_no_failsafe() {
  Rig r;
  r.player.request(13, Origin::Portal);
  for (uint32_t t = 0; t < 5000; t += 10) r.pass(t);
  TEST_ASSERT_EQUAL_STRING("", r.ports.log);
  TEST_ASSERT_EQUAL(OutputMode::Active, r.gate.mode());
  TEST_ASSERT_EQUAL_UINT8(13, r.player.current);
}

// --- Telemetry frames ------------------------------------------------------------

void test_telemetry_schedule() {
  Rig r;
  TEST_ASSERT_EQUAL(0, r.pass(990));  // no session: nothing due
  r.send(kRemote, 10, 0, 4, 1000);
  TEST_ASSERT_TRUE(r.pass(1000) > 0);  // in the session's first pass
  for (uint32_t t = 1001; t < 1100; t++) {
    if (t % 50 == 0) r.send(kRemote, static_cast<uint8_t>(10 + (t - 1000) / 50), 0, 4, t);
    TEST_ASSERT_EQUAL(0, r.pass(t));
  }
  TEST_ASSERT_TRUE(r.pass(1100) > 0);
  // Scenario: Late loop pass (scheduled at 1100, 1200, 1300; 1200 sent at 1215).
  r.send(kRemote, 13, 0, 4, 1150);
  TEST_ASSERT_EQUAL(0, r.pass(1199));
  r.send(kRemote, 14, 0, 4, 1215);
  TEST_ASSERT_TRUE(r.pass(1215) > 0);
  TEST_ASSERT_EQUAL_UINT32(1300, r.link.session().telemetryDeadline());
  TEST_ASSERT_EQUAL(0, r.pass(1299));
  r.send(kRemote, 15, 0, 4, 1260);
  TEST_ASSERT_TRUE(r.pass(1300) > 0);
  // More than one period late: 1400 sent at 1520 re-bases to 1620.
  r.send(kRemote, 16, 0, 4, 1350);
  r.send(kRemote, 17, 0, 4, 1500);
  TEST_ASSERT_TRUE(r.pass(1520) > 0);
  TEST_ASSERT_EQUAL_UINT32(1620, r.link.session().telemetryDeadline());
  TEST_ASSERT_EQUAL(0, r.pass(1600));
  // After the session ends nothing is due.
  TEST_ASSERT_EQUAL(0, r.pass(1801));
  TEST_ASSERT_FALSE(r.link.live());
  TEST_ASSERT_EQUAL(0, r.pass(2000));
}

void test_telemetry_fields() {
  Rig r;
  r.send(kRemote, 10, 0, 4, 1000);
  Frame f;
  TEST_ASSERT_TRUE(r.pass(1000, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(0, f.seq);
  TEST_ASSERT_EQUAL_UINT8(10, f.telemetry.commandSeq);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.ageMs);
  for (int i = 0; i < JOINT_COUNT; i++) TEST_ASSERT_EQUAL_UINT8(255, f.telemetry.angles[i]);  // released
  TEST_ASSERT_EQUAL_UINT16(1, f.telemetry.accepted);

  r.send(kRemote, 11, 1, 5, 1050);
  TEST_ASSERT_TRUE(r.pass(1100 + 37, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(1, f.seq);
  TEST_ASSERT_EQUAL_UINT8(11, f.telemetry.commandSeq);
  TEST_ASSERT_EQUAL_UINT8(87, f.telemetry.ageMs);
  const uint8_t zero[JOINT_COUNT] = {135, 45, 135, 45, 45, 135, 45, 135};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, f.telemetry.angles, JOINT_COUNT);

  // One servo released: its angle reads 255.
  r.bank.release(3);
  TEST_ASSERT_TRUE(r.pass(1200, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(255, f.telemetry.angles[3]);
  TEST_ASSERT_EQUAL_UINT8(135, f.telemetry.angles[2]);

  // The age is capped at 255.
  TEST_ASSERT_TRUE(r.pass(1300, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(250, f.telemetry.ageMs);
  r.send(kRemote, 12, 1, 5, 1340);
  TEST_ASSERT_TRUE(r.pass(1400, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(60, f.telemetry.ageMs);
  Telemetry t = r.link.session().makeTelemetry(0, 0, zero, 1340 + 255);
  TEST_ASSERT_EQUAL_UINT8(255, t.ageMs);
  t = r.link.session().makeTelemetry(0, 0, zero, 1340 + 254);
  TEST_ASSERT_EQUAL_UINT8(254, t.ageMs);
}

// Scenario: Telemetry reports link statistics.
void test_telemetry_reports_link_statistics() {
  Rig r;
  r.startSession(1000);
  Frame a, b;
  r.send(kRemote, 11, 0, 4, 1050);
  TEST_ASSERT_TRUE(r.pass(1100, &a) > 0);
  uint8_t bad[3] = {1, 2, 3};
  for (int i = 0; i < 3; i++) r.raw(kRemote, bad, sizeof bad, 1120 + 20 * static_cast<uint32_t>(i));
  r.send(kRemote, 12, 0, 4, 1150);
  TEST_ASSERT_TRUE(r.pass(1200, &b) > 0);
  TEST_ASSERT_EQUAL_UINT16(a.telemetry.malformed + 3, b.telemetry.malformed);
}

void test_counts_wrap_at_65535() {
  Rig r;
  r.startSession(1000);
  uint8_t bad[1] = {0};
  // 65537 malformed datagrams within the budget, with a keep-alive frame first
  // in every tenth window: the count wraps once, to 1.
  uint32_t malformed = 0;
  for (uint32_t window = 51; malformed < 65537; window++) {
    const uint32_t t = window * WINDOW_MS;
    int budget = WINDOW_BUDGET;
    if (window % 10 == 0) {
      TEST_ASSERT_EQUAL(Outcome::Accepted, r.send(kRemote, ++r.remoteSeq, 0, 4, t).outcome);
      --budget;
    }
    for (int k = 0; k < budget && malformed < 65537; k++) {
      TEST_ASSERT_EQUAL(Outcome::Malformed, r.raw(kRemote, bad, 1, t).outcome);
      ++malformed;
    }
  }
  TEST_ASSERT_TRUE(r.link.live());
  TEST_ASSERT_EQUAL_UINT16(1, r.link.session().counts().malformed);
  const uint8_t none[JOINT_COUNT] = {255, 255, 255, 255, 255, 255, 255, 255};
  TEST_ASSERT_EQUAL_UINT16(1, r.link.session().makeTelemetry(0, 0, none, 0).malformed);
}

// Telemetry built after the player's step, in the pass of a due frame.
// Scenario: Request in the pass of a due frame.
void test_request_in_pass_of_due_frame() {
  Rig r;
  r.startSession(1000);
  Frame f;
  r.send(kRemote, 11, 2, 5, 1100);  // idle player: reported as playing
  TEST_ASSERT_TRUE(r.pass(1100, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(2, f.telemetry.playing);
  r.send(kRemote, 12, 9, 6, 1200);  // busy player: reported as pending
  TEST_ASSERT_TRUE(r.pass(1200, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(2, f.telemetry.playing);
  TEST_ASSERT_EQUAL_UINT8(9, f.telemetry.pending);
  r.send(kRemote, 13, 0, 7, 1300);  // stop: nothing playing
  TEST_ASSERT_TRUE(r.pass(1300, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.playing);
  TEST_ASSERT_EQUAL_UINT8(0, f.telemetry.pending);
  TEST_ASSERT_EQUAL_UINT8(2, f.telemetry.result);
}

// Scenario: Telemetry after a request (Say Hi with counter 7).
void test_telemetry_after_request() {
  Rig r;
  r.send(kRemote, 10, 0, 6, 1000);
  r.pass(1000);
  r.send(kRemote, 11, 9, 7, 1050);
  r.pass(1050);
  Frame f;
  TEST_ASSERT_TRUE(r.pass(1100, &f) > 0);
  TEST_ASSERT_EQUAL_UINT8(7, f.telemetry.counter);
  TEST_ASSERT_EQUAL_UINT8(1, f.telemetry.result);
  TEST_ASSERT_EQUAL_UINT8(9, f.telemetry.playing);
}

// --- Coexistence with the web portal ---------------------------------------------

// Scenarios: Another caller's motion during a session, and when the session ends.
void test_other_callers_motion_during_session() {
  Rig r;
  r.startSession(1000);
  r.send(kRemote, 11, 1, 5, 1050);
  r.pass(1050);
  r.player.finish();
  // Another caller (output already active) starts Dance 2.
  TEST_ASSERT_TRUE(r.player.request(14, Origin::Portal));
  r.pass(1060);
  TEST_ASSERT_EQUAL_UINT8(14, r.player.current);
  r.ports.clear();
  for (uint32_t t = 1100; t <= 1300; t += 50) {
    r.send(kRemote, static_cast<uint8_t>(12 + (t - 1100) / 50), 2, 5, t);
    r.pass(t);
  }
  TEST_ASSERT_EQUAL_STRING("", r.ports.log);
  TEST_ASSERT_EQUAL_UINT8(14, r.player.current);
  // The session lapses: the failsafe stops it.
  TEST_ASSERT_TRUE(r.link.finishReceive(1601));
  TEST_ASSERT_EQUAL_STRING("stop safe ended", r.ports.log);
  TEST_ASSERT_EQUAL_UINT8(0, r.player.current);
  TEST_ASSERT_EQUAL(OutputMode::Safe, r.gate.mode());
}

// Scenario: Session state: live from the pass that reads the first frame until
// the pass that detects the lapse.
void test_session_state_report() {
  Rig r;
  TEST_ASSERT_FALSE(r.link.live());
  r.send(kRemote, 10, 0, 4, 1000);
  TEST_ASSERT_TRUE(r.link.live());  // before the player's step
  TEST_ASSERT_TRUE(r.gate.sessionLive());
  for (uint32_t t = 1000; t <= 1300; t += 10) {
    r.pass(t);
    TEST_ASSERT_TRUE(r.link.live());
  }
  r.pass(1310);
  TEST_ASSERT_FALSE(r.link.live());
  TEST_ASSERT_FALSE(r.gate.sessionLive());
}

// A session across the millis() wrap at 0xFFFFFFFF.
void test_session_across_millis_wrap() {
  Rig r;
  const uint32_t t0 = 0xFFFFFF00u;
  r.send(kRemote, 10, 0, 4, t0);
  TEST_ASSERT_TRUE(r.pass(t0) > 0);
  uint8_t seq = 10;
  uint32_t t = t0;
  int frames = 1;
  for (int i = 1; i <= 20; i++) {  // 1000 ms across the wrap
    t = t0 + 50u * static_cast<uint32_t>(i);
    r.send(kRemote, ++seq, 0, 4, t);
    for (uint32_t k = 0; k < 50; k += 10)
      if (r.pass(t + k) > 0) ++frames;
  }
  TEST_ASSERT_TRUE(r.link.live());
  TEST_ASSERT_EQUAL(11, frames);  // t0, t0+100, ... t0+1000
  TEST_ASSERT_EQUAL_UINT16(0, r.link.session().counts().failsafes);
  TEST_ASSERT_FALSE(r.link.finishReceive(t + 300));
  TEST_ASSERT_TRUE(r.link.finishReceive(t + 301));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_constants);
  RUN_TEST(test_valid_command_datagram_accepted);
  RUN_TEST(test_corrupt_or_foreign_datagram_is_malformed);
  RUN_TEST(test_corrupt_datagram_from_other_sender_counts_other_sender);
  RUN_TEST(test_longer_datagram_length_from_socket_is_malformed);
  RUN_TEST(test_lost_frame_loses_nothing);
  RUN_TEST(test_repeated_state_makes_no_request);
  RUN_TEST(test_request_motion_while_idle);
  RUN_TEST(test_request_motion_while_another_plays);
  RUN_TEST(test_request_stop);
  RUN_TEST(test_rejected_motion_id);
  RUN_TEST(test_first_frame_of_session_makes_no_request);
  RUN_TEST(test_counter_wrap_makes_one_request);
  RUN_TEST(test_reordered_datagram_out_of_sequence);
  RUN_TEST(test_duplicated_datagram_one_request);
  RUN_TEST(test_sequence_wrap_accepted);
  RUN_TEST(test_sequence_window_edges);
  RUN_TEST(test_sequence_jump_ends_session_with_failsafe);
  RUN_TEST(test_second_sender_ignored);
  RUN_TEST(test_lapse_and_new_session_in_one_pass);
  RUN_TEST(test_jump_then_new_session_in_one_pass);
  RUN_TEST(test_session_starts_and_ends_in_one_pass);
  RUN_TEST(test_handover_after_lapse);
  RUN_TEST(test_safe_without_session_end_then_request);
  RUN_TEST(test_flood_from_locked_sender);
  RUN_TEST(test_flood_from_other_station_keeps_session);
  RUN_TEST(test_remote_joins_during_portal_motion);
  RUN_TEST(test_live_at_300_ended_at_301);
  RUN_TEST(test_remote_goes_quiet_while_walking);
  RUN_TEST(test_pending_request_at_end_of_session);
  RUN_TEST(test_lapse_while_servos_hold_pose);
  RUN_TEST(test_undecodable_frames_do_not_keep_session_alive);
  RUN_TEST(test_five_losses_tolerated);
  RUN_TEST(test_gap_past_timeout);
  RUN_TEST(test_restart_same_port_after_lapse_changed_counter);
  RUN_TEST(test_no_session_no_failsafe);
  RUN_TEST(test_telemetry_schedule);
  RUN_TEST(test_telemetry_fields);
  RUN_TEST(test_telemetry_reports_link_statistics);
  RUN_TEST(test_counts_wrap_at_65535);
  RUN_TEST(test_request_in_pass_of_due_frame);
  RUN_TEST(test_telemetry_after_request);
  RUN_TEST(test_other_callers_motion_during_session);
  RUN_TEST(test_session_state_report);
  RUN_TEST(test_session_across_millis_wrap);
  return UNITY_END();
}
