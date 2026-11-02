// net/line_socket.h — lines of text over a local TCP connection (ENet's socket calls, so it works the same on Linux and
// Windows): the engine bridge between a running game and the launcher's Engine page or an outside tool such as the
// user's Strata coding AI (client/engine_bridge.cpp; docs/engine-bridge.md). Listens on 127.0.0.1 only. Never blocks.
#pragma once
#include "common/common.h"
#include <functional>

namespace dq {

class LineServer {
public:
  ~LineServer();
  bool listen(u16 port);   // 127.0.0.1:port
  // every whole line from every client goes to handle(); what it returns (if not empty) is sent back as one line
  void poll(const std::function<std::string(const std::string&)>& handle);
  void close();
  bool open() const { return sock != -1; }
  int clients() const { return (int)conns.size(); }
private:
  struct Conn { intptr_t s; std::string in; };
  intptr_t sock = -1;
  std::vector<Conn> conns;
};

class LineClient {
public:
  ~LineClient();
  bool connect(const std::string& host, u16 port);   // starts connecting; connected() says when it's up
  bool connected();
  bool send(const std::string& line);                 // adds the newline
  std::vector<std::string> poll();                    // whole lines received since the last call
  void close();
  bool open() const { return sock != -1; }
private:
  intptr_t sock = -1; std::string in; bool up = false;
};

} // namespace dq
