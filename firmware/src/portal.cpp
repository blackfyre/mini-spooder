#include "portal.h"

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <lwip/tcp.h>  // TCP_SND_BUF: the send buffer when everything is acknowledged
#include <stdio.h>

#include <memory>

#include "calibration/trim_table.h"
#include "portal/request_handler.h"
#include "portal/transfer.h"
#include "portal_motion.h"
#include "portal_pages.h"  // generated into the build directory (scripts/portal_pages.py)

namespace portal {

namespace {

namespace ph = ms::portal;

// Longest wait for a request line or header that has only partly arrived. The
// library reads them with the client's stream timeout (5 s for a WiFiClient),
// which would hold the main loop; complete requests are unaffected.
constexpr unsigned long kParseTimeoutMs = 20;

// ESP8266WebServer with the parse timeout applied before the library reads a
// request, including the first request of a newly accepted connection. The
// library only parses and dispatches: every response is written by the
// portal's sender (ms::portal::Transfer) across loop passes (design decision 10).
class PortalServer : public ESP8266WebServer {
 public:
  explicit PortalServer(int port) : ESP8266WebServer(port) {}

  void serve() {
    if (_currentStatus == HC_NONE) {
      // The library's own accept step (handleClient), with the timeout set first.
      WiFiClient client = _server.accept();
      if (!client) return;
      client.setTimeout(kParseTimeoutMs);
      _currentClient = client;
      _currentStatus = HC_WAIT_READ;
      _statusChange = millis();
    } else {
      _currentClient.setTimeout(kParseTimeoutMs);
    }
    handleClient();
  }

  // The request's HTTP minor version (0 or 1), set once the request line is read.
  int httpMinor() const { return _currentVersion; }

  // Drops the library's hold on the connection the sender has taken over.
  void release() {
    _currentClient = WiFiClient();
    _currentStatus = HC_NONE;
  }
};

// The sender's view of the connection. No call waits: writes are at most the
// free send buffer, with a zero write timeout.
class ClientConnection : public ph::Connection {
 public:
  WiFiClient client;

