// Host tests of the web portal's response sender (web-portal task 4.3).
#include <stdint.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "portal/transfer.h"

using namespace ms::portal;

namespace {

// A client whose send buffer frees `window[i]` bytes before pass i (the last
// value repeats), that can disappear and that acknowledges only when told.
class FakeConnection : public Connection {
 public:
  std::vector<size_t> window{4096};
  size_t pass = 0;
  size_t free = 0;
  bool connected = true;
  bool acked = true;  // sent() once everything is written
  bool closed = false;
  size_t writes = 0;
  size_t largestWrite = 0;
  size_t flashBytes = 0;
  std::string received;

  void nextPass() {
    free = window[pass < window.size() ? pass : window.size() - 1];
    ++pass;
  }
  bool open() override { return connected; }
  size_t room() override { return free; }
  size_t takeAtMost = SIZE_MAX;  // a write that takes less (lwIP out of segments)

  size_t write(const char* data, size_t n, bool flash) override {
    TEST_ASSERT_FALSE(closed);
    TEST_ASSERT_TRUE(n <= free);  // never more than fits without waiting
    if (n > takeAtMost) n = takeAtMost;
    received.append(data, n);
    free -= n;
    ++writes;
    if (n > largestWrite) largestWrite = n;
    if (flash) flashBytes += n;
    return n;
  }
  bool sent() override { return acked; }
  void close() override {
    TEST_ASSERT_FALSE(closed);
    closed = true;
  }
};

// A page much larger than one send buffer, as the stock pages are.
std::string bigPage() {
  std::string s;
  for (int i = 0; s.size() < 9000; ++i) s += "<p>line " + std::to_string(i) + "</p>\n";
  return s;
}

// Runs passes until the transfer ends, at most `limit` passes, 10 ms apart.
int runToEnd(Transfer& t, FakeConnection& c, uint32_t& now, int limit = 10000) {
  int passes = 0;
  while (t.busy() && passes < limit) {
    c.nextPass();
    t.step(now);
    now += 10;
    ++passes;
  }
  return passes;
}

std::string header(int status, const char* reason, const char* type, size_t length,
                   int minor = 1) {
  return "HTTP/1." + std::to_string(minor) + " " + std::to_string(status) + " " + reason +
         "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(length) +
         "\r\nConnection: close\r\n\r\n";
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_whole_page_arrives_byte_exact_through_a_varying_window() {
  const std::string page = bigPage();
  FakeConnection c;
  c.window = {0, 3, 536, 1, 0, 1072, 97, 2920, 0, 17};
  Transfer t;
  const Piece p{page.c_str(), page.size(), true};
  uint32_t now = 1000;
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/html", &p, 1, now));
  TEST_ASSERT_TRUE(c.received.empty());  // begin writes nothing
  const int passes = runToEnd(t, c, now);
  TEST_ASSERT_FALSE(t.busy());
  TEST_ASSERT_TRUE(c.closed);
  TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Done), static_cast<int>(t.lastOutcome()));
  TEST_ASSERT_TRUE(passes > 1);  // spread over passes, not written in one
  TEST_ASSERT_EQUAL_STRING((header(200, "OK", "text/html", page.size()) + page).c_str(),
                           c.received.c_str());
  TEST_ASSERT_TRUE(c.largestWrite <= Transfer::kChunk);
  TEST_ASSERT_EQUAL(page.size(), c.flashBytes);
}

void test_one_pass_writes_no_more_than_the_window() {
  const std::string page = bigPage();
  FakeConnection c;
  c.window = {700};
  Transfer t;
  const Piece p{page.c_str(), page.size(), true};
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/html", &p, 1, 0));
  c.nextPass();
  t.step(0);
  TEST_ASSERT_EQUAL(700, c.received.size());
  TEST_ASSERT_TRUE(t.busy());
}

void test_short_writes_end_the_pass_and_lose_nothing() {
  const std::string page = bigPage();
  FakeConnection c;
  c.window = {1500};
  c.takeAtMost = 100;
  Transfer t;
  const Piece p{page.c_str(), page.size(), true};
  uint32_t now = 0;
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/html", &p, 1, now));
  c.nextPass();
  t.step(now);
  TEST_ASSERT_EQUAL(2, c.writes);  // the header, then a short write that ends the pass
  runToEnd(t, c, now);
  TEST_ASSERT_EQUAL_STRING((header(200, "OK", "text/html", page.size()) + page).c_str(),
                           c.received.c_str());
}

