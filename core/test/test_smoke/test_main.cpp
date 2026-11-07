// The first host test: the core library links and Unity runs.
#include <unity.h>

#include "servo/pulse.h"

void setUp() {}
void tearDown() {}

static void test_library_links() {
  TEST_ASSERT_EQUAL_INT(8, ms::kServoCount);
  TEST_ASSERT_EQUAL_UINT16(1394, ms::pulseWidthUs(90));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_library_links);
  return UNITY_END();
}
