// net/lan.cpp — LAN announcements and the scanner (see net/lan.h). Uses ENet's portable socket calls.
#include "net/lan.h"
#include "net/protocol.h"
#include <enet/enet.h>

namespace dq::net {

static ENetSocket S(intptr_t s) { return (ENetSocket)s; }

bool LanAnnouncer::start() {
  if (sock != -1) return true;
  if (!enetInit()) return false;
  ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (s == ENET_SOCKET_NULL) { enetDone(); return false; }
  enet_socket_set_option(s, ENET_SOCKOPT_BROADCAST, 1);
  enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
  sock = (intptr_t)s; timer = 1e9;
  return true;
}
void LanAnnouncer::stop() {
  if (sock == -1) return;
  enet_socket_destroy(S(sock)); sock = -1;
  enetDone();
}
void LanAnnouncer::update(double dt, const json& info) {
  if (sock == -1) return;
  timer += dt;
  if (timer < 1.5) return;
  timer = 0;
  json j = info; j["dokuquest"] = 1; j["version"] = DQ_VERSION; j["proto"] = PROTO;
  std::string msg = j.dump();
  ENetBuffer b; b.data = msg.data(); b.dataLength = msg.size();
  for (const char* target : {"255.255.255.255", "127.0.0.1"}) {
    ENetAddress a; enet_address_set_host_ip(&a, target); a.port = DISCOVERY_PORT;
    enet_socket_send(S(sock), &a, &b, 1);
  }
}

bool LanScanner::start() {
  if (sock != -1) return true;
  if (!enetInit()) return false;
  ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (s == ENET_SOCKET_NULL) { enetDone(); return false; }
  enet_socket_set_option(s, ENET_SOCKOPT_REUSEADDR, 1);
  enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
  ENetAddress a; a.host = ENET_HOST_ANY; a.port = DISCOVERY_PORT;
  if (enet_socket_bind(s, &a) != 0) { enet_socket_destroy(s); enetDone(); DQ_WARN("lan: port %d is taken; type an address instead", DISCOVERY_PORT); return false; }
  sock = (intptr_t)s;
  return true;
}
void LanScanner::stop() {
  if (sock == -1) return;
  enet_socket_destroy(S(sock)); sock = -1; found.clear();
  enetDone();
}
void LanScanner::poll() {
  if (sock == -1) return;
  char buf[2048];
  for (int n = 0; n < 64; n++) {
    ENetAddress from; ENetBuffer b; b.data = buf; b.dataLength = sizeof buf - 1;
    int got = enet_socket_receive(S(sock), &from, &b, 1);
    if (got <= 0) break;
    json o = json::parse(std::string(buf, got), nullptr, false);
    if (!o.is_object() || !o.value("dokuquest", 0) || !o.value("port", 0)) continue;
    char ip[64]; enet_address_get_host_ip(&from, ip, sizeof ip);
    LanGame g; g.address = ip; g.port = o.value("port", 0);
    g.world = o.value("world", "A world"); g.host = o.value("host", "Someone"); g.version = o.value("version", "");
    g.motd = o.value("motd", ""); g.players = o.value("players", 0); g.maxPlayers = o.value("max", 0); g.proto = o.value("proto", 0);
    g.password = o.value("password", false); g.self = g.address == "127.0.0.1"; g.seenAt = nowSeconds();
    bool merged = false;
    for (auto& f : found) if (f.port == g.port && (f.address == g.address || (f.world == g.world && f.host == g.host))) {
      bool keepRemote = f.address != "127.0.0.1" && g.self;   // the same server heard twice: keep its network address
      std::string addr = keepRemote ? f.address : g.address;
      f = g; f.address = addr; f.self = f.self || g.self; merged = true; break;
    }
    if (!merged) found.push_back(g);
  }
}
std::vector<LanGame> LanScanner::list() {
  double now = nowSeconds();
  found.erase(std::remove_if(found.begin(), found.end(), [&](const LanGame& g) { return now - g.seenAt > 5; }), found.end());
  return found;
}

std::string localAddress() {
  if (!enetInit()) return "";
  std::string out;
  ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (s != ENET_SOCKET_NULL) {
    ENetAddress a; enet_address_set_host_ip(&a, "8.8.8.8"); a.port = 53;   // a UDP "connect" sends nothing: it only picks the route
    ENetAddress me;
    if (enet_socket_connect(s, &a) == 0 && enet_socket_get_address(s, &me) == 0) { char ip[64]; enet_address_get_host_ip(&me, ip, sizeof ip); out = ip; }
    enet_socket_destroy(s);
  }
  enetDone();
  return out == "0.0.0.0" ? "" : out;
}

} // namespace dq::net
