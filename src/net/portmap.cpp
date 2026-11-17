// net/portmap.cpp — see portmap.h. miniupnpc (third_party/miniupnpc, BSD-3).
#include "net/portmap.h"
#include "net/lan.h"
#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#include <chrono>
#include <cstring>

namespace dq::net {

void PortMapper::start(u16 port) {
  stop();
  running = true;
  th = std::thread([this, port] { run(port); });
}
void PortMapper::stop() {
  running = false;
  if (th.joinable()) th.join();
}
std::vector<std::string> PortMapper::takeLog() { std::lock_guard<std::mutex> lk(mx); auto v = std::move(log); log.clear(); return v; }
std::string PortMapper::publicAddress() { std::lock_guard<std::mutex> lk(mx); return pub; }

void PortMapper::run(u16 port) {
  auto say = [&](const std::string& l) { std::lock_guard<std::mutex> lk(mx); log.push_back(l); };
  int err = 0;
  UPNPDev* devs = upnpDiscover(2000, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, 2, &err);
  if (!devs) { say("The router didn't answer an automatic port request (UPnP is off or not there): friends from outside need the port forwarded by hand, or the IPv6 address"); return; }
  UPNPUrls urls{}; IGDdatas data{};
  char lan[64] = "", wan[64] = "";
  const int igd = UPNP_GetValidIGD(devs, &urls, &data, lan, sizeof lan, wan, sizeof wan);
  freeUPNPDevlist(devs);
  if (igd == UPNP_NO_IGD || igd == UPNP_UNKNOWN_DEVICE) { if (igd) FreeUPNPUrls(&urls); say("No router that takes automatic port requests was found: forward the port by hand"); return; }
  if (igd == UPNP_PRIVATEIP_IGD)
    say(std::string("The router's own internet address is ") + wan + ", a shared one: your provider puts many homes behind one address (CGNAT), so nobody can reach this computer over IPv4. Use the IPv6 address, or ask your provider for a public IPv4 address");
  { std::lock_guard<std::mutex> lk(mx); pub = wan; }
  const std::string ps = std::to_string(port);
  bool mapped[2] = {false, false};
  const char* proto[2] = {"TCP", "UDP"};
  auto addAll = [&](bool quiet) {
    for (int k = 0; k < 2; k++) {
      int r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, ps.c_str(), ps.c_str(), lan, "DokuQuest", proto[k], nullptr, "3600");
      if (r == 725) r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, ps.c_str(), ps.c_str(), lan, "DokuQuest", proto[k], nullptr, "0");   // (725: only permanent ones allowed)
      mapped[k] = r == UPNPCOMMAND_SUCCESS;
      if (!quiet) {
        if (mapped[k]) say(std::string("The router forwarded ") + proto[k] + " port " + ps + " to this computer (" + lan + ") by itself (UPnP)");
        else say(std::string("The router refused to forward ") + proto[k] + " port " + ps + " to this computer (" + lan + "): " + strupnperror(r) + " (another computer may already have it: forward it here by hand)");
      }
    }
  };
  addAll(false);
  // IPv6: routers with IPv6 firewall control let this computer's own IPv6 address be reached on the port
  char pin[2][16] = {"", ""};
  const std::string six = internetAddress6();
  if (!six.empty() && data.IPv6FC.controlurl[0]) {
    for (int k = 0; k < 2; k++) {
      const int r = UPNP_AddPinhole(urls.controlURL_6FC, data.IPv6FC.servicetype, "", "0", six.c_str(), ps.c_str(), k ? "17" : "6", "3600", pin[k]);
      say(std::string("The router's IPv6 firewall ") + (r == UPNPCOMMAND_SUCCESS ? "opened " : "wouldn't open ") + proto[k] + " port " + ps + " for [" + six + "]" + (r == UPNPCOMMAND_SUCCESS ? "" : std::string(": ") + strupnperror(r)));
      if (r != UPNPCOMMAND_SUCCESS) pin[k][0] = 0;
    }
  }
  // keep it: renewed every half hour while open
  auto t0 = std::chrono::steady_clock::now();
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (std::chrono::steady_clock::now() - t0 > std::chrono::minutes(30)) {
      t0 = std::chrono::steady_clock::now();
      addAll(true);
      for (int k = 0; k < 2; k++) if (pin[k][0]) UPNP_UpdatePinhole(urls.controlURL_6FC, data.IPv6FC.servicetype, pin[k], "3600");
    }
  }
  for (int k = 0; k < 2; k++) if (mapped[k]) UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, ps.c_str(), proto[k], nullptr);
  for (int k = 0; k < 2; k++) if (pin[k][0]) UPNP_DeletePinhole(urls.controlURL_6FC, data.IPv6FC.servicetype, pin[k]);
  FreeUPNPUrls(&urls);
}

} // namespace dq::net
