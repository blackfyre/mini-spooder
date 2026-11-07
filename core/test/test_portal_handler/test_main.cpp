// Host tests of the web portal's request handler (web-portal tasks 2.1-2.5).
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "calibration/trim_table.h"
#include "portal/request_handler.h"

using namespace ms;
using namespace ms::portal;

namespace {

bool stockMotionId(int32_t id) { return (id >= 1 && id <= 15) || id == 99 || id == 100; }

Context ctx(OutputMode mode = OutputMode::Active, bool session = false, bool busy = false) {
  return Context{session, mode, busy, stockMotionId, kTrimMin, kTrimMax};
}

// Splits "a=1&b=2" into arguments; the strings live in the returned holder.
struct Query {
  std::vector<std::string> parts;
  std::vector<Arg> args;
};

Query parseQuery(const char* query) {
  Query q;
  std::string s = query;
  size_t start = 0;
  while (start <= s.size() && !s.empty()) {
    size_t end = s.find('&', start);
    if (end == std::string::npos) end = s.size();
    const std::string item = s.substr(start, end - start);
    const size_t eq = item.find('=');
    q.parts.push_back(item.substr(0, eq));
    q.parts.push_back(eq == std::string::npos ? std::string() : item.substr(eq + 1));
    start = end + 1;
    if (end == s.size()) break;
  }
  for (size_t i = 0; i + 1 < q.parts.size(); i += 2) {
    q.args.push_back(Arg{q.parts[i].c_str(), q.parts[i + 1].c_str()});
  }
  return q;
}

struct Outcome {
  Result result;
  std::string body;
};

// `target` is "path" or "path?query".
Outcome run(const char* target, const Context& c, Method method = Method::Get) {
  std::string t = target;
  const size_t qm = t.find('?');
  static std::string path;  // outlives the result's echo pointer within one test
  path = t.substr(0, qm);
  static Query q;
  q = parseQuery(qm == std::string::npos ? "" : t.substr(qm + 1).c_str());
  const Request req{method, path.c_str(), q.args.empty() ? nullptr : q.args.data(), q.args.size()};
  Outcome o;
  o.result = handle(req, c);
  char buf[256];
  formatBody(o.result, buf, sizeof buf);
  o.body = buf;
  return o;
}

void expect(const Outcome& o, int status, ContentType type, const char* body, ActionKind action) {
  TEST_ASSERT_EQUAL_INT(status, o.result.status);
  TEST_ASSERT_EQUAL(static_cast<int>(type), static_cast<int>(o.result.type));
  TEST_ASSERT_EQUAL_STRING(body, o.body.c_str());
  TEST_ASSERT_EQUAL(static_cast<int>(action), static_cast<int>(o.result.action.kind));
  TEST_ASSERT_EQUAL(static_cast<int>(Page::None), static_cast<int>(o.result.page));
}

void expectNoAction(const Outcome& o, int status) {
  TEST_ASSERT_EQUAL_INT(status, o.result.status);
  TEST_ASSERT_EQUAL(static_cast<int>(ActionKind::None), static_cast<int>(o.result.action.kind));
}

void expectPage(const Outcome& o, Page p) {
  TEST_ASSERT_EQUAL_INT(200, o.result.status);
  TEST_ASSERT_EQUAL(static_cast<int>(ContentType::Html), static_cast<int>(o.result.type));
  TEST_ASSERT_EQUAL(static_cast<int>(p), static_cast<int>(o.result.page));
  TEST_ASSERT_EQUAL(static_cast<int>(ActionKind::None), static_cast<int>(o.result.action.kind));
}

const OutputMode kModes[] = {OutputMode::Idle, OutputMode::Active, OutputMode::Safe,
                             OutputMode::Fault};

// Records the calls an action makes, in order; each owner can be told to reject.
struct Recorder : Executor {
  std::vector<std::string> calls;
  bool servoOk = true, trimOk = true, resetOk = true, keyframeOk = true;
  void requestMotion(int32_t motion) override { calls.push_back("request " + std::to_string(motion)); }
  void stopMotion() override { calls.push_back("stop"); }
  bool directServo(int servo, int angle) override {
    calls.push_back("servo " + std::to_string(servo) + " " + std::to_string(angle));
    return servoOk;
  }
  bool storeTrim(int servo, int value) override {
    calls.push_back("trim " + std::to_string(servo) + " " + std::to_string(value));
    return trimOk;
  }
  bool resetTrims() override {
    calls.push_back("reset");
    return resetOk;
  }
  bool playKeyframe(const uint8_t angles[kServoCount], uint16_t durationMs) override {
    std::string c = "keyframe";
    for (int i = 0; i < kServoCount; ++i) c += " " + std::to_string(angles[i]);
    calls.push_back(c + " " + std::to_string(durationMs));
    return keyframeOk;
  }
  std::string joined() const {
    std::string j;
    for (const std::string& c : calls) j += (j.empty() ? "" : "; ") + c;
    return j;
  }
};

// handle() then carryOut(), as the glue does; returns the response sent.
Outcome serve(const char* target, const Context& c, Recorder& rec, Method method = Method::Get) {
  Outcome o = run(target, c, method);
  if (o.result.page != Page::None) return o;
  o.result = carryOut(o.result, rec);
  char buf[256];
  formatBody(o.result, buf, sizeof buf);
  o.body = buf;
  return o;
}

const char* const kOnline = "/online?m0=70&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70&t1=1000";

}  // namespace

