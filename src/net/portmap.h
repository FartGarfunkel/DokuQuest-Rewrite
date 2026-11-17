// net/portmap.h — asking the home router to let friends in, so nobody has to set up port forwarding: UPnP (most home
// routers; often on by default). For the computer that's actually hosting: TCP and UDP on the game's port forwarded to
// it, and (routers with IPv6 firewall control) the IPv6 firewall opened for it. Leases are renewed while it's open and
// taken back when it closes. Everything happens on a worker; what the router said comes back as lines for the log.
#pragma once
#include "common/common.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace dq::net {

class PortMapper {
public:
  ~PortMapper() { stop(); }
  void start(u16 port);              // asks the router (on a worker): returns at once
  void stop();                       // takes the forwards back (waits up to ~2 s for the router)
  std::vector<std::string> takeLog();
  std::string publicAddress();       // the router's internet address, once it said ("" otherwise)
private:
  void run(u16 port);
  std::thread th; std::atomic<bool> running{false};
  std::mutex mx; std::vector<std::string> log; std::string pub;
};

} // namespace dq::net
