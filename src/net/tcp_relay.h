// net/tcp_relay.h — the game's connection carried over TCP, for networks that let TCP in but drop UDP coming in from
// outside (home connections where other games' servers, which use TCP, work but ours, on UDP, was never heard).
// The server listens for TCP on its own port number; each TCP connection gets a little UDP socket on 127.0.0.1 that
// passes the game's packets to the real server and back, so the server sees an ordinary player. A joining game that
// hears nothing over UDP for a few seconds starts a TcpRelayClient: a UDP socket on 127.0.0.1 its connection talks to,
// whose packets go to the server over one TCP connection. Packets travel as [2-byte length][the packet].
#pragma once
#include "common/common.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace dq::net {

class TcpRelayServer {
public:
  ~TcpRelayServer() { stop(); }
  bool start(u16 tcpPort, u16 gamePort);   // false: the TCP port is taken (UDP joins still work)
  void stop();
  std::vector<std::string> takeLog();      // lines for the server's console (read on the server's own thread)
  // the real address ("ip") of the player whose packets come from 127.0.0.1:loopPort (their relay socket), or ""
  std::string realAddress(u16 loopPort);
private:
  void run();
  std::thread th; std::atomic<bool> running{false};
  intptr_t listener = -1; u16 game = 0;
public:
  bool dual = false;   // the listener takes IPv6 as well as IPv4
private:
  std::mutex mx; std::vector<std::string> log; std::unordered_map<u16, std::string> real;
};

class TcpRelayClient {
public:
  ~TcpRelayClient() { stop(); }
  bool start(const std::string& host, u16 port);   // the local UDP port to send to: localPort()
  u16 localPort() const { return local; }
  void stop();
  bool failed() const { return dead; }
private:
  void run(std::string host, u16 port);
  std::thread th; std::atomic<bool> running{false}, dead{false};
  intptr_t udp = -1; u16 local = 0;
  std::vector<std::string> targets;   // (where to: the addresses start() looked up, each a sockaddr_storage's bytes)
};

} // namespace dq::net