void setUp() {}
void tearDown() {}

// 2.1 strict integers
static void test_parse_integer() {
  int32_t v = 0;
  TEST_ASSERT_TRUE(parseInteger("7", v));
  TEST_ASSERT_EQUAL_INT(7, v);
  TEST_ASSERT_TRUE(parseInteger("-5", v));
  TEST_ASSERT_EQUAL_INT(-5, v);
  TEST_ASSERT_TRUE(parseInteger("+3", v));
  TEST_ASSERT_EQUAL_INT(3, v);
  TEST_ASSERT_TRUE(parseInteger("2147483647", v));
  TEST_ASSERT_EQUAL_INT(2147483647, v);
  TEST_ASSERT_TRUE(parseInteger("-2147483648", v));
  TEST_ASSERT_TRUE(v == INT32_MIN);
  const char* bad[] = {"", "7a", "1.5", " 7", "7 ", "99999999999", "2147483648", "-2147483649",
                       "+", "-", "0x10", "--1"};
  for (const char* s : bad) TEST_ASSERT_FALSE_MESSAGE(parseInteger(s, v), s);
  TEST_ASSERT_FALSE(parseInteger(nullptr, v));
}

// 2.2 Motion request
static void test_motion_request() {
  Outcome o = run("/controller?pm=9", ctx());
  expect(o, 200, ContentType::Html, "(pm)=(9)", ActionKind::RequestMotion);
  TEST_ASSERT_EQUAL_INT(9, o.result.action.motion);

  o = run("/controller?pm=02", ctx());
  expect(o, 200, ContentType::Html, "(pm)=(02)", ActionKind::RequestMotion);
  TEST_ASSERT_EQUAL_INT(2, o.result.action.motion);

  o = run("/controller?pm=100", ctx());
  expect(o, 200, ContentType::Html, "(pm)=(100)", ActionKind::RequestMotion);
  TEST_ASSERT_EQUAL_INT(100, o.result.action.motion);
  o = run("/controller?pm=99", ctx());
  expect(o, 200, ContentType::Html, "(pm)=(99)", ActionKind::RequestMotion);
  TEST_ASSERT_EQUAL_INT(99, o.result.action.motion);
}

