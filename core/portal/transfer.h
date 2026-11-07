// The web portal's response sender (web-portal, design decision 10): one HTTP
// response written across loop passes, never waiting inside a pass.
// Hardware-free: the firmware glue implements Connection on a WiFiClient.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ms {
namespace portal {

// The client connection as the sender sees it. No call may wait.
class Connection {
 public:
  virtual ~Connection() = default;
  // The client is still connected.
  virtual bool open() = 0;
  // Bytes write() takes now without waiting (the free send buffer).
  virtual size_t room() = 0;
  // Writes up to n bytes (n <= room()) from RAM, or from flash when `flash`;
  // returns the bytes taken.
  virtual size_t write(const char* data, size_t n, bool flash) = 0;
  // Every byte written has been acknowledged by the client.
  virtual bool sent() = 0;
  // Closes the connection without waiting; the sender drops it afterwards.
  virtual void close() = 0;
};

// One part of a response body: bytes in RAM (copied by begin) or in flash
// (PROGMEM, which must stay valid until the transfer ends).
struct Piece {
  const char* data;
  size_t length;
  bool flash;
};

enum class Outcome : uint8_t {
  None,      // no transfer has ended yet
  Done,      // the whole response was written and acknowledged, then closed
  Gone,      // the client disconnected first
  Stalled,   // no progress for kStallMs; closed
  Refused,   // begin() could not start (too many pieces, no memory); closed
};

class Transfer {
 public:
  // Most pieces in one body (the calibration page: 9 parts and 8 trims).
  static constexpr size_t kMaxPieces = 20;
  // Most bytes handed to one write() call.
  static constexpr size_t kChunk = 512;
  // A transfer with no progress for this long is closed. Progress is bytes
  // written, or send-buffer room freed by the client's acknowledgements.
  static constexpr uint32_t kStallMs = 2000;

  Transfer() = default;
  ~Transfer();
  Transfer(const Transfer&) = delete;
  Transfer& operator=(const Transfer&) = delete;

  bool busy() const { return connection_ != nullptr; }
  Outcome lastOutcome() const { return outcome_; }

  // Starts a response on `connection`: the status line (HTTP/1.<httpMinor>),
  // Content-Type, Content-Length (the sum of the pieces) and Connection: close,
  // then the pieces in order. Writes nothing yet; step() does. Returns false,
  // closing the connection, when the pieces do not fit or memory is short.
  // Must not be called while busy().
  bool begin(Connection& connection, int httpMinor, int status, const char* contentType,
             const Piece* pieces, size_t count, uint32_t nowMs);

  // One loop pass: writes what the connection takes without waiting, closes
  // it when the response is acknowledged, or aborts when the client is gone or
  // stalled.
  void step(uint32_t nowMs);

 private:
  void finish(Outcome outcome);

  Connection* connection_ = nullptr;
  char* ram_ = nullptr;  // the status line, headers and every RAM piece
  Piece pieces_[kMaxPieces + 1];
  size_t count_ = 0;
  size_t index_ = 0;   // current piece
  size_t offset_ = 0;  // bytes of it written
  size_t roomAfter_ = 0;  // the connection's room when the last pass ended
  uint32_t lastProgressMs_ = 0;
  Outcome outcome_ = Outcome::None;
};

// The reason phrase for a status the portal sends ("OK", "Not Found", ...).
const char* reasonPhrase(int status);

}  // namespace portal
}  // namespace ms
