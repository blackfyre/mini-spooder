#include "portal/transfer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ms {
namespace portal {

const char* reasonPhrase(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 409:
      return "Conflict";
    case 500:
      return "Internal Server Error";
    case 503:
      return "Service Unavailable";
    default:
      return "";
  }
}

Transfer::~Transfer() { free(ram_); }

bool Transfer::begin(Connection& connection, int httpMinor, int status, const char* contentType,
                     const Piece* pieces, size_t count, uint32_t nowMs) {
  connection_ = &connection;
  lastProgressMs_ = nowMs;
  index_ = 0;
  offset_ = 0;
  count_ = 0;
  roomAfter_ = connection.room();
  if (count > kMaxPieces || (count > 0 && pieces == nullptr)) {
    finish(Outcome::Refused);
    return false;
  }
  size_t bodyLength = 0;
  size_t ramLength = 0;
  for (size_t i = 0; i < count; ++i) {
    bodyLength += pieces[i].length;
    if (!pieces[i].flash) ramLength += pieces[i].length;
  }
  char head[160];
  const int n = snprintf(head, sizeof head,
                         "HTTP/1.%d %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
                         "Connection: close\r\n\r\n",
                         httpMinor > 0 ? 1 : 0, status, reasonPhrase(status),
                         contentType != nullptr ? contentType : "text/plain",
                         static_cast<unsigned long>(bodyLength));
  if (n <= 0 || static_cast<size_t>(n) >= sizeof head) {
    finish(Outcome::Refused);
    return false;
  }
  const size_t headLength = static_cast<size_t>(n);
  ram_ = static_cast<char*>(malloc(headLength + ramLength));
  if (ram_ == nullptr) {
    finish(Outcome::Refused);
    return false;
  }
  memcpy(ram_, head, headLength);
  pieces_[0] = Piece{ram_, headLength, false};
  size_t at = headLength;
  for (size_t i = 0; i < count; ++i) {
    Piece p = pieces[i];
    if (!p.flash && p.length > 0) {
      memcpy(ram_ + at, p.data, p.length);
      p.data = ram_ + at;
      at += p.length;
    }
    pieces_[i + 1] = p;
  }
  count_ = count + 1;
  return true;
}

void Transfer::step(uint32_t nowMs) {
  if (connection_ == nullptr) return;
  Connection& c = *connection_;
  if (!c.open()) {
    finish(Outcome::Gone);
    return;
  }
  // Room freed since the last pass is data the client acknowledged: progress.
  if (c.room() > roomAfter_) lastProgressMs_ = nowMs;
  while (index_ < count_) {
    const Piece& p = pieces_[index_];
    const size_t left = p.length - offset_;
    if (left == 0) {
      ++index_;
      offset_ = 0;
      continue;
    }
    size_t n = c.room();
    if (n > left) n = left;
    if (n > kChunk) n = kChunk;
    if (n == 0) break;
    size_t taken = c.write(p.data + offset_, n, p.flash);
    if (taken > n) taken = n;
    if (taken == 0) break;
    offset_ += taken;
    lastProgressMs_ = nowMs;
    if (taken < n) break;  // the buffer filled up: continue in a later pass
  }
  roomAfter_ = c.room();
  if (index_ == count_ && c.sent()) {
    finish(Outcome::Done);
    return;
  }
  if (nowMs - lastProgressMs_ >= kStallMs) finish(Outcome::Stalled);
}

void Transfer::finish(Outcome outcome) {
  if (connection_ != nullptr) connection_->close();
  connection_ = nullptr;
  free(ram_);
  ram_ = nullptr;
  count_ = 0;
  index_ = 0;
  offset_ = 0;
  outcome_ = outcome;
}

}  // namespace portal
}  // namespace ms