static void test_motion_unknown_and_malformed() {
  expect(run("/controller?pm=21", ctx()), 200, ContentType::Html, "(pm)=(21)", ActionKind::None);
  expect(run("/controller?pm=0", ctx()), 200, ContentType::Html, "(pm)=(0)", ActionKind::None);
  expect(run("/controller?pm=-1", ctx()), 200, ContentType::Html, "(pm)=(-1)", ActionKind::None);
  expect(run("/controller?pm=16", ctx()), 200, ContentType::Html, "(pm)=(16)", ActionKind::None);
  expect(run("/controller?pm=101", ctx()), 200, ContentType::Html, "(pm)=(101)", ActionKind::None);
  expectNoAction(run("/controller?pm=2a", ctx()), 400);
  expectNoAction(run("/controller?pm=", ctx()), 400);
  expectNoAction(run("/controller?pm=1.5", ctx()), 400);
}

// 2.2 Direct servo command
static void test_direct_servo() {
  for (OutputMode m : {OutputMode::Active, OutputMode::Idle, OutputMode::Safe}) {
    Outcome o = run("/controller?servo=3&value=110", ctx(m));
    expect(o, 200, ContentType::Html, "(pm)=()", ActionKind::DirectServo);
    TEST_ASSERT_EQUAL_INT(3, o.result.action.servo);
    TEST_ASSERT_EQUAL_INT(110, o.result.action.value);
  }
  Outcome o = run("/controller?servo=0&value=0", ctx());
  expect(o, 200, ContentType::Html, "(pm)=()", ActionKind::DirectServo);
  o = run("/controller?servo=7&value=180", ctx());
  expect(o, 200, ContentType::Html, "(pm)=()", ActionKind::DirectServo);
  TEST_ASSERT_EQUAL_INT(7, o.result.action.servo);
  TEST_ASSERT_EQUAL_INT(180, o.result.action.value);
  // Passed unchanged, while motion-playback is busy too (the glue stops first).
  o = run("/controller?servo=0&value=90", ctx(OutputMode::Active, false, true));
  expect(o, 200, ContentType::Html, "(pm)=()", ActionKind::DirectServo);
  TEST_ASSERT_EQUAL_INT(90, o.result.action.value);
}

static void test_direct_servo_invalid() {
  const char* bad[] = {
      "/controller?servo=3",           "/controller?servo=9&value=90",
      "/controller?servo=-1&value=90", "/controller?servo=8&value=90",
      "/controller?servo=3&value=181", "/controller?servo=3&value=-1",
      "/controller?servo=3&value=",    "/controller?servo=3&value=9x",
      "/controller?servo=&value=90",   "/controller?servo=a&value=90",
  };
  for (const char* t : bad) {
    const Outcome o = run(t, ctx());
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, o.result.status, t);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(ActionKind::None),
                              static_cast<int>(o.result.action.kind), t);
  }
}

static void test_pm_and_servo_together() {
  expectNoAction(run("/controller?pm=2&servo=3&value=90", ctx()), 400);
}

static void test_direct_servo_rejected_by_glue() {
  const Result r = directServoRejected();
  char buf[64];
  formatBody(r, buf, sizeof buf);
  TEST_ASSERT_EQUAL_INT(503, r.status);
  TEST_ASSERT_EQUAL(static_cast<int>(ContentType::Plain), static_cast<int>(r.type));
  TEST_ASSERT_EQUAL_STRING("servo output off", buf);
}

// 2.2 Unused pms parameter
static void test_pms_only() {
  for (OutputMode m : kModes) {
    for (bool session : {false, true}) {
      expect(run("/controller?pms=3", ctx(m, session)), 200, ContentType::Html, "(pm)=()",
             ActionKind::None);
    }
  }
  expect(run("/controller", ctx()), 200, ContentType::Html, "(pm)=()", ActionKind::None);
}

