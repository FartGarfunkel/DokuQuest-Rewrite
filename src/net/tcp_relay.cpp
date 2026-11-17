// net/tcp_relay.cpp — see tcp_relay.h. ENet's socket calls, so it works the same on Linux, Windows and Android.
#ifdef _WIN32
#define FD_SETSIZE 256   // (Windows' default 64 would cap the server at ~31 TCP players; must come before the socket headers)
#endif
#include "net/tcp_relay.h"
#include <enet/enet.h>
#include <chrono>
#include <algorithm>

namespace dq::net {

namespace {
ENetSocket S(intptr_t s) { return (ENetSocket)s; }
// whole packets out of a TCP stream's bytes
template <class F> void frames(std::string& in, F&& f) {
  while (in.size() >= 2) {
    const size_t n = ((u8)in[0] << 8) | (u8)in[1];
    if (in.size() < 2 + n) break;
    f(in.data() + 2, n);
    in.erase(0, 2 + n);
  }
}
void frameTo(std::string& out, const char* d, size_t n) { out.push_back((char)(n >> 8)); out.push_back((char)(n & 255)); out.append(d, n); }
// as much of `out` as the TCP connection takes now; false: it's gone
bool flush(ENetSocket s, std::string& out) {
  while (!out.empty()) {
    ENetBuffer b; b.data = out.data(); b.dataLength = out.size();
    const int n = enet_socket_send(s, nullptr, &b, 1);
    if (n < 0) return false;
    if (n == 0) return true;   // (full for now)
    out.erase(0, (size_t)n);
  }
  return true;
}
// what a TCP connection has for us: false when it closed
bool readTcp(ENetSocket s, std::string& in) {
  char buf[16384]; ENetBuffer b; b.data = buf; b.dataLength = sizeof buf;
  const int n = enet_socket_receive(s, nullptr, &b, 1);
  if (n <= 0) return false;   // (it said it was readable: nothing means it closed)
  in.append(buf, (size_t)n);
  return in.size() < (8u << 20);
}
ENetSocket loopUdp(u16* port) {
  ENetSocket u = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (u == ENET_SOCKET_NULL) return u;
  ENetAddress a; a.port = 0; enet_address_set_host_ip(&a, "127.0.0.1");
  if (enet_socket_bind(u, &a) < 0) { enet_socket_destroy(u); return ENET_SOCKET_NULL; }
  enet_socket_set_option(u, ENET_SOCKOPT_NONBLOCK, 1);
  ENetAddress got; if (port && enet_socket_get_address(u, &got) == 0) *port = got.port;
  return u;
}
}

// ------------------------------------------------------------------ the server's side
bool TcpRelayServer::start(u16 tcpPort, u16 gamePort) {
  stop();
  ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
  if (s == ENET_SOCKET_NULL) return false;
#ifndef _WIN32
  enet_socket_set_option(s, ENET_SOCKOPT_REUSEADDR, 1);   // (on Windows it would let us share a port another program holds)
#endif
  ENetAddress a; a.host = ENET_HOST_ANY; a.port = tcpPort;
  if (enet_socket_bind(s, &a) < 0 || enet_socket_listen(s, 16) < 0) { enet_socket_destroy(s); return false; }
  enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
  listener = (intptr_t)s; game = gamePort; running = true;
  th = std::thread([this] { run(); });
  return true;
}
void TcpRelayServer::stop() {
  running = false;
  if (th.joinable()) th.join();
  if (listener != -1) { enet_socket_destroy(S(listener)); listener = -1; }
}
std::string TcpRelayServer::realAddress(u16 loopPort) { std::lock_guard<std::mutex> lk(mx); auto it = real.find(loopPort); return it == real.end() ? std::string() : it->second; }
std::vector<std::string> TcpRelayServer::takeLog() { std::lock_guard<std::mutex> lk(mx); auto v = std::move(log); log.clear(); return v; }

void TcpRelayServer::run() {
  struct Conn { ENetSocket tcp, udp; std::string in, out, who; u16 loop = 0; double heard = 0; };
  auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
  std::vector<Conn> conns;
  ENetAddress gameAt; gameAt.port = game; enet_address_set_host_ip(&gameAt, "127.0.0.1");
  auto say = [&](const std::string& l) { std::lock_guard<std::mutex> lk(mx); log.push_back(l); };
  while (running) {
    ENetSocketSet rs; ENET_SOCKETSET_EMPTY(rs);
    ENetSocket top = S(listener); ENET_SOCKETSET_ADD(rs, S(listener));
    for (auto& c : conns) { ENET_SOCKETSET_ADD(rs, c.tcp); ENET_SOCKETSET_ADD(rs, c.udp); top = std::max(top, std::max(c.tcp, c.udp)); }
    if (enet_socketset_select(top, &rs, nullptr, 50) < 0) continue;
    if (ENET_SOCKETSET_CHECK(rs, S(listener))) {
      ENetAddress from; ENetSocket t = enet_socket_accept(S(listener), &from);
      if (t != ENET_SOCKET_NULL) {
        enet_socket_set_option(t, ENET_SOCKOPT_NONBLOCK, 1);
        enet_socket_set_option(t, ENET_SOCKOPT_NODELAY, 1);
        u16 loop = 0; ENetSocket u = loopUdp(&loop);
        char ip[64] = "?"; enet_address_get_host_ip(&from, ip, sizeof ip);
        if (u == ENET_SOCKET_NULL || conns.size() >= 64) { enet_socket_destroy(t); if (u != ENET_SOCKET_NULL) enet_socket_destroy(u); }
        else {
          conns.push_back({t, u, {}, {}, std::string(ip) + ":" + std::to_string(from.port), loop, now()});
          { std::lock_guard<std::mutex> lk(mx); real[loop] = ip; }
          say("Heard from " + conns.back().who + " over TCP (someone trying to join)");
        }
      }
    }
    for (size_t i = 0; i < conns.size();) {
      Conn& c = conns[i];
      bool alive = true;
      if (ENET_SOCKETSET_CHECK(rs, c.tcp)) {
        alive = readTcp(c.tcp, c.in); c.heard = now();
        frames(c.in, [&](const char* d, size_t n) { ENetBuffer b; b.data = (void*)d; b.dataLength = n; enet_socket_send(c.udp, &gameAt, &b, 1); });
      }
      if (alive && ENET_SOCKETSET_CHECK(rs, c.udp))
        for (int k = 0; k < 256; k++) {
          char buf[4096]; ENetBuffer b; b.data = buf; b.dataLength = sizeof buf; ENetAddress from;
          const int n = enet_socket_receive(c.udp, &from, &b, 1);
          if (n <= 0) break;
         
          frameTo(c.out, buf, (size_t)n);
        }
      if (alive) alive = flush(c.tcp, c.out) && c.out.size() < (8u << 20);
      // a game in touch sends twice a second; a minute of nothing: it's gone without closing (a phone that lost its signal,
      // a half-open probe), so its slot is freed
      const bool idle = now() - c.heard > 60;
      if (!alive || idle) {
        say("The TCP connection from " + c.who + (idle ? " went quiet: closed" : " closed"));
        enet_socket_destroy(c.tcp); enet_socket_destroy(c.udp);
        { std::lock_guard<std::mutex> lk(mx); real.erase(c.loop); }
        conns.erase(conns.begin() + i); continue;
      }
      i++;
    }
  }
  for (auto& c : conns) { enet_socket_destroy(c.tcp); enet_socket_destroy(c.udp); }
}

// ------------------------------------------------------------------ the joining game's side
bool TcpRelayClient::start(const std::string& host, u16 port) {
  stop();
  ENetAddress t; if (enet_address_set_host(&t, host.c_str()) != 0) return false;   // (looked up here: a slow lookup in the thread would hold up leaving)
  targetHost = t.host; targetPort = port;
  ENetSocket u = loopUdp(&local);
  if (u == ENET_SOCKET_NULL) return false;
  udp = (intptr_t)u; running = true; dead = false;
  th = std::thread([this, host, port] { run(host, port); });
  return true;
}
void TcpRelayClient::stop() {
  running = false;
  if (th.joinable()) th.join();
  if (udp != -1) { enet_socket_destroy(S(udp)); udp = -1; }
}
void TcpRelayClient::run(std::string host, u16 port) {
  ENetAddress at; at.host = targetHost; at.port = targetPort;
  ENetSocket t = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
  if (t == ENET_SOCKET_NULL) { dead = true; return; }
  // connect without blocking, a little at a time, so leaving the join never waits on it (a dropped handshake would
  // otherwise hold for two minutes); 8 seconds in all
  enet_socket_set_option(t, ENET_SOCKOPT_NONBLOCK, 1);
  enet_socket_connect(t, &at);   // (in progress)
  bool up = false;
  for (int k = 0; k < 160 && running; k++) {
    ENetSocketSet ws; ENET_SOCKETSET_EMPTY(ws); ENET_SOCKETSET_ADD(ws, t);
    const int r = enet_socketset_select(t, nullptr, &ws, 50);
    if (r < 0) break;
    if (r > 0 && ENET_SOCKETSET_CHECK(ws, t)) {
      int e = 0; enet_socket_get_option(t, ENET_SOCKOPT_ERROR, &e);
      up = e == 0; break;
    }
  }
  if (!up) { enet_socket_destroy(t); dead = true; if (running) DQ_WARN("net: TCP to %s:%u failed too", host.c_str(), (unsigned)port); return; }
  enet_socket_set_option(t, ENET_SOCKOPT_NODELAY, 1);
  DQ_LOG("net: connected to %s:%u over TCP", host.c_str(), (unsigned)port);
  std::string in, out; ENetAddress game{}; bool haveGame = false;
  const ENetSocket u = S(udp);
  while (running) {
    ENetSocketSet rs; ENET_SOCKETSET_EMPTY(rs); ENET_SOCKETSET_ADD(rs, t); ENET_SOCKETSET_ADD(rs, u);
    if (enet_socketset_select(std::max(t, u), &rs, nullptr, 50) < 0) continue;
    if (ENET_SOCKETSET_CHECK(rs, u))
      for (int k = 0; k < 256; k++) {
        char buf[4096]; ENetBuffer b; b.data = buf; b.dataLength = sizeof buf; ENetAddress from;
        const int n = enet_socket_receive(u, &from, &b, 1);
        if (n <= 0) break;
        game = from; haveGame = true;
        frameTo(out, buf, (size_t)n);
      }
    bool alive = true;
    if (ENET_SOCKETSET_CHECK(rs, t)) {
      alive = readTcp(t, in);
      frames(in, [&](const char* d, size_t n) { if (haveGame) { ENetBuffer b; b.data = (void*)d; b.dataLength = n; enet_socket_send(u, &game, &b, 1); } });
    }
    if (alive) alive = flush(t, out) && out.size() < (8u << 20);
    if (!alive) { DQ_LOG("net: the TCP connection closed"); dead = true; break; }
  }
  enet_socket_destroy(t);
}

} // namespace dq::net
