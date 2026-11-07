#include <string.h>
#include <unity.h>

#include "system/text_queue.h"

using namespace ms;

void setUp() {}
void tearDown() {}

static void test_fifo_across_wrap() {
  TextQueue<8> q;
  char out[16];
  TEST_ASSERT_EQUAL_size_t(0, q.push("abcdef", 6));
  TEST_ASSERT_EQUAL_size_t(4, q.pop(out, 4));
  TEST_ASSERT_EQUAL_MEMORY("abcd", out, 4);
  TEST_ASSERT_EQUAL_size_t(0, q.push("ghijkl", 6));  // wraps
  TEST_ASSERT_EQUAL_size_t(8, q.size());
  TEST_ASSERT_EQUAL_size_t(8, q.pop(out, sizeof out));
  TEST_ASSERT_EQUAL_MEMORY("efghijkl", out, 8);
  TEST_ASSERT_TRUE(q.empty());
}

static void test_overflow_drops_tail() {
  TextQueue<4> q;
  char out[8];
  TEST_ASSERT_EQUAL_size_t(2, q.push("abcdef", 6));
  TEST_ASSERT_EQUAL_size_t(4, q.pop(out, sizeof out));
  TEST_ASSERT_EQUAL_MEMORY("abcd", out, 4);
  TEST_ASSERT_EQUAL_size_t(0, q.pop(out, sizeof out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fifo_across_wrap);
  RUN_TEST(test_overflow_drops_tail);
  return UNITY_END();
}