// 2.3 Calibration save
static void test_save_trim() {
  Outcome o = run("/save?key=1&value=-5", ctx());
  expect(o, 200, ContentType::Html, "(key, value)=(1,-5)", ActionKind::StoreTrim);
  TEST_ASSERT_EQUAL_INT(1, o.result.action.servo);
  TEST_ASSERT_EQUAL_INT(-5, o.result.action.value);
  // Decimal integers in the body.
  o = run("/save?key=+2&value=+04", ctx());
  expect(o, 200, ContentType::Html, "(key, value)=(2,4)", ActionKind::StoreTrim);
  // Both boundaries of the trim range from the context.
  o = run("/save?key=0&value=-124", ctx());
  expect(o, 200, ContentType::Html, "(key, value)=(0,-124)", ActionKind::StoreTrim);
  o = run("/save?key=7&value=124", ctx());
  expect(o, 200, ContentType::Html, "(key, value)=(7,124)", ActionKind::StoreTrim);
  expectNoAction(run("/save?key=0&value=-125", ctx()), 400);
  expectNoAction(run("/save?key=7&value=125", ctx()), 400);
  // The range comes from the context, not a constant.
  Context narrow = ctx();
  narrow.trimMin = -10;
  narrow.trimMax = 10;
  expectNoAction(run("/save?key=0&value=11", narrow), 400);
  expect(run("/save?key=0&value=10", narrow), 200, ContentType::Html, "(key, value)=(0,10)",
         ActionKind::StoreTrim);
}

static void test_save_reset_all() {
  expect(run("/save?key=100&value=abc", ctx()), 200, ContentType::Html, "(key, value)=(100,0)",
         ActionKind::ResetTrims);
  expect(run("/save?key=100", ctx()), 200, ContentType::Html, "(key, value)=(100,0)",
         ActionKind::ResetTrims);
  expect(run("/save?key=100&value=5", ctx()), 200, ContentType::Html, "(key, value)=(100,0)",
         ActionKind::ResetTrims);
}

static void test_save_invalid() {
  const char* bad[] = {"/save?key=50&value=3", "/save?key=8&value=0", "/save?key=-1&value=0",
                       "/save?key=1",          "/save?key=1&value=",  "/save?key=1&value=x",
                       "/save?value=3",        "/save",               "/save?key=1a&value=1"};
  for (const char* t : bad) {
    const Outcome o = run(t, ctx());
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, o.result.status, t);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(ActionKind::None),
                              static_cast<int>(o.result.action.kind), t);
  }
}

static void test_save_busy_session_and_modes() {
  // Motion busy: 409 before the parameter checks.
  for (const char* t : {"/save?key=1&value=-5", "/save?key=100", "/save?key=50&value=3"}) {
    expect(run(t, ctx(OutputMode::Active, false, true)), 409, ContentType::Plain, "motion playing",
           ActionKind::None);
  }
  // Session live: 409 remote in control, in fault too and for key 100, before busy.
  for (OutputMode m : kModes) {
    for (bool busy : {false, true}) {
      for (const char* t : {"/save?key=0&value=2", "/save?key=100", "/save?key=1"}) {
        expect(run(t, ctx(m, true, busy)), 409, ContentType::Plain, "remote in control",
               ActionKind::None);
      }
    }
  }
  // Safe state and fault without a session: accepted (repair in fault).
  expect(run("/save?key=1&value=-5", ctx(OutputMode::Safe)), 200, ContentType::Html,
         "(key, value)=(1,-5)", ActionKind::StoreTrim);
  expect(run("/save?key=2&value=0", ctx(OutputMode::Fault)), 200, ContentType::Html,
         "(key, value)=(2,0)", ActionKind::StoreTrim);
}

// 2.4 Online keyframe
static void test_online_valid() {
  const Outcome o = run(kOnline, ctx());
  expect(o, 200, ContentType::Html, "(m0, m1)=(70,90)", ActionKind::PlayKeyframe);
  const uint8_t expected[8] = {70, 90, 90, 110, 110, 90, 90, 70};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, o.result.action.angles, 8);
  TEST_ASSERT_EQUAL_UINT16(1000, o.result.action.durationMs);
}

