// net/line_socket.cpp — see line_socket.h.
#include "net/line_socket.h"
#include <enet/enet.h>

namespace dq {

namespace {
bool g_enetUp = false;
void enetUp() { if (!g_enetUp) { g_enetUp = enet_initialize() == 0; } }
ENetSocket S(intptr_t s) { return (ENetSocket)s; }
// 1 readable, 0 nothing yet, -1 failed
int readable(ENetSocket s) {
  ENetSocketSet rs; ENET_SOCKETSET_EMPTY(rs); ENET_SOCKETSET_ADD(rs, s);
  int r = enet_socketset_select(s, &rs, nullptr, 0);
  if (r < 0) return -1;
  return r > 0 && ENET_SOCKETSET_CHECK(rs, s) ? 1 : 0;
}
// reads what's there into `in`; false if the other end went away
bool drain(ENetSocket s, std::string& in) {
  char buf[4096];
  for (int k = 0; k < 64; k++) {
    int r = readable(s);
    if (r < 0) return false;
    if (r == 0) return true;
    ENetBuffer b; b.data = buf; b.dataLength = sizeof buf;
    int n = enet_socket_receive(s, nullptr, &b, 1);
    if (n < 0) return false;
    if (n == 0) return false;   // readable but nothing: closed
    in.append(buf, (size_t)n);
    if (in.size() > (8u << 20)) return false;   // a runaway sender
  }
  return true;
}
bool sendAll(ENetSocket s, const std::string& line) {
  std::string t = line + "\n"; size_t off = 0;
  for (int k = 0; k < 2000 && off < t.size(); k++) {
    ENetBuffer b; b.data = (void*)(t.data() + off); b.dataLength = t.size() - off;
    int n = enet_socket_send(s, nullptr, &b, 1);
    if (n < 0) return false;
    off += (size_t)n;
  }
  return off == t.size();
}
std::vector<std::string> lines(std::string& in) {
  std::vector<std::string> out; size_t p;
  while ((p = in.find('\n')) != std::string::npos) { std::string l = in.substr(0, p); if (!l.empty() && l.back() == '\r') l.pop_back(); out.push_back(l); in.erase(0, p + 1); }
  return out;
}
} // namespace

LineServer::~LineServer() { close(); }
bool LineServer::listen(u16 port) {
  close(); enetUp();
  ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
  if (s == ENET_SOCKET_NULL) return false;
  enet_socket_set_option(s, ENET_SOCKOPT_REUSEADDR, 1);
  ENetAddress a; a.host = 0; a.port = port; enet_address_set_host_ip(&a, "127.0.0.1");
  if (enet_socket_bind(s, &a) < 0 || enet_socket_listen(s, 4) < 0) { enet_socket_destroy(s); return false; }
  enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
  sock = (intptr_t)s;
  return true;
}
void LineServer::close() {
  for (auto& c : conns) enet_socket_destroy(S(c.s));
  conns.clear();
  if (sock != -1) { enet_socket_destroy(S(sock)); sock = -1; }
}
void LineServer::poll(const std::function<std::string(const std::string&)>& handle) {
  if (sock == -1) return;
  while (readable(S(sock)) > 0) {
    ENetAddress from;
    ENetSocket c = enet_socket_accept(S(sock), &from);
    if (c == ENET_SOCKET_NULL) break;
    enet_socket_set_option(c, ENET_SOCKOPT_NONBLOCK, 1);
    if (conns.size() >= 4) { enet_socket_destroy(S(conns.front().s)); conns.erase(conns.begin()); }
    conns.push_back({(intptr_t)c, {}});
  }
  for (size_t i = 0; i < conns.size();) {
    Conn& c = conns[i];
    bool alive = drain(S(c.s), c.in);
    for (auto& l : lines(c.in)) { std::string r = handle(l); if (!r.empty() && !sendAll(S(c.s), r)) alive = false; }
    if (!alive) { enet_socket_destroy(S(c.s)); conns.erase(conns.begin() + i); continue; }
    i++;
  }
}

LineClient::~LineClient() { close(); }
bool LineClient::connect(const std::string& host, u16 port) {
  close(); enetUp();
  ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
  if (s == ENET_SOCKET_NULL) return false;
  ENetAddress a; a.port = port;
  if (enet_address_set_host_ip(&a, host.c_str()) < 0) { enet_socket_destroy(s); return false; }
  if (enet_socket_connect(s, &a) < 0) { enet_socket_destroy(s); return false; }   // (blocking connect: local, at once)
  enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
  sock = (intptr_t)s; up = true;
  return true;
}
bool LineClient::connected() { return sock != -1 && up; }
bool LineClient::send(const std::string& line) {
  if (sock == -1) return false;
  if (!sendAll(S(sock), line)) { close(); return false; }
  return true;
}
std::vector<std::string> LineClient::poll() {
  if (sock == -1) return {};
  if (!drain(S(sock), in)) { auto l = lines(in); close(); return l; }
  return lines(in);
}
void LineClient::close() { if (sock != -1) { enet_socket_destroy(S(sock)); sock = -1; } up = false; in.clear(); }

} // namespace dq
