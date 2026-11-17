// net/portmap.cpp — see portmap.h. miniupnpc (third_party/miniupnpc, BSD-3).
#include "net/portmap.h"
#include "net/lan.h"
#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include <atomic>
#include <mutex>
#include <thread>

namespace dq::net {

struct PortMapper::State { std::atomic<bool> running{true}; std::mutex mx; std::vector<std::string> log; };

static void portMapRun(std::shared_ptr<PortMapper::State> S, u16 port);

void PortMapper::start(u16 port) {
  stop();
  st = std::make_shared<State>();
  std::thread([S = st, port] { portMapRun(S, port); }).detach();
}
void PortMapper::stop() {
  if (st) st->running = false;   // (the worker notices between steps, takes back what it added and ends)
  st.reset();
}
std::vector<std::string> PortMapper::takeLog() {
  if (!st) return {};
  std::lock_guard<std::mutex> lk(st->mx); auto v = std::move(st->log); st->log.clear(); return v;
}

// the default gateway (the home router), IPv4 as text ("" if unknown)
static std::string gatewayIPv4() {
#ifdef _WIN32
  MIB_IPFORWARDROW r{}; DWORD dst = 0; inet_pton(AF_INET, "8.8.8.8", &dst);
  if (GetBestRoute(dst, 0, &r) == NO_ERROR && r.dwForwardNextHop) { char b[32]; in_addr a{}; a.s_addr = r.dwForwardNextHop; inet_ntop(AF_INET, &a, b, sizeof b); return b; }
#else
  std::ifstream f("/proc/net/route");   // (Linux; Android may hide it: then the guess below)
  std::string line; std::getline(f, line);
  while (std::getline(f, line)) {
    std::istringstream is(line); std::string ifc, dest, gw; is >> ifc >> dest >> gw;
    if (dest == "00000000" && gw.size() == 8) { unsigned v = (unsigned)std::stoul(gw, nullptr, 16); in_addr a{}; a.s_addr = v; char b[32]; inet_ntop(AF_INET, &a, b, sizeof b); return b; }
  }
#endif
  const std::string me = localAddress();   // (most home networks: the router is .1)
  const size_t k = me.rfind('.');
  return k == std::string::npos ? std::string() : me.substr(0, k) + ".1";
}

// asked straight at the router: some Wi-Fi networks never deliver the search everyone hears (the router stays silent to
// that computer), but it answers one sent to it. Returns its description's address ("" if no answer).
static std::string askGatewayDirect(const std::string& gw) {
  if (gw.empty()) return "";
#ifdef _WIN32
  SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (s == INVALID_SOCKET) return "";
  DWORD tv = 2500; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#else
  int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (s < 0) return "";
  timeval tv{2, 500000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
#endif
  sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(1900); inet_pton(AF_INET, gw.c_str(), &to.sin_addr);
  std::string out;
  for (const char* st : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:2"}) {
    const std::string msg = std::string("M-SEARCH * HTTP/1.1\r\nHOST: ") + gw + ":1900\r\nST: " + st + "\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n\r\n";
    ::sendto(s, msg.data(), (int)msg.size(), 0, (sockaddr*)&to, sizeof to);
    char buf[2048]; const int n = (int)::recvfrom(s, buf, sizeof buf - 1, 0, nullptr, nullptr);
    if (n <= 0) continue;
    buf[n] = 0;
    std::string r = buf, low = r; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
    const size_t at = low.find("\nlocation:");
    if (at == std::string::npos) continue;
    size_t b = at + 10; while (b < r.size() && (r[b] == ' ' || r[b] == ':')) b++;
    size_t e = r.find_first_of("\r\n", b);
    out = r.substr(b, e == std::string::npos ? std::string::npos : e - b);
    if (!out.empty()) break;
  }
#ifdef _WIN32
  closesocket(s);
#else
  ::close(s);
#endif
  return out;
}

static void portMapRun(std::shared_ptr<PortMapper::State> S, u16 port) {
  auto say = [&](const std::string& l) { std::lock_guard<std::mutex> lk(S->mx); S->log.push_back(l); };
  auto on = [&] { return S->running.load(); };
  int err = 0;
  UPNPUrls urls{}; IGDdatas data{};
  char lan[64] = "", wan[64] = "";
  int igd = UPNP_NO_IGD;
  if (UPNPDev* devs = upnpDiscover(2000, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, 2, &err)) {
    igd = UPNP_GetValidIGD(devs, &urls, &data, lan, sizeof lan, wan, sizeof wan);
    freeUPNPDevlist(devs);
    if (igd == UPNP_UNKNOWN_DEVICE) { FreeUPNPUrls(&urls); igd = UPNP_NO_IGD; }   // (another computer's UPnP, not a router)
  }
  if (igd == UPNP_NO_IGD && on()) {   // nobody answered the search everyone hears: ask the router itself
    const std::string gw = gatewayIPv4(), url = askGatewayDirect(gw);
    if (!url.empty() && UPNP_GetIGDFromUrl(url.c_str(), &urls, &data, lan, sizeof lan) > 0) {
      igd = UPNP_CONNECTED_IGD;
      UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, wan);
      say("Found the router by asking it directly (" + gw + "): it didn't answer the usual search from this computer");
    }
  }
  if (igd == UPNP_NO_IGD) { say("No router that takes automatic port requests was found (UPnP is off or not there): friends from outside need the port forwarded by hand, or the IPv6 address"); return; }
  if (!on()) { FreeUPNPUrls(&urls); return; }
  if (igd == UPNP_PRIVATEIP_IGD)
    say(std::string("The router's own internet address is ") + wan + ", a private one: either it sits behind another router (forward the port on that one too) or your provider shares one address among many homes (CGNAT: then use the IPv6 address, or ask them for a public IPv4 address)");
  const std::string ps = std::to_string(port);
  bool mapped[2] = {false, false};
  const char* proto[2] = {"TCP", "UDP"};
  auto addAll = [&](bool quiet) {
    for (int k = 0; k < 2 && on(); k++) {
      int r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, ps.c_str(), ps.c_str(), lan, "DokuQuest", proto[k], nullptr, "3600");
      if (r == 725) r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, ps.c_str(), ps.c_str(), lan, "DokuQuest", proto[k], nullptr, "0");   // (725: only permanent ones allowed; taken back on close)
      mapped[k] = mapped[k] || r == UPNPCOMMAND_SUCCESS;
      if (!quiet) {
        if (r == UPNPCOMMAND_SUCCESS) say(std::string("The router forwarded ") + proto[k] + " port " + ps + " to this computer (" + lan + ") by itself (UPnP)");
        else say(std::string("The router refused to forward ") + proto[k] + " port " + ps + " to this computer (" + lan + "): " + strupnperror(r) + " (another computer may already have it: forward it here by hand)");
      }
    }
  };
  addAll(false);
  // IPv6: routers with IPv6 firewall control let this computer's own IPv6 address be reached on the port
  char pin[2][16] = {"", ""};
  const std::string six = internetAddress6();
  if (on() && !six.empty() && data.IPv6FC.controlurl[0]) {
    for (int k = 0; k < 2 && on(); k++) {
      const int r = UPNP_AddPinhole(urls.controlURL_6FC, data.IPv6FC.servicetype, "", "0", six.c_str(), ps.c_str(), k ? "17" : "6", "3600", pin[k]);
      say(std::string("The router's IPv6 firewall ") + (r == UPNPCOMMAND_SUCCESS ? "opened " : "wouldn't open ") + proto[k] + " port " + ps + " for [" + six + "]" + (r == UPNPCOMMAND_SUCCESS ? "" : std::string(": ") + strupnperror(r)));
      if (r != UPNPCOMMAND_SUCCESS) pin[k][0] = 0;
    }
  }
  // kept while open: renewed every half hour
  auto t0 = std::chrono::steady_clock::now();
  while (on()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (std::chrono::steady_clock::now() - t0 > std::chrono::minutes(30)) {
      t0 = std::chrono::steady_clock::now();
      addAll(true);
      for (int k = 0; k < 2; k++) if (pin[k][0]) UPNP_UpdatePinhole(urls.controlURL_6FC, data.IPv6FC.servicetype, pin[k], "3600");
    }
  }
  // closed: taken back (on this worker: nobody waits for the router)
  for (int k = 0; k < 2; k++) if (mapped[k]) UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, ps.c_str(), proto[k], nullptr);
  for (int k = 0; k < 2; k++) if (pin[k][0]) UPNP_DeletePinhole(urls.controlURL_6FC, data.IPv6FC.servicetype, pin[k]);
  FreeUPNPUrls(&urls);
}

} // namespace dq::net
