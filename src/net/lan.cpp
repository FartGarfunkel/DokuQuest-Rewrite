// net/lan.cpp — LAN announcements and the scanner (see net/lan.h). Uses ENet's portable socket calls.
#include <cstdio>
#include <cctype>
#include "net/lan.h"
#include "net/protocol.h"
#include <enet/enet.h>
#include <chrono>
#include <mutex>
#include <vector>
#ifdef _WIN32
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <shellapi.h>
#undef near   // windows.h (through enet) defines near and far as nothing
#undef far
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

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

// ---- the computer's network adapters: which one is the home network, and is a VPN on? ----------------------------
// A VPN (NordVPN, Proton, Mullvad…) takes the default route, so "the address that reaches the internet" is the VPN's own
// (10.5.0.2 for NordLynx) and the internet sees the VPN's address: the home address has to be looked for among the adapters.
namespace {
struct Adapter { std::string name, ip; bool vpn = false, other = false, gateway = false; };
std::string lowerOf(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
bool has(const std::string& s, std::initializer_list<const char*> keys) { for (auto k : keys) if (s.find(k) != std::string::npos) return true; return false; }
bool starts(const std::string& s, std::initializer_list<const char*> keys) { for (auto k : keys) if (s.rfind(k, 0) == 0) return true; return false; }
bool privateIp(const std::string& ip) {
  unsigned a = 0, b = 0;
  if (std::sscanf(ip.c_str(), "%u.%u", &a, &b) != 2) return false;
  return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}
// privacy VPNs (they hide you from the internet) vs. adapters that are neither home network nor VPN (mesh VPNs, VMs, docker)
bool vpnName(const std::string& n) {
  std::string l = lowerOf(n);
  return has(l, {"nord", "proton", "mullvad", "wintun", "wireguard", "openvpn", "tap-windows", "expressvpn", "surfshark", "cyberghost", "windscribe", "private internet", "pia "}) ||
         starts(l, {"tun", "tap", "wg", "ppp", "ipsec", "pvpn", "utun"});
}
bool otherName(const std::string& n) {
  std::string l = lowerOf(n);
  return l == "lo" || has(l, {"tailscale", "zerotier", "hamachi", "radmin", "hyper-v", "virtualbox", "vmware", "loopback", "vethernet", "bluetooth", "docker"}) ||
         starts(l, {"zt", "docker", "br-", "virbr", "veth", "vmnet", "vboxnet", "lxc", "lxd"});
}
std::vector<Adapter> adapters() {
  std::vector<Adapter> out;
#ifdef _WIN32
  ULONG len = 16 * 1024;
  std::vector<unsigned char> buf(len);
  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
  ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &len);
  if (r == ERROR_BUFFER_OVERFLOW) { buf.resize(len); r = GetAdaptersAddresses(AF_INET, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &len); }
  if (r != NO_ERROR) return out;
  for (auto* a = (IP_ADAPTER_ADDRESSES*)buf.data(); a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    char fn[256] = {0}, ds[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, fn, sizeof fn, nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, a->Description, -1, ds, sizeof ds, nullptr, nullptr);
    std::string both = std::string(fn) + " / " + ds;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
      char ip[64] = {0};
      inet_ntop(AF_INET, &((sockaddr_in*)u->Address.lpSockaddr)->sin_addr, ip, sizeof ip);
      Adapter ad; ad.name = std::string(fn).empty() ? ds : fn; ad.ip = ip;
      ad.other = otherName(both);   // a mesh network (Tailscale, ZeroTier…) or a VM isn't a VPN that hides you
      ad.vpn = !ad.other && (vpnName(both) || a->IfType == IF_TYPE_PPP || a->IfType == IF_TYPE_PROP_VIRTUAL);
      ad.gateway = a->FirstGatewayAddress != nullptr;
      out.push_back(ad);
    }
  }
#else
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return out;
  for (ifaddrs* i = list; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
    char ip[64] = {0};
    inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, ip, sizeof ip);
    Adapter ad; ad.name = i->ifa_name; ad.ip = ip;
    ad.other = otherName(ad.name);
    ad.vpn = !ad.other && (vpnName(ad.name) || (i->ifa_flags & IFF_POINTOPOINT));
    ad.gateway = true;
    out.push_back(ad);
  }
  freeifaddrs(list);
#endif
  return out;
}
// looked up at most every few seconds (the screens ask every frame)
struct AdapterCache { std::mutex mx; std::chrono::steady_clock::time_point at{}; std::vector<Adapter> list; bool ok = false; };
std::vector<Adapter> cachedAdapters() {
  static AdapterCache c;
  std::lock_guard<std::mutex> lk(c.mx);
  auto now = std::chrono::steady_clock::now();
  if (!c.ok || now - c.at > std::chrono::seconds(5)) { c.list = adapters(); c.at = now; c.ok = true; }
  return c.list;
}
} // namespace

