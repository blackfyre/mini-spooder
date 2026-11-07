#include "diag.h"

#include <Arduino.h>
#include <string.h>

#include "system/text_queue.h"

namespace diag {
namespace {

ms::TextQueue<1536> queue;

}  // namespace

void begin() { Serial.begin(115200); }

void line(const char* text) {
  queue.push(text, strlen(text));
  queue.push("\r\n", 2);
  poll();
}

void poll() {
  char chunk[64];
  int room = Serial.availableForWrite();
  while (room > 0 && !queue.empty()) {
    const size_t want = static_cast<size_t>(room) < sizeof chunk ? static_cast<size_t>(room) : sizeof chunk;
    const size_t n = queue.pop(chunk, want);
    Serial.write(reinterpret_cast<const uint8_t*>(chunk), n);
    room -= static_cast<int>(n);
  }
}

void flush() {
  while (!queue.empty()) {
    poll();
    yield();
  }
  Serial.flush();
}

}  // namespace diag