void test_pieces_in_order_with_ram_pieces_copied() {
  // The calibration page's shape: flash parts with trims between them.
  char trim[8];
  strcpy(trim, "-5");
  const char* part0 = "<input value=\"";
  const char* part1 = "\">";
  Piece pieces[] = {{part0, strlen(part0), true}, {trim, strlen(trim), false},
                    {part1, strlen(part1), true}, {"", 0, false}};
  FakeConnection c;
  c.window = {5};
  Transfer t;
  uint32_t now = 0;
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/html", pieces, 4, now));
  strcpy(trim, "99");  // the caller's buffer may change once begin returns
  runToEnd(t, c, now);
  const std::string body = "<input value=\"-5\">";
  TEST_ASSERT_EQUAL_STRING((header(200, "OK", "text/html", body.size()) + body).c_str(),
                           c.received.c_str());
}

void test_status_line_follows_request_version_and_status() {
  const char* body = "Not found: /x";
  const Piece p{body, strlen(body), false};
  FakeConnection c;
  Transfer t;
  uint32_t now = 0;
  TEST_ASSERT_TRUE(t.begin(c, 0, 404, "text/plain", &p, 1, now));
  runToEnd(t, c, now);
  TEST_ASSERT_EQUAL_STRING((header(404, "Not Found", "text/plain", 13, 0) + body).c_str(),
                           c.received.c_str());
}

void test_client_that_disappears_is_closed_at_once() {
  const std::string page = bigPage();
  FakeConnection c;
  c.window = {600};
  Transfer t;
  const Piece p{page.c_str(), page.size(), true};
  uint32_t now = 0;
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/html", &p, 1, now));
  c.nextPass();
  t.step(now);
  c.connected = false;
  const size_t before = c.received.size();
  c.nextPass();
  t.step(now + 10);
  TEST_ASSERT_FALSE(t.busy());
  TEST_ASSERT_TRUE(c.closed);
  TEST_ASSERT_EQUAL(before, c.received.size());
  TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Gone), static_cast<int>(t.lastOutcome()));
}

void test_client_that_stops_reading_is_closed_after_the_stall_time() {
  const std::string page = bigPage();
  FakeConnection c;
  c.window = {1000, 0};  // one window, then the client reads nothing more
  Transfer t;
  const Piece p{page.c_str(), page.size(), true};
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/html", &p, 1, 0));
  c.nextPass();
  t.step(0);
  for (uint32_t now = 10; now < Transfer::kStallMs; now += 10) {
    c.nextPass();
    t.step(now);
    TEST_ASSERT_TRUE(t.busy());
  }
  c.nextPass();
  t.step(Transfer::kStallMs);
  TEST_ASSERT_FALSE(t.busy());
  TEST_ASSERT_TRUE(c.closed);
  TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Stalled), static_cast<int>(t.lastOutcome()));
}

void test_slow_but_progressing_client_is_served() {
  const std::string page = bigPage();
  FakeConnection c;
  c.window = std::vector<size_t>(140, 0);  // 1.4 s of nothing at 10 ms a pass
  c.window.push_back(2000);                // then room for the rest each pass
  Transfer t;
  const Piece p{page.c_str(), page.size(), true};
  uint32_t now = 0;
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/html", &p, 1, now));
  runToEnd(t, c, now);
  TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Done), static_cast<int>(t.lastOutcome()));
  TEST_ASSERT_EQUAL(header(200, "OK", "text/html", page.size()).size() + page.size(),
                    c.received.size());
}

void test_close_waits_for_acknowledgement_across_passes() {
  const char* body = "(pm)=()";
  const Piece p{body, strlen(body), false};
  FakeConnection c;
  c.acked = false;
  Transfer t;
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/html", &p, 1, 0));
  c.nextPass();
  t.step(0);
  TEST_ASSERT_TRUE(t.busy());
  TEST_ASSERT_FALSE(c.closed);
  c.acked = true;
  c.nextPass();
  t.step(10);
  TEST_ASSERT_FALSE(t.busy());
  TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Done), static_cast<int>(t.lastOutcome()));
}

void test_unacknowledged_response_is_closed_after_the_stall_time() {
  const char* body = "x";
  const Piece p{body, 1, false};
  FakeConnection c;
  c.acked = false;
  Transfer t;
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/plain", &p, 1, 0));
  c.nextPass();
  t.step(0);
  // No acknowledgement: the send buffer frees nothing more.
  t.step(Transfer::kStallMs - 1);
  TEST_ASSERT_TRUE(t.busy());
  t.step(Transfer::kStallMs);
  TEST_ASSERT_FALSE(t.busy());
  TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Stalled), static_cast<int>(t.lastOutcome()));
}