std::string vpnOn() {
  for (auto& a : cachedAdapters())
    if (a.vpn) {
      std::string l = lowerOf(a.name);
      return l.find("nord") != std::string::npos ? "NordVPN" : l.find("proton") != std::string::npos ? "Proton VPN" : l.find("mullvad") != std::string::npos ? "Mullvad" : a.name;
    }
  return "";
}

std::string vpnAdvice(int port) {
  std::string v = vpnOn();
  if (v.empty()) return "";
  std::string p = std::to_string(port);
  return "A VPN is on (" + v + "): friends on the internet can't reach this computer through it, and the internet address it shows is the VPN's, "
         "not your home's. Turn it off while you play together, or let the game's port skip it" +
         (v == "NordVPN" ? std::string(" (NordVPN on Linux: nordvpn allowlist add port ") + p + " protocol UDP; on Windows: Split tunneling, turn the VPN off for DokuQuest)" : std::string(" (split tunnelling, or allow incoming on UDP port ") + p + ")") + ".";
}

bool firewallAllowSelf(std::string* err) {
#ifdef _WIN32
  // Windows asks once whether the game may use the network; turned down (or closed), it blocks the game from then on, and
  // antivirus firewalls do the same. Clear every rule for this program and allow it, in and out, on every kind of network,
  // through one "Yes" in Windows' own question (netsh needs an administrator).
  wchar_t exe[MAX_PATH] = {0};
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) { if (err) *err = "Couldn't find the game's own file"; return false; }
  const std::wstring e = exe;
  const std::wstring args = L"/c netsh advfirewall firewall delete rule name=all program=\"" + e +
                            L"\" & netsh advfirewall firewall add rule name=\"DokuQuest\" dir=in action=allow program=\"" + e +
                            L"\" enable=yes profile=any & netsh advfirewall firewall add rule name=\"DokuQuest\" dir=out action=allow program=\"" + e +
                            L"\" enable=yes profile=any";
  SHELLEXECUTEINFOW si{}; si.cbSize = sizeof si; si.fMask = SEE_MASK_NOCLOSEPROCESS; si.lpVerb = L"runas"; si.lpFile = L"cmd.exe";
  si.lpParameters = args.c_str(); si.nShow = SW_HIDE;
  if (!ShellExecuteExW(&si)) { if (err) *err = "Windows didn't let it through (the question was answered No)"; return false; }
  if (si.hProcess) {
    WaitForSingleObject(si.hProcess, 20000);
    DWORD code = 1; GetExitCodeProcess(si.hProcess, &code); CloseHandle(si.hProcess);
    if (code != 0) { if (err) *err = "Windows Firewall didn't take the rule (an antivirus may run the firewall instead: allow DokuQuest there)"; return false; }
  }
  DQ_LOG("net: DokuQuest is allowed through Windows Firewall now");
  return true;
#else
  if (err) *err = "Only on Windows";
  return false;
#endif
}

std::string localAddress() {
  // the home network: a private address on an adapter that isn't a VPN, a VM or a mesh network (one with a gateway first)
  {
    std::string best; int bestScore = -1;
    for (auto& a : cachedAdapters()) {
      if (a.vpn || a.other || !privateIp(a.ip)) continue;
      int sc = (a.gateway ? 2 : 0) + (a.ip.rfind("192.168.", 0) == 0 ? 1 : 0);
      if (sc > bestScore) { bestScore = sc; best = a.ip; }
    }
    if (!best.empty() && !vpnOn().empty()) return best;   // with a VPN on, the route below would give the VPN's address
  }
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


std::string internetAddress() {
  // the system's curl (every Linux desktop; Windows 10 and later have curl.exe): one short line back, the address
#ifdef _WIN32
  FILE* f = _popen("curl -s --max-time 6 https://api.ipify.org 2>NUL", "r");
#else
  FILE* f = popen("curl -s --max-time 6 https://api.ipify.org 2>/dev/null", "r");
#endif
  if (!f) return "";
  char buf[128] = {0}; size_t n = fread(buf, 1, sizeof buf - 1, f);
#ifdef _WIN32
  _pclose(f);
#else
  pclose(f);
#endif
  std::string a(buf, n);
  while (!a.empty() && (a.back() == '\n' || a.back() == '\r' || a.back() == ' ')) a.pop_back();
  for (char c : a) if (!(std::isdigit((unsigned char)c) || c == '.' || c == ':' || std::isxdigit((unsigned char)c))) return "";
  return a.size() >= 7 && a.size() <= 45 ? a : "";
}

} // namespace dq::net
