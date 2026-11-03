// net/lan.h — finding worlds on the home network (launcher/desktop/lan.js): a host says "a world is open here"
// every 1.5 s by UDP broadcast to port 25581 ({dokuquest:1, world, host, version, players, port, ...} as JSON);
// a scanner listens there and lists what it heard in the last 5 seconds.
#pragma once
#include "common/fs.h"
#include <mutex>

namespace dq::net {

struct LanGame {
  std::string address;      // where the announcement came from
  int port = 0;
  std::string world, host, version, motd;
  int players = 0, maxPlayers = 0, proto = 0;
  bool password = false, self = false;
  double seenAt = 0;
};

class LanAnnouncer {
public:
  ~LanAnnouncer() { stop(); }
  bool start();
  void stop();
  void update(double dt, const json& info);   // info: world, host, players, port... (dokuquest/version added)
private:
  intptr_t sock = -1;
  double timer = 0;
};

class LanScanner {
public:
  ~LanScanner() { stop(); }
  bool start();               // binds UDP 25581 (false if it is taken: typing an address still works)
  void stop();
  void poll();                // read what arrived (call every frame while the list is shown)
  std::vector<LanGame> list();
  bool running() const { return sock != -1; }
private:
  intptr_t sock = -1;
  std::vector<LanGame> found;
};

std::string internetAddress();   // this computer's address as the internet sees it (asks api.ipify.org through curl; blocks
                                 // up to ~6 s, so call it off the main thread); "" when it can't be found out
bool firewallAllowSelf(std::string* err = nullptr);   // Windows: let this program through Windows Firewall (asks for
                                                       // an administrator's Yes; blocks up to ~20 s, so off the main thread)
std::string vpnOn();          // the name of a VPN that's on ("NordVPN", "Proton VPN", or the adapter's name), "" if none
std::string vpnAdvice(int port);   // what to do about it for friends joining over the internet ("" when no VPN is on)
std::string localAddress();   // this computer's address on the home network ("" if it has none), for "open on <address>:<port>"

} // namespace dq::net