void test_acknowledgements_count_as_progress() {
  const std::string page = bigPage();
  FakeConnection c;
  c.window = {page.size() + 200};  // everything fits in the first pass
  c.acked = false;
  Transfer t;
  const Piece p{page.c_str(), page.size(), true};
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/html", &p, 1, 0));
  c.nextPass();
  t.step(0);
  TEST_ASSERT_TRUE(t.busy());
  // The client acknowledges a little every 500 ms, well past the stall time.
  uint32_t now = 0;
  for (int i = 0; i < 10; ++i) {
    now += 500;
    c.free += 10;
    t.step(now);
    TEST_ASSERT_TRUE(t.busy());
  }
  // Then nothing more: closed 2 s after the last acknowledgement.
  t.step(now + Transfer::kStallMs - 1);
  TEST_ASSERT_TRUE(t.busy());
  t.step(now + Transfer::kStallMs);
  TEST_ASSERT_FALSE(t.busy());
  TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Stalled), static_cast<int>(t.lastOutcome()));
}

void test_stall_time_survives_millis_wrap() {
  const char* body = "x";
  const Piece p{body, 1, false};
  FakeConnection c;
  c.window = {0};
  Transfer t;
  const uint32_t start = 0xFFFFFF00u;
  TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/plain", &p, 1, start));
  c.nextPass();
  t.step(start + 100);  // wraps
  TEST_ASSERT_TRUE(t.busy());
  c.nextPass();
  t.step(start + Transfer::kStallMs);
  TEST_ASSERT_FALSE(t.busy());
}

void test_too_many_pieces_are_refused_and_closed() {
  Piece pieces[Transfer::kMaxPieces + 1];
  for (auto& p : pieces) p = Piece{"a", 1, false};
  FakeConnection c;
  Transfer t;
  TEST_ASSERT_FALSE(t.begin(c, 1, 200, "text/plain", pieces, Transfer::kMaxPieces + 1, 0));
  TEST_ASSERT_FALSE(t.busy());
  TEST_ASSERT_TRUE(c.closed);
  TEST_ASSERT_TRUE(c.received.empty());
  TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Refused), static_cast<int>(t.lastOutcome()));
}

void test_transfer_is_reusable() {
  const char* body = "ok";
  const Piece p{body, 2, false};
  Transfer t;
  for (int i = 0; i < 3; ++i) {
    FakeConnection c;
    uint32_t now = 0;
    TEST_ASSERT_TRUE(t.begin(c, 1, 200, "text/plain", &p, 1, now));
    runToEnd(t, c, now);
    TEST_ASSERT_EQUAL_STRING((header(200, "OK", "text/plain", 2) + body).c_str(),
                             c.received.c_str());
  }
}

void test_reason_phrases() {
  TEST_ASSERT_EQUAL_STRING("OK", reasonPhrase(200));
  TEST_ASSERT_EQUAL_STRING("Bad Request", reasonPhrase(400));
  TEST_ASSERT_EQUAL_STRING("Not Found", reasonPhrase(404));
  TEST_ASSERT_EQUAL_STRING("Conflict", reasonPhrase(409));
  TEST_ASSERT_EQUAL_STRING("Internal Server Error", reasonPhrase(500));
  TEST_ASSERT_EQUAL_STRING("Service Unavailable", reasonPhrase(503));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_whole_page_arrives_byte_exact_through_a_varying_window);
  RUN_TEST(test_one_pass_writes_no_more_than_the_window);
  RUN_TEST(test_short_writes_end_the_pass_and_lose_nothing);
  RUN_TEST(test_pieces_in_order_with_ram_pieces_copied);
  RUN_TEST(test_status_line_follows_request_version_and_status);
  RUN_TEST(test_client_that_disappears_is_closed_at_once);
  RUN_TEST(test_client_that_stops_reading_is_closed_after_the_stall_time);
  RUN_TEST(test_slow_but_progressing_client_is_served);
  RUN_TEST(test_close_waits_for_acknowledgement_across_passes);
  RUN_TEST(test_unacknowledged_response_is_closed_after_the_stall_time);
  RUN_TEST(test_acknowledgements_count_as_progress);
  RUN_TEST(test_stall_time_survives_millis_wrap);
  RUN_TEST(test_too_many_pieces_are_refused_and_closed);
  RUN_TEST(test_transfer_is_reusable);
  RUN_TEST(test_reason_phrases);
  return UNITY_END();
}
