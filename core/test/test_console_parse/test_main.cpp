#include <string.h>
#include <unity.h>

#include "check/console_parse.h"

using namespace ms;
using namespace ms::check;

namespace {

bool parse(const char* s, Command& c) { return parseLine(s, strlen(s), c); }

void assertRejected(const char* s) {
  Command c;
  TEST_ASSERT_FALSE_MESSAGE(parse(s, c), s);
}

}  // namespace

void setUp() {}
void tearDown() {}

static void test_enable_and_pose() {
  Command c;
  TEST_ASSERT_TRUE(parse("enable 70 90 90 110 110 90 90 70", c));
  TEST_ASSERT_EQUAL(CommandKind::Enable, c.kind);
  const int expected[8] = {70, 90, 90, 110, 110, 90, 90, 70};
  for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_INT(expected[i], c.pose.angle[i]);

  TEST_ASSERT_TRUE(parse("pose 1 2 3 4 5 6 7 -1\r", c));
  TEST_ASSERT_EQUAL(CommandKind::Pose, c.kind);
  TEST_ASSERT_EQUAL_INT(-1, c.pose.angle[7]);  // passed on; the gate rejects it
}

static void test_servo() {
  Command c;
  TEST_ASSERT_TRUE(parse("servo 0 80", c));
  TEST_ASSERT_EQUAL(CommandKind::Servo, c.kind);
  TEST_ASSERT_EQUAL_INT(0, c.servo);
  TEST_ASSERT_EQUAL_INT(80, c.value);
  TEST_ASSERT_TRUE(parse("  servo\t7  181 ", c));
  TEST_ASSERT_EQUAL_INT(7, c.servo);
  TEST_ASSERT_EQUAL_INT(181, c.value);
}

static void test_release() {
  Command c;
  TEST_ASSERT_TRUE(parse("release 3", c));
  TEST_ASSERT_EQUAL(CommandKind::Release, c.kind);
  TEST_ASSERT_EQUAL_INT(3, c.servo);
  TEST_ASSERT_TRUE(parse("release all", c));
  TEST_ASSERT_EQUAL(CommandKind::ReleaseAll, c.kind);
}

static void test_trim() {
  Command c;
  TEST_ASSERT_TRUE(parse("trim 4 -7", c));
  TEST_ASSERT_EQUAL(CommandKind::Trim, c.kind);
  TEST_ASSERT_EQUAL_INT(4, c.servo);
  TEST_ASSERT_EQUAL_INT(-7, c.value);
  TEST_ASSERT_TRUE(parse("trim 7 +12", c));
  TEST_ASSERT_EQUAL_INT(12, c.value);
  TEST_ASSERT_TRUE(parse("trim 2 125", c));  // passed on; the trim table rejects it
  TEST_ASSERT_EQUAL_INT(125, c.value);
}

static void test_bare_commands() {
  Command c;
  TEST_ASSERT_TRUE(parse("safe", c));
  TEST_ASSERT_EQUAL(CommandKind::Safe, c.kind);
  TEST_ASSERT_TRUE(parse("trimreset", c));
  TEST_ASSERT_EQUAL(CommandKind::TrimReset, c.kind);
  TEST_ASSERT_TRUE(parse("status", c));
  TEST_ASSERT_EQUAL(CommandKind::Status, c.kind);
  TEST_ASSERT_TRUE(parse("restart", c));
  TEST_ASSERT_EQUAL(CommandKind::Restart, c.kind);
  TEST_ASSERT_TRUE(parse("hang\r", c));
  TEST_ASSERT_EQUAL(CommandKind::Hang, c.kind);
}

static void test_malformed() {
  assertRejected("servo 3");
  assertRejected("servo 9 90");
  assertRejected("servo -1 90");
  assertRejected("enable 1 2 3");
  assertRejected("enable 1 2 3 4 5 6 7 8 9");
  assertRejected("");
  assertRejected("   ");
  assertRejected("\r");
  assertRejected("servo 3 eighty");
  assertRejected("servo 3 8x");
  assertRejected("servo 3 -");
  assertRejected("servo 3 1000");
  assertRejected("release");
  assertRejected("release 8");
  assertRejected("release everything");
  assertRejected("trim 9 0");
  assertRejected("trim 1");
  assertRejected("safe now");
  assertRejected("status 1");
  assertRejected("Status");
  assertRejected("hangs");
  assertRejected("fly 1 2");
  assertRejected("servo 1 2 3 4 5 6 7 8 9 10 11");
  const char withNul[] = {'s', 'a', 'f', 'e', '\0', 'x'};
  Command c;
  TEST_ASSERT_FALSE(parseLine(withNul, sizeof withNul, c));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_enable_and_pose);
  RUN_TEST(test_servo);
  RUN_TEST(test_release);
  RUN_TEST(test_trim);
  RUN_TEST(test_bare_commands);
  RUN_TEST(test_malformed);
  return UNITY_END();
}