static void test_online_ranges() {
  expectNoAction(run("/online?m0=70&m1=90&m2=90&m3=110&m4=110&m6=90&m7=70&t1=1000", ctx()), 400);
  expectNoAction(run("/online?m0=181&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70&t1=1000", ctx()),
                 400);
  expectNoAction(run("/online?m0=70&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=-1&t1=1000", ctx()),
                 400);
  expectNoAction(run("/online?m0=70&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70&t1=60001", ctx()),
                 400);
  expectNoAction(run("/online?m0=70&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70&t1=-1", ctx()),
                 400);
  expectNoAction(run("/online?m0=70&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70", ctx()), 400);
  Outcome o = run("/online?m0=0&m1=180&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70&t1=0", ctx());
  expect(o, 200, ContentType::Html, "(m0, m1)=(0,180)", ActionKind::PlayKeyframe);
  TEST_ASSERT_EQUAL_UINT16(0, o.result.action.durationMs);
  o = run("/online?m0=70&m1=90&m2=90&m3=110&m4=110&m5=90&m6=90&m7=70&t1=60000", ctx());
  expect(o, 200, ContentType::Html, "(m0, m1)=(70,90)", ActionKind::PlayKeyframe);
  TEST_ASSERT_EQUAL_UINT16(60000, o.result.action.durationMs);
}

static void test_online_rejected_by_glue() {
  TEST_ASSERT_EQUAL_INT(400, keyframeRejected().status);
}

// 2.5 Refusal order: 4 modes x session flag x {pm, servo, online, save}.
static void test_mode_session_matrix() {
  struct Case {
    const char* target;
    ActionKind passed;
    bool modeApplies;  // refused with 503 in fault (not /save)
  };
  const Case cases[] = {
      {"/controller?pm=2", ActionKind::RequestMotion, true},
      {"/controller?servo=0&value=135", ActionKind::DirectServo, true},
      {kOnline, ActionKind::PlayKeyframe, true},
      {"/save?key=0&value=2", ActionKind::StoreTrim, false},
  };
  int count = 0;
  for (const Case& c : cases) {
    for (OutputMode m : kModes) {
      for (bool session : {false, true}) {
        const Outcome o = run(c.target, ctx(m, session));
        ++count;
        if (session) {
          expect(o, 409, ContentType::Plain, "remote in control", ActionKind::None);
        } else if (m == OutputMode::Fault && c.modeApplies) {
          expect(o, 503, ContentType::Plain, "servo output fault", ActionKind::None);
        } else {
          TEST_ASSERT_EQUAL_INT_MESSAGE(200, o.result.status, c.target);
          TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(c.passed),
                                    static_cast<int>(o.result.action.kind), c.target);
        }
      }
    }
  }
  TEST_ASSERT_EQUAL_INT(32, count);
}

static void test_lock_before_validation() {
  for (OutputMode m : kModes) {
    for (const char* t : {"/controller?servo=3", "/controller?pm=2a", "/controller?pm=",
                          "/controller?pm=2&servo=1&value=90", "/online?m0=999", "/online",
                          "/save?key=50&value=3", "/controller?servo=3&value=45",
                          "/controller?pm=13", "/controller?pm=100"}) {
      expect(run(t, ctx(m, true)), 409, ContentType::Plain, "remote in control", ActionKind::None);
    }
  }
}

static void test_fault_before_validation() {
  for (const char* t : {"/controller?servo=3", "/controller?pm=2a", "/online?m0=999",
                        "/controller?pm=2", "/controller?servo=0&value=135", "/controller?pm=21"}) {
    expect(run(t, ctx(OutputMode::Fault)), 503, ContentType::Plain, "servo output fault",
           ActionKind::None);
  }
  // Validation applies after the mode table when output is off.
  expectNoAction(run("/controller?servo=3", ctx(OutputMode::Safe)), 400);
  expectNoAction(run("/controller?servo=3", ctx(OutputMode::Idle)), 400);
}

static void test_pages_in_every_state() {
  struct P {
    const char* path;
    Page page;
  };
  const P pages[] = {{"/", Page::Controller},
                     {"/zero", Page::Zero},
                     {"/setting", Page::Setting},
                     {"/editor", Page::Editor}};
  for (const P& p : pages) {
    for (OutputMode m : kModes) {
      for (bool session : {false, true}) {
        for (bool busy : {false, true}) expectPage(run(p.path, ctx(m, session, busy)), p.page);
      }
    }
  }
  // Query arguments do not change a page.
  expectPage(run("/?pm=2", ctx()), Page::Controller);
}

