// net/tcp_relay.cpp — see tcp_relay.h. The loopback UDP side uses ENet's socket calls; the TCP side the system's own, which
// speak IPv6 as well as IPv4 (ENet's addresses are IPv4 only): the listener is one IPv6 socket that takes IPv4 too
// (IPV6_V6ONLY off, as a dual-stack server does), and a join looks the name up for any family and tries each address.
#ifdef _WIN32
#define FD_SETSIZE 256   // (Windows' default 64 would cap the server at ~31 TCP players; must come before the socket headers)
#endif
#include "net/tcp_relay.h"
#include <enet/enet.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
#include <chrono>
#include <algorithm>
#include <cstring>

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
// ---- the system's TCP calls (IPv4 and IPv6) ----
#ifdef _WIN32
using NSock = SOCKET;
void nClose(NSock s) { closesocket(s); }
void nNonblock(NSock s) { u_long one = 1; ioctlsocket(s, FIONBIO, &one); }
bool nWouldBlock() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
constexpr int SEND_FLAGS = 0;
#else
using NSock = int;
void nClose(NSock s) { ::close(s); }
void nNonblock(NSock s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
bool nWouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
constexpr int SEND_FLAGS = MSG_NOSIGNAL;   // (a closed connection must not kill the program)
#endif
constexpr NSock NNONE = (NSock)ENET_SOCKET_NULL;
void nNoDelay(NSock s) { int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one); }
// "1.2.3.4" or "2600::1" (an IPv4 player reaching the dual-stack listener shows as itself, not ::ffff:1.2.3.4)
std::string addrText(const sockaddr_storage& a, u16* port) {
  char b[INET6_ADDRSTRLEN] = "?";
  if (a.ss_family == AF_INET) { auto* v = (const sockaddr_in*)&a; inet_ntop(AF_INET, (void*)&v->sin_addr, b, sizeof b); if (port) *port = ntohs(v->sin_port); }
  else if (a.ss_family == AF_INET6) { auto* v = (const sockaddr_in6*)&a; inet_ntop(AF_INET6, (void*)&v->sin6_addr, b, sizeof b); if (port) *port = ntohs(v->sin6_port); }
  std::string s = b;
  if (s.rfind("::ffff:", 0) == 0 && s.find('.') != std::string::npos) s = s.substr(7);
  return s;
}
std::string withPort(const std::string& ip, u16 port) { return (ip.find(':') != std::string::npos ? "[" + ip + "]" : ip) + ":" + std::to_string(port); }
bool flushN(NSock s, std::string& out) {
  while (!out.empty()) {
    const int n = (int)::send(s, out.data(), (int)out.size(), SEND_FLAGS);
    if (n < 0) return nWouldBlock();   // (full for now: the rest goes later)
    if (n == 0) return true;
    out.erase(0, (size_t)n);
  }
  return true;
}
bool readN(NSock s, std::string& in) {
  char buf[16384];
  const int n = (int)::recv(s, buf, sizeof buf, 0);
  if (n == 0) return false;            // (closed)
  if (n < 0) return nWouldBlock();     // (nothing after all, or reset)
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
  // one IPv6 socket that takes IPv4 too; a system with no IPv6 gets a plain IPv4 one
  NSock s = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  bool six = s != NNONE;
  if (six) {
    int off = 0; setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof off);   // (Windows defaults to IPv6 only)
#ifndef _WIN32
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);   // (on Windows it would let us share a port another program holds)
#endif
    sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_addr = in6addr_any; a.sin6_port = htons(tcpPort);
    if (::bind(s, (sockaddr*)&a, sizeof a) != 0 || ::listen(s, 16) != 0) { nClose(s); s = NNONE; six = false; }
  }
  if (s == NNONE) {
    s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == NNONE) return false;
#ifndef _WIN32
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
#endif
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(tcpPort);
    if (::bind(s, (sockaddr*)&a, sizeof a) != 0 || ::listen(s, 16) != 0) { nClose(s); return false; }
  }
  nNonblock(s);
  dual = six;
  listener = (intptr_t)s; game = gamePort; running = true;
  th = std::thread([this] { run(); });
  return true;
}
void TcpRelayServer::stop() {
  running = false;
  if (th.joinable()) th.join();
  if (listener != -1) { nClose((NSock)listener); listener = -1; }
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
    ENetSocket top = S(listener); ENET_SOCKETSET_ADD(rs, S(listener));   // (the system's sockets and ENet's are the same kind of handle)
    for (auto& c : conns) { ENET_SOCKETSET_ADD(rs, c.tcp); ENET_SOCKETSET_ADD(rs, c.udp); top = std::max(top, std::max(c.tcp, c.udp)); }
    if (enet_socketset_select(top, &rs, nullptr, 50) < 0) continue;
    if (ENET_SOCKETSET_CHECK(rs, S(listener))) {
      sockaddr_storage from{}; socklen_t fl = sizeof from;
      NSock t = ::accept((NSock)listener, (sockaddr*)&from, &fl);
      if (t != NNONE) {
        nNonblock(t); nNoDelay(t);
        u16 loop = 0; ENetSocket u = loopUdp(&loop);
        u16 fport = 0; const std::string ip = addrText(from, &fport);
        if (u == ENET_SOCKET_NULL || conns.size() >= 64) { nClose(t); if (u != ENET_SOCKET_NULL) enet_socket_destroy(u); }
        else {
          conns.push_back({(ENetSocket)t, u, {}, {}, withPort(ip, fport), loop, now()});
          { std::lock_guard<std::mutex> lk(mx); real[loop] = ip; }
          say("Heard from " + conns.back().who + " over TCP (someone trying to join)");
        }
      }
    }
    for (size_t i = 0; i < conns.size();) {
      Conn& c = conns[i];
      bool alive = true;
      if (ENET_SOCKETSET_CHECK(rs, c.tcp)) {
        alive = readN((NSock)c.tcp, c.in); c.heard = now();
        frames(c.in, [&](const char* d, size_t n) { ENetBuffer b; b.data = (void*)d; b.dataLength = n; enet_socket_send(c.udp, &gameAt, &b, 1); });
      }
      if (alive && ENET_SOCKETSET_CHECK(rs, c.udp))
        for (int k = 0; k < 256; k++) {
          char buf[4096]; ENetBuffer b; b.data = buf; b.dataLength = sizeof buf; ENetAddress from;
          const int n = enet_socket_receive(c.udp, &from, &b, 1);
          if (n <= 0) break;
         
          frameTo(c.out, buf, (size_t)n);
        }
      if (alive) alive = flushN((NSock)c.tcp, c.out) && c.out.size() < (8u << 20);
      // a game in touch sends twice a second; a minute of nothing: it's gone without closing (a phone that lost its signal,
      // a half-open probe), so its slot is freed
      const bool idle = now() - c.heard > 60;
      if (!alive || idle) {
        say("The TCP connection from " + c.who + (idle ? " went quiet: closed" : " closed"));
        nClose((NSock)c.tcp); enet_socket_destroy(c.udp);
        { std::lock_guard<std::mutex> lk(mx); real.erase(c.loop); }
        conns.erase(conns.begin() + i); continue;
      }
      i++;
    }
  }
  for (auto& c : conns) { nClose((NSock)c.tcp); enet_socket_destroy(c.udp); }
}

