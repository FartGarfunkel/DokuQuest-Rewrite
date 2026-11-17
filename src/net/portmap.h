// net/portmap.h — asking the home router to let friends in, so nobody has to set up port forwarding: UPnP (most home
// routers; often on by default). For the computer that's actually hosting: TCP and UDP on the game's port forwarded to
// it, and (routers with IPv6 firewall control) the IPv6 firewall opened for it. Leases are renewed while it's open and
// taken back when it closes. Everything happens on a worker of its own (stopping never waits on the router); what the
// router said comes back as lines for the log.
#pragma once
#include "common/common.h"
#include <memory>

namespace dq::net {

class PortMapper {
public:
  ~PortMapper() { stop(); }
  void start(u16 port);              // asks the router (on a worker): returns at once
  void stop();                       // the worker takes the forwards back by itself and ends: returns at once
  std::vector<std::string> takeLog();
  struct State;
private:
  std::shared_ptr<State> st;
};

} // namespace dq::net