// Portal server and address: unknown paths and other methods.
static void test_not_found() {
  expect(run("/generate_204", ctx()), 404, ContentType::Plain, "Not found: /generate_204",
         ActionKind::None);
  expect(run("/controller/", ctx()), 404, ContentType::Plain, "Not found: /controller/",
         ActionKind::None);
  expect(run("/controller?pm=2", ctx(), Method::Other), 404, ContentType::Plain,
         "Not found: /controller", ActionKind::None);
  expect(run("/", ctx(), Method::Other), 404, ContentType::Plain, "Not found: /",
         ActionKind::None);
  expect(run("/save?key=1&value=1", ctx(), Method::Other), 404, ContentType::Plain,
         "Not found: /save", ActionKind::None);
}

static void test_lock_ends_with_session() {
  expect(run("/controller?pm=1", ctx(OutputMode::Safe, true)), 409, ContentType::Plain,
         "remote in control", ActionKind::None);
  const Outcome o = run("/controller?pm=1", ctx(OutputMode::Safe, false));
  expect(o, 200, ContentType::Html, "(pm)=(1)", ActionKind::RequestMotion);
  TEST_ASSERT_EQUAL_INT(1, o.result.action.motion);
}

static void test_first_argument_wins() {
  const Outcome o = run("/controller?pm=3&pm=4", ctx());
  expect(o, 200, ContentType::Html, "(pm)=(3)", ActionKind::RequestMotion);
  TEST_ASSERT_EQUAL_INT(3, o.result.action.motion);
}

// carryOut: the calls each action makes, their order, and rejections.
static void test_carry_out_calls() {
  Recorder rec;
  Outcome o = serve("/controller?pm=9", ctx(), rec);
  TEST_ASSERT_EQUAL_STRING("(pm)=(9)", o.body.c_str());
  TEST_ASSERT_EQUAL_STRING("request 9", rec.joined().c_str());

  rec = Recorder();
  o = serve("/controller?pm=21", ctx(), rec);
  TEST_ASSERT_EQUAL_INT(200, o.result.status);
  TEST_ASSERT_EQUAL_STRING("", rec.joined().c_str());

  // Stop first, then the direct servo command, unchanged (no enable call exists).
  for (OutputMode m : {OutputMode::Idle, OutputMode::Active, OutputMode::Safe}) {
    rec = Recorder();
    o = serve("/controller?servo=0&value=135", ctx(m, false, true), rec);
    TEST_ASSERT_EQUAL_INT(200, o.result.status);
    TEST_ASSERT_EQUAL_STRING("(pm)=()", o.body.c_str());
    TEST_ASSERT_EQUAL_STRING("stop; servo 0 135", rec.joined().c_str());
  }

  rec = Recorder();
  o = serve("/save?key=1&value=-5", ctx(), rec);
  TEST_ASSERT_EQUAL_STRING("(key, value)=(1,-5)", o.body.c_str());
  TEST_ASSERT_EQUAL_STRING("trim 1 -5", rec.joined().c_str());  // no servo, motion or stop

  rec = Recorder();
  o = serve("/save?key=100&value=abc", ctx(), rec);
  TEST_ASSERT_EQUAL_STRING("(key, value)=(100,0)", o.body.c_str());
  TEST_ASSERT_EQUAL_STRING("reset", rec.joined().c_str());

  rec = Recorder();
  o = serve(kOnline, ctx(), rec);
  TEST_ASSERT_EQUAL_STRING("(m0, m1)=(70,90)", o.body.c_str());
  TEST_ASSERT_EQUAL_STRING("keyframe 70 90 90 110 110 90 90 70 1000", rec.joined().c_str());
}