// ------------------------------------------------------------------ the joining game's side
bool TcpRelayClient::start(const std::string& host, u16 port) {
  stop();
  // looked up here, for any family (IPv6 and IPv4: a phone on mobile data can be reached over IPv6 when its IPv4 can't),
  // so a slow lookup never holds up leaving the join
  targets.clear();
  addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
  addrinfo* res = nullptr;
  const std::string ps = std::to_string(port);
  if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0 || !res) return false;
  for (addrinfo* r = res; r && targets.size() < 6; r = r->ai_next) {
    if (r->ai_family != AF_INET && r->ai_family != AF_INET6) continue;
    sockaddr_storage ss{}; std::memcpy(&ss, r->ai_addr, std::min((size_t)r->ai_addrlen, sizeof ss));
    std::string blob((const char*)&ss, sizeof ss); targets.push_back(blob);
  }
  freeaddrinfo(res);
  if (targets.empty()) return false;
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
  // each address in turn (IPv6 first when the system lists it first), without blocking, so leaving never waits on it;
  // a few seconds each, about 10 in all
  NSock t = NNONE; std::string via;
  const int each = std::max(40, 200 / (int)targets.size());   // (50 ms steps)
  for (size_t ti = 0; ti < targets.size() && t == NNONE && running; ti++) {
    sockaddr_storage ss{}; std::memcpy(&ss, targets[ti].data(), sizeof ss);
    NSock c = ::socket(ss.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (c == NNONE) continue;
    nNonblock(c);
    const socklen_t sl = ss.ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    const int r0 = ::connect(c, (sockaddr*)&ss, sl);
    bool up = r0 == 0;
    if (!up && nWouldBlock())
      for (int k = 0; k < each && running; k++) {
        fd_set ws, es; FD_ZERO(&ws); FD_ZERO(&es); FD_SET(c, &ws); FD_SET(c, &es);
        timeval tv{0, 50000};
        const int r = ::select((int)c + 1, nullptr, &ws, &es, &tv);
        if (r < 0) break;
        if (r > 0) {
          int e = 0; socklen_t el = sizeof e; getsockopt(c, SOL_SOCKET, SO_ERROR, (char*)&e, &el);
          up = e == 0 && FD_ISSET(c, &ws); break;
        }
      }
    if (up) { t = c; via = addrText(ss, nullptr); } else nClose(c);
  }
  if (t == NNONE) { dead = true; if (running) DQ_WARN("net: TCP to %s:%u failed too", host.c_str(), (unsigned)port); return; }
  nNoDelay(t);
  DQ_LOG("net: connected to %s:%u over TCP (%s)", host.c_str(), (unsigned)port, via.c_str());
  std::string in, out; ENetAddress game{}; bool haveGame = false;
  const ENetSocket u = S(udp);
  while (running) {
    ENetSocketSet rs; ENET_SOCKETSET_EMPTY(rs); ENET_SOCKETSET_ADD(rs, (ENetSocket)t); ENET_SOCKETSET_ADD(rs, u);
    if (enet_socketset_select(std::max((ENetSocket)t, u), &rs, nullptr, 50) < 0) continue;
    if (ENET_SOCKETSET_CHECK(rs, u))
      for (int k = 0; k < 256; k++) {
        char buf[4096]; ENetBuffer b; b.data = buf; b.dataLength = sizeof buf; ENetAddress from;
        const int n = enet_socket_receive(u, &from, &b, 1);
        if (n <= 0) break;
        game = from; haveGame = true;
        frameTo(out, buf, (size_t)n);
      }
    bool alive = true;
    if (ENET_SOCKETSET_CHECK(rs, (ENetSocket)t)) {
      alive = readN(t, in);
      frames(in, [&](const char* d, size_t n) { if (haveGame) { ENetBuffer b; b.data = (void*)d; b.dataLength = n; enet_socket_send(u, &game, &b, 1); } });
    }
    if (alive) alive = flushN(t, out) && out.size() < (8u << 20);
    if (!alive) { DQ_LOG("net: the TCP connection closed"); dead = true; break; }
  }
  nClose(t);
}

} // namespace dq::net
