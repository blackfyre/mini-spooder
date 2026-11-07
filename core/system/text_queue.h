// A fixed-size byte queue for diagnostic output, so that a long reply (the check
// console's status) is written to the UART over several passes, never blocking one.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ms {

template <size_t N>
class TextQueue {
 public:
  // Appends as much of `text` as fits; returns the number of bytes dropped.
  size_t push(const char* text, size_t len) {
    size_t dropped = 0;
    for (size_t i = 0; i < len; ++i) {
      if (count_ == N) {
        ++dropped;
        continue;
      }
      buf_[(head_ + count_) % N] = text[i];
      ++count_;
    }
    return dropped;
  }

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }

  // Copies up to `max` bytes from the front into `out` and removes them.
  size_t pop(char* out, size_t max) {
    size_t n = 0;
    while (n < max && count_ > 0) {
      out[n++] = buf_[head_];
      head_ = (head_ + 1) % N;
      --count_;
    }
    return n;
  }

 private:
  char buf_[N] = {};
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace ms