  bool open() override { return client.connected(); }
  size_t room() override {
    const int n = client.availableForWrite();
    return n > 0 ? static_cast<size_t>(n) : 0;
  }
  size_t write(const char* data, size_t n, bool flash) override {
    if (!flash) return client.write(reinterpret_cast<const uint8_t*>(data), n);
    char buf[ph::Transfer::kChunk];
    if (n > sizeof buf) n = sizeof buf;
    memcpy_P(buf, data, n);
    return client.write(reinterpret_cast<const uint8_t*>(buf), n);
  }
  bool sent() override { return client.availableForWrite() >= static_cast<int>(TCP_SND_BUF); }
  // Drops the last reference (the library has released its own), which closes
  // the connection through lwIP without the flush wait of stop().
  void close() override { client = WiFiClient(); }
};

PortalServer server(80);
ClientConnection connection;
ph::Transfer transfer;
bool handedOver = false;  // the current request's connection went to the sender
ms::OutputGate* gate = nullptr;
CalibrationStore* calibration = nullptr;

const char* contentType(ph::ContentType type) {
  return type == ph::ContentType::Html ? "text/html" : "text/plain";
}

// Hands the request's connection to the sender with the response to write.
void respond(WiFiClient& client, int status, const char* type, const ph::Piece* pieces,
             size_t count) {
  connection.client = client;
  connection.client.setTimeout(0);
  handedOver = true;
  transfer.begin(connection, server.httpMinor(), status, type, pieces, count, millis());
}

ph::Piece ram(const char* text) {
  return ph::Piece{text != nullptr ? text : "", text != nullptr ? strlen(text) : 0, false};
}

// head + echo + tail; the sender copies them, so `echo` may point into the request.
void sendBody(WiFiClient& client, const ph::Result& r) {
  const ph::Piece pieces[] = {ram(r.head), ram(r.echo), ram(r.tail)};
  respond(client, r.status, contentType(r.type), pieces, 3);
}

ph::Piece flash(const char* page, size_t length) { return ph::Piece{page, length, true}; }

// The calibration page from flash, each {{OFF<i>}} replaced by servo i's trim.
// The length is known up front, so HTTP/1.0 clients get the page too.
// The sender sums the pieces for Content-Length and copies the trims.
void sendSetting(WiFiClient& client) {
  static_assert(kPageSettingPartCount + ms::kServoCount <= ph::Transfer::kMaxPieces,
                "the calibration page has more pieces than the sender holds");
  const ms::TrimTable& trims = calibration->table();
  char values[ms::kServoCount][8];
  for (int i = 0; i < ms::kServoCount; ++i) {
    const int n = snprintf(values[i], sizeof values[i], "%d", trims.trim(i));
    if (n <= 0) values[i][0] = '\0';
  }
  ph::Piece pieces[ph::Transfer::kMaxPieces];
  size_t count = 0;
  for (size_t i = 0; i < kPageSettingPartCount; ++i) {
    pieces[count++] = flash(kPageSettingParts[i], kPageSettingPartLengths[i]);
    if (i < ms::kServoCount) pieces[count++] = ram(values[kPageSettingTrimAfterPart[i]]);
  }
  respond(client, 200, "text/html", pieces, count);
}

void sendPage(WiFiClient& client, ph::Page page) {
  ph::Piece piece;
  switch (page) {
    case ph::Page::Controller:
      piece = flash(kPageController, sizeof kPageController - 1);
      break;
    case ph::Page::Zero:
      piece = flash(kPageZero, sizeof kPageZero - 1);
      break;
    case ph::Page::Editor:
      piece = flash(kPageEditor, sizeof kPageEditor - 1);
      break;
    case ph::Page::Setting:
      sendSetting(client);
      return;
    case ph::Page::None:
    default:
      return;
  }
  respond(client, 200, "text/html", &piece, 1);
}

bool motionId(int32_t id) { return portal_motion::isMotionId(id); }

// The actions on the capabilities' interfaces. The portal holds no writer token
// and never calls enable: a direct servo command goes through servo-output's
// token-free direct servo command, which owns switching output on.
class Glue : public ph::Executor {
 public:
  void requestMotion(int32_t motion) override { portal_motion::request(motion); }
  void stopMotion() override { portal_motion::stop(); }
  bool directServo(int servo, int angle) override { return gate->directServo(servo, angle); }
  bool storeTrim(int servo, int value) override {
    // Index and range are checked already; a rejection is a failed flash commit.
    return calibration->setTrim(servo, value) != ms::TrimSetResult::Rejected;
  }
  bool resetTrims() override { return calibration->resetAll(); }
  bool playKeyframe(const uint8_t angles[ms::kServoCount], uint16_t durationMs) override {
    return portal_motion::playKeyframe(angles, durationMs);
  }
};

// Called by the library once the request line is read, before the headers.
// The installed ESP8266WebServer parses an unknown method (TRACE, PATCH, ...) as
// GET, and would wait up to 5 s for a declared POST body; so every request that
// is not GET is answered here with the handler's 404, the connection given to
// the sender, which closes it once the reply is written.
bool requestIsGet = false;

ESP8266WebServer::ClientFuture screenMethod(const String& method, const String& url,
                                            WiFiClient* client,
                                            ESP8266WebServer::ContentTypeFunction) {
  requestIsGet = method == F("GET");
  if (requestIsGet) return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  const int query = url.indexOf('?');
  const String path = query < 0 ? url : url.substring(0, query);
  const ph::Request req{ph::Method::Other, path.c_str(), nullptr, 0};
  const ph::Context ctx{gate->sessionLive(), gate->mode(), false, nullptr, ms::kTrimMin,
                        ms::kTrimMax};
  const ph::Result r = ph::handle(req, ctx);  // 404 Not found: <path>, no action
  sendBody(*client, r);
  return ESP8266WebServer::CLIENT_IS_GIVEN;
}

void handle() {
  const int count = server.args();
  std::unique_ptr<ph::Arg[]> args(count > 0 ? new ph::Arg[count] : nullptr);
  for (int i = 0; i < count; ++i) {
    args[i] = ph::Arg{server.argName(i).c_str(), server.arg(i).c_str()};
  }
  const bool get = requestIsGet && server.method() == HTTP_GET;
  requestIsGet = false;
  const ph::Request req{get ? ph::Method::Get : ph::Method::Other,
                        server.uri().c_str(), args.get(), static_cast<size_t>(count)};
  // The context is read fresh for every request.
  const ph::Context ctx{gate->sessionLive(), gate->mode(), portal_motion::busy(), motionId,
                        ms::kTrimMin, ms::kTrimMax};
  const ph::Result r = ph::handle(req, ctx);
  if (r.page != ph::Page::None) {
    sendPage(server.client(), r.page);
    return;
  }
  Glue glue;
  sendBody(server.client(), ph::carryOut(r, glue));
}

}  // namespace

void begin(ms::OutputGate& outputGate, CalibrationStore& store) {
  gate = &outputGate;
  calibration = &store;
  static const char* const kRoutes[] = {"/",      "/controller", "/save",  "/editor",
                                        "/zero", "/setting",    "/online"};
  for (const char* route : kRoutes) server.on(route, HTTP_GET, handle);
  server.onNotFound(handle);  // other paths and methods: 404 from the handler
  server.addHook(screenMethod);
  server.begin();
}

// One transfer at a time: while one is in progress, new connections wait in
// the listening socket's queue. No step waits (web-portal design decision 10).
void poll(uint32_t /*nowMs*/) {
  if (!transfer.busy()) {
    handedOver = false;
    server.serve();
    if (handedOver) server.release();
  }
  transfer.step(millis());
}

}  // namespace portal