static void test_carry_out_rejections() {
  Recorder rec;
  rec.servoOk = false;
  Outcome o = serve("/controller?servo=3&value=80", ctx(OutputMode::Safe), rec);
  TEST_ASSERT_EQUAL_INT(503, o.result.status);
  TEST_ASSERT_EQUAL(static_cast<int>(ContentType::Plain), static_cast<int>(o.result.type));
  TEST_ASSERT_EQUAL_STRING("servo output off", o.body.c_str());
  TEST_ASSERT_EQUAL_STRING("stop; servo 3 80", rec.joined().c_str());

  rec = Recorder();
  rec.keyframeOk = false;
  o = serve(kOnline, ctx(), rec);
  TEST_ASSERT_EQUAL_INT(400, o.result.status);

  rec = Recorder();
  rec.trimOk = false;
  o = serve("/save?key=1&value=-5", ctx(), rec);
  TEST_ASSERT_EQUAL_INT(500, o.result.status);
  TEST_ASSERT_EQUAL_STRING("calibration not stored", o.body.c_str());

  rec = Recorder();
  rec.resetOk = false;
  o = serve("/save?key=100", ctx(), rec);
  TEST_ASSERT_EQUAL_INT(500, o.result.status);
}

// Every refusal makes no call to motion-playback, servo-output or servo-calibration.
static void test_refusals_make_no_calls() {
  const char* moving[] = {"/controller?pm=2",       "/controller?servo=0&value=135",
                          "/controller?servo=3",    kOnline,
                          "/save?key=0&value=2",    "/save?key=100",
                          "/controller?pm=100"};
  for (const char* t : moving) {
    for (OutputMode m : kModes) {
      Recorder rec;
      const Outcome o = serve(t, ctx(m, true), rec);
      TEST_ASSERT_EQUAL_INT_MESSAGE(409, o.result.status, t);
      TEST_ASSERT_EQUAL_STRING_MESSAGE("", rec.joined().c_str(), t);
    }
  }
  for (const char* t : {"/controller?pm=2", "/controller?servo=0&value=135", kOnline}) {
    Recorder rec;
    TEST_ASSERT_EQUAL_INT(503, serve(t, ctx(OutputMode::Fault), rec).result.status);
    TEST_ASSERT_EQUAL_STRING("", rec.joined().c_str());
  }
  for (const char* t : {"/controller?servo=3", "/controller?servo=9&value=90",
                        "/controller?pm=2&servo=3&value=90", "/save?key=50&value=3",
                        "/online?m0=70", "/controller?pm=2a"}) {
    Recorder rec;
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, serve(t, ctx(OutputMode::Safe), rec).result.status, t);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", rec.joined().c_str(), t);
  }
  Recorder rec;
  TEST_ASSERT_EQUAL_INT(409, serve("/save?key=1&value=-5", ctx(OutputMode::Active, false, true),
                                   rec).result.status);
  TEST_ASSERT_EQUAL_INT(404, serve("/controller?pm=2", ctx(), rec, Method::Other).result.status);
  TEST_ASSERT_EQUAL_INT(404, serve("/generate_204", ctx(), rec).result.status);
  TEST_ASSERT_EQUAL_STRING("", rec.joined().c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_integer);
  RUN_TEST(test_motion_request);
  RUN_TEST(test_motion_unknown_and_malformed);
  RUN_TEST(test_direct_servo);
  RUN_TEST(test_direct_servo_invalid);
  RUN_TEST(test_pm_and_servo_together);
  RUN_TEST(test_direct_servo_rejected_by_glue);
  RUN_TEST(test_pms_only);
  RUN_TEST(test_save_trim);
  RUN_TEST(test_save_reset_all);
  RUN_TEST(test_save_invalid);
  RUN_TEST(test_save_busy_session_and_modes);
  RUN_TEST(test_online_valid);
  RUN_TEST(test_online_ranges);
  RUN_TEST(test_online_rejected_by_glue);
  RUN_TEST(test_mode_session_matrix);
  RUN_TEST(test_lock_before_validation);
  RUN_TEST(test_fault_before_validation);
  RUN_TEST(test_pages_in_every_state);
  RUN_TEST(test_not_found);
  RUN_TEST(test_lock_ends_with_session);
  RUN_TEST(test_first_argument_wins);
  RUN_TEST(test_carry_out_calls);
  RUN_TEST(test_carry_out_rejections);
  RUN_TEST(test_refusals_make_no_calls);
  return UNITY_END();
}
