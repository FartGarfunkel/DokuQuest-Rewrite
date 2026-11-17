// tools/test_net.cpp — headless multiplayer + mods test: a dedicated server (with JS mods, a data/JS folder mod
// and a native plugin) and two scripted clients on this computer. Checks joining, LAN discovery, block and
// block-entity sync, entities as ghosts, chat, mod and plugin commands/events, server console commands
// (give, tp, kick), movement checks, saved players and the server going away.
//   test_net [--userdir DIR] [--port N]       exit code 0 = everything passed
#include "sim/attack_moves.h"
#include <set>
#include "core/init.h"
#include "server/server.h"
#include "net/client.h"
#include "net/server_host.h"
#include "net/lan.h"
#include "mod/modloader.h"
#include "sim/commands.h"
#include "gen/generator.h"
#include "sim/creature.h"
#include "sim/gameplay.h"
#include "sim/controller.h"
#include <map>
#include <array>
#include "sim/weather.h"
#include <atomic>
#include "sim/forge.h"
#include "sim/moving.h"
#include "sim/glue.h"
#include <thread>
#include <cstdio>

using namespace dq;

static int fails = 0, passes = 0;
static std::set<std::string> g_skinsSeen;   // skins handed to netSkinHook
static void check(bool ok, const std::string& what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  (ok ? passes : fails)++;
}

struct RecFx : LevelFx {
  std::vector<std::string> msgs; int sounds = 0;
  void message(u32, const std::string& t, const std::string&) override { msgs.push_back(t); }
  void sound(const std::string&, vec3, float, float, float) override { sounds++; }
  void material(const std::string&, const std::string&, vec3) override { sounds++; }
};

struct StationProbe { std::atomic<bool> armed{false}, listed{false}; std::atomic<int> x{0}, y{0}, z{0}, fuel{-1}; std::atomic<double> pump{0}; };
static StationProbe g_st;
// a slider the host moves (the moving-parts sync checks): 1 = build it and start it, then 2
static std::atomic<bool> g_freeze{false};   // the server's next tick stalls 8 s (a busy host: start-up, an autosave)
static struct { std::atomic<int> stage{0}; int x = 0, y = 0, z = 0; std::atomic<bool> started{false}; } g_mv;
struct Client {
  std::string name;
  EventBus bus;
  std::unique_ptr<Level> lv;
  std::shared_ptr<net::NetClient> nc;
  Player* me = nullptr;
  RecFx fx;
  std::string err;
  bool tcpOnly = false;   // (joined over a TCP connection)
  double tickAt = 0;
  bool noWorld = false;   // (only watching the others: no land made round them)
  bool join(const std::string& addr, ThreadPool* pool) {
    nc = std::make_shared<net::NetClient>();
    net::JoinOptions o; o.address = addr; o.name = name; o.viewDistance = 3; o.timeout = 15; o.tcpOnly = tcpOnly;
    o.skin = name + ".png"; o.skinData = {{"png", "c2tpbg=="}, {"slim", name == "Alice"}};   // (a stand-in picture: only the plumbing is checked)
    if (!nc->connect(o)) { err = nc->error(); nc.reset(); return false; }
    fs::path dir = userSub("cache") / "test" / name;
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
    lv = std::make_unique<Level>(nc->levelInfo(), dir, pool, &bus);
    nc->attach(*lv);
    lv->net = nc;
    lv->fx = &fx;
    auto p = std::make_unique<Player>();
    p->load(nc->playerRecord()); p->name = name;
    me = static_cast<Player*>(lv->spawn(std::move(p)));
    return true;
  }
  void step(double dt) {
    if (!lv) return;
    lv->net->update(*lv, dt);
    if (!lv) return;
    tickAt = nowSeconds();   // (its replicas glide to this moment)
    lv->tick(dt);
    if (!noWorld) lv->world(me->realm).stream({{me->pos, 3, false}}, 4);
  }
  bool heard(const std::string& s) const { for (auto& m : fx.msgs) if (m.find(s) != std::string::npos) return true; return false; }
  Entity* ghost(const std::string& type, const std::string& pname = "") {
    if (!lv) return nullptr;
    for (auto& e : lv->entities()) if (!e->removed && e->ghost && e->type == type) {
      if (pname.empty()) return e.get();
      if (auto* p = dynamic_cast<Player*>(e.get())) if (p->name == pname) return e.get();
    }
    return nullptr;
  }
  void leave() { if (nc) nc->close(); if (lv) { lv->net.reset(); lv.reset(); } nc.reset(); me = nullptr; }
};

static std::vector<Client*> g_clients;
// run the clients (the server has its own thread) until `done` or the time is up
template <class F> static bool pump(double seconds, F done) {
  double t0 = nowSeconds();
  while (nowSeconds() - t0 < seconds) {
    for (auto* c : g_clients) c->step(1.0 / 30);
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  return done();
}
static void pumpFor(double s) { pump(s, [] { return false; }); }

static void writeMods() {
  fs::path m = userSub("mods");
  writeFileAtomic(m / "greeter.js", R"JS(DQ.name = 'Greeter';
DQ.plugin = true;
DQ.on('join', (name) => DQ.say('Welcome, ' + name + '!'));
DQ.on('blockBreak', (player, x, y, z, key) => { DQ.say(player.name + ' broke ' + key); });
DQ.on('blockPlace', (player, key) => { if (key === 'gold_block') return false; });
DQ.command('roll', 'Roll a die', (player, args) => player.name + ' rolls a ' + (1 + Math.floor(Math.random() * (+args[0] || 6))));
DQ.command('spawnit', 'Spawn a creature next to you', (player, args) => DQ.spawn(args[0] || 'cow', player.x + 2, player.y, player.z) ? 'spawned' : 'no');
DQ.command('who', 'List players', () => DQ.players().map(p => p.name).sort().join(','));
DQ.item('lucky_charm', { name: 'Lucky Charm', icon: 'emerald', desc: 'It does nothing. Probably.' });
DQ.recipe({ out: 'lucky_charm', n: 1, items: ['emerald', 'gold_nugget', 'string'] });
setTimeout(() => DQ.log('timer fired'), 50);
)JS");
  writeFileAtomic(m / "hog.js", "DQ.name = 'Hog';\nDQ.on('tick', () => { while (true) {} });\n");
  writeFileAtomic(m / "broken.js", "DQ.name = 'Broken';\nDQ.on('tick', () => { \n");
  fs::create_directories(m / "gems" / "textures");
  writeJson(m / "gems" / "mod.json", {{"name", "Gems"}, {"version", "1.0"}, {"author", "Test"}});
  writeFileAtomic(m / "gems" / "main.js", "DQ.block('ruby_block', { name: 'Ruby Block', hardness: 3, tex: 'ruby_block' });\n");
  writeFileAtomic(m / "gems" / "textures" / "ruby_block.png", std::string("not really a png"));
#ifdef DQ_SAMPLE_PLUGIN
  fs::create_directories(m / "native_demo");
  std::error_code ec;
  fs::copy_file(DQ_SAMPLE_PLUGIN, m / "native_demo" / fs::path(DQ_SAMPLE_PLUGIN).filename(), fs::copy_options::overwrite_existing, ec);
#endif
}

// ---- smooth replicas (sim/net_interp.cpp) ----
// A path drawn on the viewer's screen, frame by frame: (time, x, yaw). Smooth means: never a step backwards along the
// way it walks, the speed steady (no lurches), near the true path (a steady lag behind it), the turning never spun the
// long way round.
struct PathStats { double minV = 1e9, maxV = -1e9, maxAcc = 0, back = 0, lagMean = 0, lagSpread = 0, offPath = 0, maxTurn = 0; int frames = 0; };
static PathStats pathStats(const std::vector<std::array<double, 6>>& f, double speed, double omega, double x0, double t0) {
  // f: {t, x, y_off_path, z_off_path, yaw, segment} (a new segment after a spell with nothing heard: not compared across)
  PathStats s;
  std::vector<double> lags;
  for (size_t i = 1; i < f.size(); i++) {
    const double dt = f[i][0] - f[i - 1][0];
    if (dt <= 1e-4 || f[i][5] != f[i - 1][5]) continue;
    s.back = std::max(s.back, f[i - 1][1] - f[i][1]);
    s.maxTurn = std::max(s.maxTurn, std::fabs(std::remainder(f[i][4] - f[i - 1][4], 2 * PI)) / dt);
    s.offPath = std::max({s.offPath, std::fabs(f[i][2]), std::fabs(f[i][3])});
    lags.push_back((f[i][0] - t0) - (f[i][1] - x0) / speed);
  }
  // speeds over 50 ms windows (a frame's own timing is noisy in a test loop)
  std::vector<std::pair<double, double>> v;
  size_t j = 0;
  for (size_t i = 0; i < f.size(); i++) {
    if (j < i) j = i;
    while (j < f.size() && f[j][0] - f[i][0] < 0.05) j++;
    if (j >= f.size()) break;
    if (f[j][5] != f[i][5]) { v.push_back({-1, 0}); continue; }   // (a break: the windows either side aren't compared)
    v.push_back({(f[i][0] + f[j][0]) / 2, (f[j][1] - f[i][1]) / (f[j][0] - f[i][0])});
  }
  for (size_t i = 0; i < v.size(); i++) {
    if (v[i].first < 0) continue;
    s.minV = std::min(s.minV, v[i].second); s.maxV = std::max(s.maxV, v[i].second);
    if (i >= 3 && v[i - 1].first >= 0 && v[i - 2].first >= 0 && v[i - 3].first >= 0) s.maxAcc = std::max(s.maxAcc, std::fabs(v[i].second - v[i - 3].second) / std::max(1e-3, v[i].first - v[i - 3].first));
  }
  double m = 0; for (double l : lags) m += l; m /= std::max<size_t>(1, lags.size());
  double sd = 0; for (double l : lags) sd += (l - m) * (l - m); sd = std::sqrt(sd / std::max<size_t>(1, lags.size()));
  s.lagMean = m; s.lagSpread = sd; s.frames = (int)f.size();
  (void)omega;
  return s;
}
static std::string describe(const PathStats& s) {
  return strFormat("speed %.2f..%.2f b/s, accel <= %.1f b/s2, back %.3f, lag %.0f ms (+-%.0f), off path %.3f, turn <= %.1f rad/s, %d frames",
                   s.minV, s.maxV, s.maxAcc, s.back, s.lagMean * 1000, s.lagSpread * 1000, s.offPath, s.maxTurn, s.frames);
}

// The sender walks at 4 b/s turning at 2 rad/s (through the half-turn where the angle wraps), sending 20 times a second;
// each packet comes 20-220 ms late (so they overtake each other), one in ten never, and once nothing at all for 0.4 s.
// The viewer draws at 60 frames a second.
static bool g_dumpPath = false;   // (DQ_INTERP_ONLY: the path printed, and nothing else run)
static PathStats replicaUnderJitter(u32 seed) {
  Entity e; e.ghost = true;
  auto rnd01 = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
  const double speed = 4, omega = 2, base = 50;   // (both clocks: ours starts at 50, the sender's at 0 — a different clock)
  struct Pkt { double arrive, st; vec3 p; float yaw; };
  std::vector<Pkt> pkts;
  for (double st = 0; st < 8.0; st += 0.05) {
    if (rnd01() < 0.1) continue;
    if (st > 4.0 && st < 4.3) continue;   // an outage (nothing for 0.3 s, and the losses either side)
    const float yaw = (float)std::remainder(omega * st + 3.0, 2 * PI);
    pkts.push_back({base + st + 0.02 + rnd01() * 0.2, st, vec3((float)(st * speed), 7, 3), yaw});
  }
  std::sort(pkts.begin(), pkts.end(), [](const Pkt& a, const Pkt& b) { return a.arrive < b.arrive; });
  net::NetClock clock;
  size_t next = 0;
  std::vector<std::array<double, 6>> f;
  for (double now = base; now < base + 8.0; now += 1.0 / 60) {
    while (next < pkts.size() && pkts[next].arrive <= now) {
      const Pkt& k = pkts[next++];
      clock.heard(k.st, k.arrive);
      e.netSample(clock.toLocal(k.st), k.arrive, k.p, k.yaw, 0, k.yaw);
    }
    if (e.netHist.empty()) continue;
    e.netUpdateView(now);
    if (now > base + 1.0 && now < base + 7.5) f.push_back({now, e.viewPos.x, e.viewPos.y - 7, e.viewPos.z - 3, e.viewYaw, 0});
  }
  if (g_dumpPath) for (auto& r : f) printf("path %.4f %.4f %.4f delay %.3f\n", r[0] - base, r[1], r[4], e.netDelay);
  return pathStats(f, speed, omega, 0, base);
}
// The way replicas were drawn before (alpha.33), for comparison on the same link: entity snapshots reliable and in order
// (a lost one sent again a round trip later, everything after it held back meanwhile), the place eased into the sim's
// 30-a-second ticks and drawn as it stood at the last tick, a fixed 0.15 s behind, a late packet taken for a stop.
static PathStats replicaTheOldWay(u32 seed) {
  auto rnd01 = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
  const double speed = 4, omega = 2, base = 50;
  struct Pkt { double arrive, st; vec3 p; float yaw; };
  std::vector<Pkt> pkts;
  double held = 0;
  for (double st = 0; st < 8.0; st += 0.05) {
    double arrive = base + st + 0.02 + rnd01() * 0.2;
    if (rnd01() < 0.1 || (st > 4.0 && st < 4.3)) arrive += 0.25;   // (lost: sent again)
    held = std::max(held, arrive);   // (in order: none before the one held up)
    pkts.push_back({held, st, vec3((float)(st * speed), 7, 3), (float)std::remainder(omega * st + 3.0, 2 * PI)});
  }
  struct S { double t; vec3 p; float yaw; };
  std::vector<S> hist;
  double off = 0; bool known = false; size_t next = 0;
  vec3 pos(0); float yaw = 0; double tickAcc = 0;
  std::vector<std::array<double, 6>> f;
  for (double now = base; now < base + 8.0; now += 1.0 / 60) {
    while (next < pkts.size() && pkts[next].arrive <= now) {
      const Pkt& k = pkts[next++];
      const double o = k.st - k.arrive;
      if (!known || o > off + 1 || o < off - 1) { off = o; known = true; } else off = o > off ? o : off + (o - off) * 0.02;
      if (!hist.empty() && k.st <= hist.back().t) continue;
      if (!hist.empty() && k.st - hist.back().t > 0.15) hist.push_back({k.st - 0.1, hist.back().p, hist.back().yaw});
      hist.push_back({k.st, k.p, k.yaw});
      if (hist.size() > 24) hist.erase(hist.begin());
    }
    tickAcc += 1.0 / 60;
    while (tickAcc >= 1.0 / 30) {   // (moved in the sim's ticks only)
      tickAcc -= 1.0 / 30;
      if (hist.empty()) continue;
      const double rt = now + off - 0.15;
      if (rt <= hist.front().t) { pos = hist.front().p; yaw = hist.front().yaw; }
      else if (rt >= hist.back().t) {
        pos = hist.back().p; yaw = hist.back().yaw;
        if (hist.size() >= 2) { const S& a = hist[hist.size() - 2]; const S& b = hist.back(); const double span = b.t - a.t, ahead = std::min(rt - b.t, 0.2);
          if (span > 1e-3 && span < 0.5) pos = b.p + (b.p - a.p) * (float)(ahead / span); }
      } else {
        size_t i = 1; while (i < hist.size() && hist[i].t < rt) i++;
        const S& a = hist[i - 1]; const S& b = hist[i]; const float k = (float)((rt - a.t) / std::max(1e-6, b.t - a.t));
        pos = glm::mix(a.p, b.p, k); yaw = a.yaw + std::remainder(b.yaw - a.yaw, 2 * PI) * k;
      }
    }
    if (now > base + 1.0 && now < base + 7.5 && !hist.empty()) f.push_back({now, pos.x, pos.y - 7, pos.z - 3, yaw, 0});
  }
  return pathStats(f, speed, omega, 0, base);
}


#ifndef _WIN32
// A bad internet link between a player and the server, on this computer: every datagram held back a random while (so
// they overtake each other), some lost. Or, for a join carried over TCP, the stream held back in random lumps.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
struct BadLink {
  std::thread th; std::atomic<bool> run{true};
  int port = 0;
  ~BadLink() { run = false; if (th.joinable()) th.join(); }
  static sockaddr_in at(int port) { sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((u16)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); return a; }
  // UDP: listens on `port`, passes to the server's; delays lo..hi seconds, `loss` of them dropped (both ways)
  void udp(int listenPort, int serverPort, double lo, double hi, double loss) {
    port = listenPort;
    th = std::thread([=, this] {
      int L = socket(AF_INET, SOCK_DGRAM, 0), U = socket(AF_INET, SOCK_DGRAM, 0);
      sockaddr_in la = at(listenPort), sa = at(serverPort), ca{}; bool haveC = false;
      bind(L, (sockaddr*)&la, sizeof la);
      struct Q { double at; bool up; std::vector<u8> d; };
      std::vector<Q> q; u32 seed = 777;
      auto r01 = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
      u8 buf[65536];
      while (run) {
        pollfd p[2] = {{L, POLLIN, 0}, {U, POLLIN, 0}};
        poll(p, 2, 1);
        const double now = nowSeconds();
        for (int k = 0; k < 2; k++) if (p[k].revents & POLLIN) {
          sockaddr_in from{}; socklen_t fl = sizeof from;
          const ssize_t n = recvfrom(k == 0 ? L : U, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
          if (n <= 0) continue;
          if (k == 0) { ca = from; haveC = true; }
          if (r01() < loss) continue;
          q.push_back({now + lo + (hi - lo) * r01(), k == 0, std::vector<u8>(buf, buf + n)});
        }
        for (size_t i = 0; i < q.size();) {
          if (q[i].at > now) { i++; continue; }
          if (q[i].up) sendto(U, q[i].d.data(), q[i].d.size(), 0, (sockaddr*)&sa, sizeof sa);
          else if (haveC) sendto(L, q[i].d.data(), q[i].d.size(), 0, (sockaddr*)&ca, sizeof ca);
          q[i] = std::move(q.back()); q.pop_back();
        }
      }
      close(L); close(U);
    });
  }
  // TCP: one connection on `port`, passed to the server's TCP port; each lump read waits lo..hi more (in order)
  void tcp(int listenPort, int serverPort, double lo, double hi) {
    port = listenPort;
    int L = socket(AF_INET, SOCK_STREAM, 0); int one = 1; setsockopt(L, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in la = at(listenPort); bind(L, (sockaddr*)&la, sizeof la); listen(L, 2);
    th = std::thread([=, this] {
      int C = -1, S = -1;
      while (run && C < 0) { pollfd p{L, POLLIN, 0}; if (poll(&p, 1, 10) > 0) C = accept(L, nullptr, nullptr); }
      if (C >= 0) { S = socket(AF_INET, SOCK_STREAM, 0); sockaddr_in sa = at(serverPort); if (connect(S, (sockaddr*)&sa, sizeof sa) != 0) { close(S); S = -1; } }
      struct Q { double at; std::vector<u8> d; };
      std::vector<Q> up, down; u32 seed = 4242;
      auto r01 = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
      u8 buf[65536];
      while (run && C >= 0 && S >= 0) {
        pollfd p[2] = {{C, POLLIN, 0}, {S, POLLIN, 0}};
        poll(p, 2, 1);
        const double now = nowSeconds();
        bool shut = false;
        for (int k = 0; k < 2; k++) if (p[k].revents & (POLLIN | POLLHUP)) {
          const ssize_t n = recv(k == 0 ? C : S, buf, sizeof buf, 0);
          if (n <= 0) { shut = true; continue; }
          auto& qq = k == 0 ? up : down;
          const double last = qq.empty() ? 0 : qq.back().at;
          qq.push_back({std::max(last, now + lo + (hi - lo) * r01()), std::vector<u8>(buf, buf + n)});
        }
        for (auto* qq : {&up, &down}) while (!qq->empty() && qq->front().at <= now) {
          const auto& d = qq->front().d; size_t o = 0;
          while (o < d.size()) { const ssize_t w = send(qq == &up ? S : C, d.data() + o, d.size() - o, MSG_NOSIGNAL); if (w <= 0) break; o += (size_t)w; }
          qq->erase(qq->begin());
        }
        if (shut) break;
      }
      if (C >= 0) close(C);
      if (S >= 0) close(S);
      close(L);
    });
  }
};
#endif


int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  std::string ud = "/tmp/dq-test-net";
  int port = 25597;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--userdir" && i + 1 < argc) ud = argv[++i];
    else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
  }
  std::error_code ec;
  fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  writeMods();

  ServerConfig cfg; cfg.world = "NetTest"; cfg.seed = "multiplayer"; cfg.port = port; cfg.motd = "Hello from the test server"; cfg.viewDistance = 4; cfg.upnp = false;   // (never ask the real router in a test)
  DedicatedServer srv(cfg);
  // the host's weather, read on the server thread (weather sync checks below)
  static std::atomic<int> hostKind{0}; static std::atomic<float> hostWind{0};
  srv.onTick = [](Level& lv) {
    if (g_freeze.exchange(false)) std::this_thread::sleep_for(std::chrono::seconds(8));
    if (g_mv.stage == 1) {
      World& w = lv.world(REALM_MAIN);
      const int x = g_mv.x, y = g_mv.y, z = g_mv.z;
      if (w.isLoaded(x, y, z) && w.isLoaded(x + 5, y, z)) {
        for (int i = 0; i <= 5; i++) w.setBlock(x + i, y, z, AIR);
        w.setBlock(x, y, z, reg().blockId("slider"), 5); w.setBlock(x + 1, y, z, reg().blockId("planks"));
        setGlue(w, ivec3(x, y, z), 0, true);
        machineState(w, ivec3(x, y, z))["distance"] = 4;
        g_mv.started = startMove(lv, REALM_MAIN, ivec3(x, y, z), nullptr);
        g_mv.stage = 2;
      }
    }
    if (g_st.armed) {   // a station a guest built and lit, as the host sees it (the immersive stations' sync checks below)
      World& w = lv.world(REALM_MAIN);
      const int x = g_st.x, y = g_st.y, z = g_st.z;
      const json* e = w.blockEntity(x, y, z);
      g_st.fuel = e && e->is_object() ? e->value("fuel", 0) : -1;
      bool listed = false; const json& l = lv.info.data.value("forges", json::array());
      for (auto& q : l) if (q.is_array() && q.size() >= 4 && q[1] == x && q[2] == y && q[3] == z) listed = true;
      g_st.listed = listed;
      const json* b = w.blockEntity(x, y, z + 1);
      g_st.pump = b && b->is_object() ? b->value("pump", 0.0) : 0.0;
    }
    if (WeatherSystem* W = weatherOf(lv)) { const std::string k = W->stormKind(); hostKind = k == "thunder" ? 2 : k == "rain" ? 1 : 0; hostWind = W->windDir; }
  };
  std::string err;
  net::netSkinHook() = [](const std::string& ref, const json&) { g_skinsSeen.insert(ref); };
  {   // replicas drawn smoothly under a bad link (sim/net_interp.cpp): late, lost, overtaking packets and an outage
    g_dumpPath = std::getenv("DQ_INTERP_DUMP") != nullptr;
    const bool only = std::getenv("DQ_INTERP_ONLY") != nullptr;
    for (u32 seed : {12345u, 999u, 31337u}) {
      const PathStats s = replicaUnderJitter(seed);
      check(s.back < 0.02 && s.minV > 2.0 && s.maxV < 6.0 && s.maxAcc < 40 && s.lagSpread < 0.06 && s.offPath < 0.02 && s.maxTurn < 6,
            "a player walking at 4 b/s and turning, sent over a link 20-220 ms late, 10% lost, 0.3 s cut: drawn smoothly (" + describe(s) + ")");
      printf("     (the old way, on the same link: %s)\n", describe(replicaTheOldWay(seed)).c_str());
    }
    if (only) return fails ? 1 : 0;
  }
  check(srv.start(&err), "server starts " + err);
  if (!srv.running()) return 1;
  std::string addr = "127.0.0.1:" + std::to_string(port);
  ThreadPool pool(2);

  // ---- mods on the server ----
  {
    ModLoader* ml = srv.modLoader();
    auto find = [&](const std::string& id) -> ModInfo* { return ml ? ml->find(id) : nullptr; };
    check(find("greeter") && find("greeter")->loaded && find("greeter")->plugin, "JS plugin loaded (DQ.plugin = true)");
    check(find("broken") && !find("broken")->loaded && !find("broken")->error.empty(), "a mod with a syntax error is reported: " + (find("broken") ? find("broken")->error : ""));
    check(reg().item("lucky_charm") != nullptr, "DQ.item registers an item");
    bool rec = false; for (auto& r : reg().recipes) if (r.out == "lucky_charm") rec = true;
    check(rec, "DQ.recipe adds a recipe");
    check(reg().blockId("ruby_block") != AIR && reg().item("ruby_block"), "folder mod (mod.json + main.js) registers a block and its item");
    bool tex = false; for (auto& [n, p] : modTextures()) if (n == "ruby_block") tex = true;
    check(tex, "modTextures() lists the mod's textures/ruby_block.png");
#ifdef DQ_SAMPLE_PLUGIN
    check(find("native_demo") && find("native_demo")->loaded && reg().item("native_gem"), "native plugin loaded (dq_plugin_init, registerItem)");
#endif
    check(ModLoader::checkSyntax("DQ.on('x', () => 1);", nullptr) && !ModLoader::checkSyntax("DQ.on(", nullptr), "syntax check like new Function('DQ', code)");
  }

  // ---- LAN discovery ----
  {
    net::LanScanner sc;
    bool started = sc.start();
    bool heard = false;
    double t0 = nowSeconds();
    while (started && nowSeconds() - t0 < 4 && !heard) { sc.poll(); for (auto& g : sc.list()) if (g.port == port) heard = true; std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    check(heard, "LAN scanner hears the server's announcement on UDP 25581");
  }

  // ---- refusals ----
  {
    net::NetClient bad;
    net::JoinOptions o; o.address = addr; o.name = "no/slashes"; o.timeout = 15;   // (the client's own wait: a fresh server is still warming up for its first few seconds)
    bool refused = !bad.connect(o);
    check(refused && bad.error().find("name") != std::string::npos, "a bad name is refused: " + bad.error());
  }

  // ---- two players join ----
  Client A, B; A.name = "Alice"; B.name = "Bob";
  check(A.join(addr, &pool), "Alice joins " + A.err);
  g_clients = {&A};
  pumpFor(1.0);
  check(B.join(addr, &pool), "Bob joins " + B.err);
  g_clients = {&A, &B};
  check(A.lv && A.lv->isRemote() && A.lv->info.seedText == "multiplayer", "the client's level is a remote copy with the server's seed");
  check(pump(6, [&] { return A.heard("Welcome, Bob!"); }), "plugin hears the join and DQ.say reaches everyone");
  check(pump(4, [&] { return B.heard("Hello from the test server"); }), "the welcome message (motd) arrives");
  check(pump(6, [&] { return A.ghost("player", "Bob") && B.ghost("player", "Alice"); }), "each sees the other as a ghost player");
  {
    Player* ga = B.ghost("player", "Alice") ? dynamic_cast<Player*>(B.ghost("player", "Alice")) : nullptr;
    const std::string want = net::netSkinRef(json{{"png", "c2tpbg=="}, {"slim", true}});
    check(ga && ga->skin == want && g_skinsSeen.count(want), "a player's skin travels with them: the others get it by its net name (" + (ga ? ga->skin : std::string("-")) + ")");
  }
  check(A.me && A.me->id >= net::LOCAL_ID_BASE, "the local player has a client-side id");
  {   // a bite of food: the others see it in her hand, bitten down (render/char_layer wornItem: eatModelKey)
    A.me->eatAnim = EAT_ANIM; A.me->eatId = "apple";
    auto ga = [&] { return B.ghost("player", "Alice") ? dynamic_cast<Player*>(B.ghost("player", "Alice")) : nullptr; };
    check(pump(4, [&] { A.me->eatAnim = EAT_ANIM; return ga() && ga()->eatT > 0 && ga()->eatId == "apple"; }), "Alice's bite of an apple reaches Bob");
    A.me->eatAnim = 0; A.me->eatId.clear();
    check(pump(4, [&] { A.me->eatAnim = 0; return ga() && ga()->eatT == 0 && ga()->eatId.empty(); }), "...and ends");
    // a swing (sim/attack_moves.h): Bob draws Alice's move at its time and facing, played on between the messages
    check(pump(4, [&] { A.me->atkMove = MV_SWORD2; A.me->atkT = 0.05f; A.me->atkYaw = 1.25f;
                        return ga() && ga()->atkMove == MV_SWORD2 && std::fabs(ga()->atkYaw - 1.25f) < 0.01f && ga()->atkT >= 0.04f; }),
          "Alice's swing reaches Bob (move, time, facing)");
    A.me->atkMove = MV_NONE; A.me->atkT = 0;
    check(pump(4, [&] { A.me->atkMove = MV_NONE; return ga() && ga()->atkMove == MV_NONE; }), "...and ends");
    // what she's up to (render/activity_layer.cpp): typing, then in a menu, then dozing, then nothing
    for (u8 act : {(u8)Player::ACT_TYPING, (u8)Player::ACT_SCREEN, (u8)Player::ACT_DOZE, (u8)0}) {
      A.me->activity = act; A.me->typeRate = act == Player::ACT_TYPING ? 7.f : 0.f;
      check(pump(4, [&] { return ga() && ga()->activity == act && (act != Player::ACT_TYPING || ga()->typeRate > 6.5f); }), "Bob sees what Alice is up to (" + std::to_string(act) + ")");
    }
  }

  // wait until both have the ground round spawn
  vec3 at = A.me->pos;
  int bx = ifloor(at.x) + 2, bz = ifloor(at.z) + 1;
  pump(10, [&] { return A.lv->world().isLoaded(bx, (int)at.y, bz) && B.lv->world().isLoaded(bx, (int)at.y, bz); });
  int by = std::max(A.lv->world().surfaceY(bx, bz, dimOfY((int)at.y)) + 1, (int)at.y);
  check(A.lv->world().isLoaded(bx, by, bz) && B.lv->world().isLoaded(bx, by, bz), "both have the chunks round spawn");

  // ---- blocks ----
  BlockId stone = reg().blockId("stone");
  A.lv->world().setBlock(bx, by, bz, stone, 0, SetFlags::Urgent);
  check(pump(4, [&] { return B.lv->world().getBlock(bx, by, bz) == stone; }), strFormat("a block Alice places shows up for Bob (%d,%d,%d)", bx, by, bz));
  A.lv->world().setBlock(bx, by, bz, AIR, 0, SetFlags::Urgent);
  check(pump(4, [&] { return B.lv->world().getBlock(bx, by, bz) == AIR; }), "and her breaking it too");
  check(pump(4, [&] { return B.heard("Alice broke stone"); }), "the server's plugin heard blockBreak (player object + key)");
  BlockId gold = reg().blockId("gold_block");
  A.lv->world().setBlock(bx, by + 1, bz, gold, 0, SetFlags::Urgent);
  check(pump(4, [&] { return A.lv->world().getBlock(bx, by + 1, bz) != gold; }) && B.lv->world().getBlock(bx, by + 1, bz) != gold,
        "a plugin cancelling blockPlace puts the block back for the player");
  // block entities
  A.lv->world().setBlock(bx, by, bz + 1, reg().blockId("chest") ? reg().blockId("chest") : stone, 0, SetFlags::Urgent);
  pumpFor(0.3);
  A.lv->world().setBlockEntity(bx, by, bz + 1, {{"type", "chest"}, {"items", json::array({{{"id", "apple"}, {"n", 3}}})}});
  check(pump(4, [&] { json* e = B.lv->world().blockEntity(bx, by, bz + 1); return e && e->value("type", "") == "chest"; }), "a block entity (chest contents) syncs");

  // ---- moving parts: the host moves a piece; a guest sees it move, then set down ----
  {
    g_mv.x = bx - 3; g_mv.y = by + 4; g_mv.z = bz + 3; g_mv.stage = 1;
    pump(4, [&] { return g_mv.stage == 2; });
    check(g_mv.started, "the host starts a slider moving");
    float f0 = -1, f1 = -1;
    check(pump(2, [&] { auto& L = movingBodiesList(*B.lv); if (!L.empty()) f0 = L[0].fraction(); return !L.empty() && L[0].blocks.size() == 1; }), "a guest gets the moving piece (what it carries and how it moves)");
    pumpFor(0.4);
    { auto& L = movingBodiesList(*B.lv); if (!L.empty()) f1 = L[0].fraction(); }
    check(f1 > f0 && f1 > 0, strFormat("and sees it move smoothly on its own (%.2f then %.2f)", f0, f1));
    check(pump(4, [&] { return movingBodiesList(*B.lv).empty() && B.lv->world().getBlock(g_mv.x + 5, g_mv.y, g_mv.z) == reg().blockId("planks"); }), "when it's set down the guest has the block where it ended");
  }

  // ---- joining over TCP (a network that drops UDP from outside): the same game, carried over a TCP connection ----
  {
    net::NetClient T; net::JoinOptions o; o.address = addr; o.name = "Tess"; o.tcpOnly = true;
    const bool ok = T.connect(o);
    check(ok && T.overTcp(), "a player whose UDP gets no answer joins over TCP (" + T.error() + ")");
    if (ok) {
      json have = json::object();
      check(pump(4, [&] { return B.ghost("player", "Tess") != nullptr; }), "and the others see them, as any player");
    }
    T.close();
    // the case from a real home: not one UDP packet gets through, TCP does. The UDP attempt waits 4 s, then TCP takes over.
    net::NetClient U; net::JoinOptions u; u.address = addr; u.name = "Uma"; u.testUdpLost = true;
    const double t0 = nowSeconds();
    const bool ok2 = U.connect(u);
    check(ok2 && U.overTcp(), strFormat("UDP lost on the way: the join goes through over TCP at once (%.1f s)", nowSeconds() - t0));
    if (ok2) check(pump(4, [&] { return B.ghost("player", "Uma") != nullptr; }), "and she plays like anyone else");
    U.close();
    // over IPv6 (a phone on mobile data is reached that way): the address typed in brackets, the join carried over TCP
    {
      std::string h6; int p6 = 0; net::parseAddress(addr, h6, p6);
      net::NetClient W; net::JoinOptions wo; wo.address = "[::1]:" + std::to_string(p6); wo.name = "Wren";
      const bool ok6 = W.connect(wo);
      check(ok6 && W.overTcp(), "joining by an IPv6 address works (over TCP) " + W.error());
      if (ok6) check(pump(4, [&] { return B.ghost("player", "Wren") != nullptr; }), "and she's in the world like anyone");
      W.close();
      net::NetClient X; net::JoinOptions xo; xo.address = "no-such-host.invalid:" + std::to_string(p6); xo.name = "Xan";
      const double tx = nowSeconds(); const bool okx = X.connect(xo);
      check(!okx && X.error().find("Could not find") != std::string::npos && nowSeconds() - tx < 10, "a name that doesn't exist says so (" + X.error() + ")");
    }
    // a host that stalls for 8 s while someone joins: UDP and TCP both answer when it wakes; UDP is kept
    net::NetClient V; net::JoinOptions vo; vo.address = addr; vo.name = "Vic";
    V.begin(vo); g_freeze = true;
    while (V.state() == net::NetClient::State::Connecting || V.state() == net::NetClient::State::Joining) { V.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    check(V.state() == net::NetClient::State::Ready && !V.overTcp(), "a host busy for 8 s: the join still goes through, and stays on UDP");
    int vics = 0; pump(3, [&] { vics = 0; for (auto& e : B.lv->entities()) if (!e->removed && e->type == "player" && static_cast<Player*>(e.get())->name == "Vic") vics++; return vics > 0; });
    check(vics == 1, "one of them, not two (" + std::to_string(vics) + ")");
    V.close();
  }

  // ---- chat & commands ----
  submitChat(*A.lv, A.me, "hello there");
  check(pump(3, [&] { return B.heard("<Alice> hello there"); }), "chat reaches the other player");
  submitChat(*A.lv, A.me, "/roll");
  check(pump(3, [&] { return A.heard("Alice rolls a"); }), "a mod's command (DQ.command) answers in chat");
  submitChat(*B.lv, B.me, "/who");
  check(pump(3, [&] { return B.heard("Alice,Bob"); }), "DQ.players() lists everyone");

  // ---- a couch player of Alice's (split screen while she's a guest): their own light link to the host ----
  if (std::getenv("NO_COUCH") == nullptr) {
    auto cp = std::make_unique<Player>();
    cp->name = "Carol"; cp->pos = A.me->pos + vec3(1.2f, 0, 0); cp->realm = A.me->realm;
    Player* carol = static_cast<Player*>(A.lv->spawn(std::move(cp)));
    check(A.nc->addCouch(*carol, json{{"png", "c2tpbg=="}, {"slim", false}}), "a guest's couch player starts joining the host");
    check(pump(8, [&] { return B.ghost("player", "Carol") != nullptr; }), "the others see the couch player");
    pumpFor(1.0);
    int carolGhostsAtAlice = 0;
    for (auto& e : A.lv->entities()) if (!e->removed && e->ghost && e->isPlayer()) if (auto* p = dynamic_cast<Player*>(e.get())) if (p->name == "Carol") carolGhostsAtAlice++;
    check(carolGhostsAtAlice == 0, "and on the guest's own screen the couch player isn't doubled by a ghost of themselves");
    carol->pos += vec3(3, 0, 0);
    vec3 want = carol->pos;
    check(pump(4, [&] { Entity* g = B.ghost("player", "Carol"); return g && glm::distance(g->pos, want) < 1.0f; }), "the couch player's moves reach the others");
    submitChat(*A.lv, carol, "hi from the couch");
    check(pump(3, [&] { return B.heard("<Carol> hi from the couch"); }), "the couch player's chat goes out under their own name");
    A.nc->removeCouch(*carol); carol->removed = true;
    check(pump(6, [&] { return B.ghost("player", "Carol") == nullptr; }), "and when they leave, they're gone for everyone");
  }
#ifdef DQ_SAMPLE_PLUGIN
  submitChat(*A.lv, A.me, "/ping");
  check(pump(3, [&] { return A.heard("pong"); }), "a native plugin's command answers");
  submitChat(*A.lv, A.me, "a badword here");
  pumpFor(1.0);
  check(!B.heard("badword"), "a native plugin cancelling chat stops the line");
#endif
  submitChat(*B.lv, B.me, "/kick Alice");
  check(pump(3, [&] { return B.heard("Only the host"); }), "server commands need op");

  // ---- entities ----
  submitChat(*A.lv, A.me, "/spawnit cow");
  // the one spawned beside Alice (the server's own herds can be about too, further off)
  auto nearCow = [&]() -> Entity* { Entity* best = nullptr; for (auto& e : B.lv->entities()) if (!e->removed && e->ghost && e->type == "cow" && (!best || glm::distance(e->pos, A.me->pos) < glm::distance(best->pos, A.me->pos))) best = e.get(); return best; };
  check(pump(5, [&] { Entity* c = nearCow(); return c && glm::distance(c->pos, A.me->pos) < 8; }), "a creature the server spawns (DQ.spawn) appears for Bob as a ghost");
  if (Entity* cow = nearCow()) check(glm::distance(cow->pos, B.me->pos) < 20 && cow->hp > 0, strFormat("the ghost is where the server says (%.1f from Bob, hp %.1f; cow %.1f %.1f %.1f, Alice %.1f %.1f %.1f)",
      glm::distance(cow->pos, B.me->pos), cow->hp, cow->pos.x, cow->pos.y, cow->pos.z, A.me->pos.x, A.me->pos.y, A.me->pos.z));
  // an item Alice drops here is made by the server
  {
    auto drop = createEntity("item");
    drop->type = "item"; drop->pos = A.me->pos + vec3(1, 1, 0); drop->data = {{"item", {{"id", "apple"}, {"n", 1}}}};
    A.lv->spawn(std::move(drop));
  }
  check(pump(4, [&] { return B.ghost("item") != nullptr; }), "something Alice makes (a dropped item) is made by the server and seen by Bob");

  // ---- the server's console ----
  srv.command("give Bob dirt 5");
  check(pump(4, [&] { for (auto& s : B.me->inv) if (s.id == "dirt" && s.n >= 5) return true; return false; }), "console give reaches Bob's bags");
  float ty = A.me->pos.y + 3;
  srv.command(strFormat("tp Alice %d %.1f %d", bx + 5, ty, bz + 5));
  check(pump(4, [&] { return std::abs(A.me->pos.x - (bx + 5)) < 0.01f; }), "console tp moves Alice");
  // moving impossibly fast is put back
  pumpFor(3.5);   // (a teleport from the server allows a jump for a moment)
  vec3 before = A.me->pos;
  A.me->pos += vec3(800, 0, 0);
  check(pump(4, [&] { return glm::distance(A.me->pos, before) < 5; }), "a jump across the map is put back (light movement check)");
  srv.command("list");
  pumpFor(0.3);
  bool listed = false; for (auto& l : srv.logSince(0)) if (l.text.find("Playing: ") != std::string::npos && l.text.find("Bob") != std::string::npos) listed = true;
  check(listed, "console list");
  // ---- weather: guests follow the host's (they used to run a cycle of their own) ----
  srv.command("weather thunder 600");
  auto kindOf = [](Client& c) { WeatherSystem* W = c.lv ? weatherOf(*c.lv) : nullptr; return W ? W->stormKind() : std::string("?"); };
  { bool ok = pump(4, [&] { return hostKind == 2 && kindOf(A) == "thunder" && kindOf(B) == "thunder"; });
    check(ok, "guest weather follows the host (Alice: " + kindOf(A) + ", Bob: " + kindOf(B) + ")"); }
  { WeatherSystem* W = weatherOf(*A.lv); check(W && !W->hostRules, "a guest doesn't run its own weather"); }
  pumpFor(3);
  { WeatherSystem* W = weatherOf(*A.lv); float d = W ? std::abs(std::remainder(W->windDir - hostWind.load(), 6.2831853f)) : 9.f;
    check(d < 0.05f, strFormat("a guest's wind is the host's (%.3f rad apart)", d)); }
  auto st = srv.status();
  check(st.running && st.players == 2 && st.tps > 10, strFormat("status: %d players, %.0f ticks/s", st.players, st.tps));
  check(A.nc->stats.records + A.nc->stats.sums > 0, strFormat("chunks came as records/checksums (%llu records, %llu sums in, %llu compared, %zu waiting, %llu asked again)",
        (unsigned long long)A.nc->stats.records, (unsigned long long)A.nc->stats.sumsIn, (unsigned long long)A.nc->stats.sums, A.nc->sumsWaiting(), (unsigned long long)A.nc->stats.needs));
  // (a chunk the host changed before it loaded here is asked for again too: that's "needs", not a mismatch)
  check(A.nc->stats.mismatches == 0, strFormat("chunks made here from the seed match the server's (%llu checksum mismatches)", (unsigned long long)A.nc->stats.mismatches));
  check(A.lv->info.time > 0, "time of day is sent");

  // a mod that never stops is switched off, the game goes on
  {
    ModInfo* hog = srv.modLoader() ? srv.modLoader()->find("hog") : nullptr;
    check(hog && hog->error.find("switched off") != std::string::npos, "a mod stuck in a loop is stopped and switched off: " + (hog ? hog->error : std::string("?")));
  }

  // ---- kick, coming back, the server stopping ----
  // ---- the immersive stations: one a guest builds reaches the host whole, and burns there; a guest's bellows pumps it ----
  {
    World& aw = A.lv->world(A.me->realm);
    const ivec3 at = ivec3(glm::floor(A.me->pos)) + ivec3(3, 0, 0);
    g_st.x = at.x; g_st.y = at.y; g_st.z = at.z; g_st.armed = true;
    aw.setBlock(at.x, at.y, at.z, reg().blockId("forge"));
    *aw.blockEntity(at.x, at.y, at.z, true) = json{{"type", "forge"}, {"fuel", 6}, {"burn", 0.0}, {"temp", 20.0}, {"lit", true}, {"items", json::array({nullptr, nullptr, nullptr, nullptr, nullptr})}};
    aw.blockEntityChanged(at.x, at.y, at.z);   // (the block and its contents in the same frame, as forgeBuild does)
    aw.setBlock(at.x, at.y, at.z + 1, reg().blockId("bellows"));
    check(bellowsPump(aw, at.x, at.y, at.z + 1), "a guest pumps bellows");
    check(pump(5, [&] { return g_st.fuel > 0; }), "a forge a guest built reaches the host with its charcoal (" + std::to_string(g_st.fuel.load()) + ")");
    check(pump(5, [&] { return g_st.listed.load(); }), "and, lit by the guest, it burns on the host");
    check(pump(5, [&] { return g_st.pump > 0; }), "the guest's pumping reaches the host's bellows");
    g_st.armed = false;
  }
#ifndef _WIN32
  // ---- over a bad internet link: the others' movement smooth, and what they're doing seen ----
  {
    BadLink udpBad, tcpBad;
    udpBad.udp(port + 10, port, 0.03, 0.16, 0.06);   // 30-160 ms late (overtaking), 6% lost, both ways
    tcpBad.tcp(port + 11, port, 0.0, 0.15);          // the TCP stream held back in lumps of up to 150 ms
    Client C, D; C.name = "Cara"; D.name = "Dan"; D.tcpOnly = true;
    check(C.join("127.0.0.1:" + std::to_string(port + 10), &pool), "a player joins over a bad UDP link (late, overtaking, lost) " + C.err);
    check(D.join("127.0.0.1:" + std::to_string(port + 11), &pool), "a player joins over TCP through a jittery link " + D.err);
    std::vector<Client*> viewers = {&B};
    if (C.lv) viewers.push_back(&C);
    if (D.lv) viewers.push_back(&D);
    g_clients = {&A, &B};
    if (C.lv) g_clients.push_back(&C);
    if (D.lv) g_clients.push_back(&D);
    check(pump(20, [&] { for (Client* v : viewers) if (!v->ghost("player", "Alice")) return false; return true; }), "everyone sees Alice");
    for (Client* v : viewers) { int n = 0; std::string names; for (auto& e : v->lv->entities()) if (!e->removed && e->ghost) { n++; if (auto* p = dynamic_cast<Player*>(e.get())) names += p->name + " "; }
      printf("  %s: %d replicas (players: %s), %llu messages in (%llu movement), lost %d, me realm %d\n", v->name.c_str(), n, names.c_str(), (unsigned long long)v->nc->stats.msgsIn, (unsigned long long)v->nc->stats.movesIn, (int)v->nc->lost(), v->me ? v->me->realm : -1); }
    // Alice walks along at 4 b/s turning at 2 rad/s (through the angle's wrap); each screen's Alice is followed frame by frame
    const vec3 start = A.me->pos; const double speed = 4, omega = 2;
    std::map<Client*, std::vector<std::array<double, 6>>> seen;
    struct Starve { double until = 0; int seg = 0, frames = 0, total = 0; bool in = false; };
    std::map<Client*, Starve> starve;
    auto walk = [&](double secs, double dir, bool record) {
      const double t0 = nowSeconds();
      const vec3 from = A.me->pos;
      while (nowSeconds() - t0 < secs) {
        const double tt = nowSeconds() - t0;
        A.me->pos = from + vec3((float)(dir * speed * tt), 0, 0); A.me->vel = vec3((float)(dir * speed), 0, 0);
        A.me->yaw = A.me->bodyYaw = (float)std::remainder(omega * tt + 3.0, 2 * PI);
        for (auto* c : g_clients) {
          double& at = c->tickAt;   // (the moment its game drew this frame: its replicas glide to it)
          c->step(1.0 / 30);
          if (record && tt > 1.5 && tt < secs - 0.3 && c != &A)
            if (Entity* g = c->ghost("player", "Alice")) {
              // fed: drawing within what it has heard (or bridging a short gap). A spell with nothing heard — this
              // computer starved by the other games in the test, the server not ticking — is counted, and not judged
              // for smoothness (it can only wait, then catch up): the frames until 0.3 s after it start a new segment
              Starve& sv = starve[c];
              const bool fed = g->netHist.size() >= 2 && g->netLastRt <= g->netHist.back().t + 0.3;
              if (!fed) { sv.until = at + 0.3; sv.frames++; }
              if (at < sv.until) { if (!sv.in) { sv.in = true; sv.seg++; } }
              else { sv.in = false; seen[c].push_back({at, g->pos.x - start.x, g->pos.y - start.y, g->pos.z - start.z, g->yaw, (double)sv.seg}); }
              sv.total++;
            }
          if (std::getenv("DQ_DUMP_WALK") && c == &B) if (Entity* g = c->ghost("player", "Alice")) { static double lastT = 0; if (!g->netHist.empty() && g->netHist.back().t != lastT) { lastT = g->netHist.back().t; printf("hist %.4f newest %.4f x %.3f n %zu delay %.3f corr %.3f\n", nowSeconds(), lastT, g->netHist.back().p.x - start.x, g->netHist.size(), g->netDelay, glm::length(g->netCorr)); } }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
      }
    };
    std::map<Client*, u64> moves0; for (Client* v : viewers) moves0[v] = v->nc->stats.movesIn;
    walk(5.0, 1, true);
    {   // the movement packets that got through (ENet once dropped most of them on a jittery link: net/protocol.cpp steadyPeer)
      const double direct = (double)(B.nc->stats.movesIn - moves0[&B]);
      for (Client* v : viewers) if (v != &B) {
        const double got = (double)(v->nc->stats.movesIn - moves0[v]);
        check(got > direct * 0.75, strFormat("%s gets the movement through the bad link (%.0f packets; %.0f directly)", v->name.c_str(), got, direct));
      }
    }
    const char* how[3] = {"directly", "over the bad UDP link", "over TCP through the jittery link"};
    for (size_t i = 0; i < viewers.size(); i++) {
      const auto& f = seen[viewers[i]];
      if (std::getenv("DQ_DUMP_WALK")) for (auto& r : f) printf("walk%zu %.4f %.4f %.4f %.0f\n", i, r[0], r[1], r[4], r[5]);
      if (f.size() < 20) { check(false, std::string("Alice's walk is seen ") + how[i]); continue; }
      const PathStats s = pathStats(f, speed, omega, 0, f.front()[0] - f.front()[1] / speed);   // (the lag measured from the first frame)
      check(s.back < 0.03 && s.minV > 1.5 && s.maxV < 7 && s.lagSpread < 0.08 && s.offPath < 0.05 && s.maxTurn < 8,
            std::string("Alice's walk is drawn smoothly ") + how[i] + " (" + describe(s) + strFormat("; %d of %d frames with nothing heard for a while: waited out", starve[viewers[i]].frames, starve[viewers[i]].total) + ")");
    }
    // what she does with her hands and body reaches everyone, over the bad links too
    {
      World& aw = A.lv->world(A.me->realm);
      const int mx = ifloor(A.me->pos.x), mz = ifloor(A.me->pos.z);
      const int my = aw.surfaceY(mx, mz, dimOfY(ifloor(A.me->pos.y)));
      A.me->swingN = (u8)(A.me->swingN + 3);
      A.me->mineAt = ivec3(mx, my, mz); A.me->mineP = 0.55f;
      const ItemStack chestWas = A.me->armor[1]; A.me->armor[1] = ItemStack("elytra", 1);   // (wings on: gliding needs them)
      A.me->bowPull = 0.7f; A.me->scoping = true; A.me->sneaking = true; A.me->gliding = true;
      A.me->hang.emplace(); A.me->hang->shimT = 1;
      A.me->carrying = {{"kind", "block"}, {"key", "stone"}, {"meta", 0}};
      for (Client* v : viewers) {
        auto ga = [&] { Entity* e = v->ghost("player", "Alice"); return e ? dynamic_cast<Player*>(e) : nullptr; };
        const bool ok = pump(6, [&] {
          Player* g = ga();
          return g && g->swingN == A.me->swingN && g->mineP > 0.5f && g->mineAt == A.me->mineAt && g->bowPull > 0.65f && g->scoping &&
                 g->sneaking && g->gliding && g->hang && g->hang->shimT > 0 && g->netCarry.is_object() && g->netCarry.value("key", "") == "stone";
        });
        Player* g = ga();
        check(ok, strFormat("Alice's swing, digging (55%%), drawn bow, spyglass, sneak, glide, ledge shimmy and the block in her arms reach a viewer %s (swings %d/%d, dig %.2f, bow %.2f, scope %d sneak %d glide %d hang %d carry %d; hers: sneak %d glide %d hang %d)",
                            how[v == &B ? 0 : v == &C ? 1 : 2], g ? g->swingN : -1, A.me->swingN, g ? g->mineP : -1.f, g ? g->bowPull : -1.f, g && g->scoping, g && g->sneaking, g && g->gliding, g && g->hang.has_value(), g && g->netCarry.is_object(), (int)A.me->sneaking, (int)A.me->gliding, (int)A.me->hang.has_value()));
        // her cracks on their screen (the block she digs, by how far)
        World& vw = v->lv->world(A.me->realm);
        if (vw.getBlock(mx, my, mz) != AIR) {
          int stage = -1; for (auto& c : damageCracks(*v->lv, vw, A.me->pos, nullptr)) if (c.x == mx && c.y == my && c.z == mz) stage = c.stage;
          check(stage == 5, strFormat("and her cracks show on the block she digs, %s (stage %d)", how[v == &B ? 0 : v == &C ? 1 : 2], stage));
        }
      }
      A.me->mineP = 0; A.me->bowPull = 0; A.me->scoping = A.me->sneaking = A.me->gliding = false; A.me->hang.reset(); A.me->carrying = nullptr; A.me->armor[1] = chestWas;
      check(pump(6, [&] { for (Client* v : viewers) { Entity* e = v->ghost("player", "Alice"); auto* g = e ? dynamic_cast<Player*>(e) : nullptr;
                            if (!g || g->mineP > 0 || g->bowPull > 0 || g->scoping || g->gliding || g->hang || g->netCarry.is_object()) return false; } return true; }),
            "...and when she stops, it stops for everyone");
    }
    walk(5.0, -1, false);   // (back where she was, for what follows)
    C.leave(); D.leave();
    g_clients = {&A, &B};
    pumpFor(0.5);
  }
#endif

  srv.command("kick Bob Testing kicks");
  check(pump(4, [&] { return B.nc && B.nc->lost(); }), "kick: Bob's game is let go");
  check(B.nc && B.nc->lostReason.find("Testing kicks") != std::string::npos, "with the reason: " + (B.nc ? B.nc->lostReason : ""));
  B.leave();
  g_clients = {&A};
  check(pump(4, [&] { return A.heard("Bob left the game"); }), "Alice hears that Bob left");
  check(pump(4, [&] { return !A.ghost("player", "Bob"); }), "and his ghost goes");
  Client B2; B2.name = "Bob";
  check(B2.join(addr, &pool), "Bob comes back " + B2.err);
  g_clients = {&A, &B2};
  bool kept = false; if (B2.me) for (auto& s : B2.me->inv) if (s.id == "dirt" && s.n >= 5) kept = true;
  check(kept, "with what he carried (the server saved his player)");
  { bool ok = pump(3, [&] { return kindOf(B2) == "thunder"; }); check(ok, "late joiner sees the storm (" + kindOf(B2) + ")"); }
  pumpFor(0.5);
  srv.stop();
  check(pump(4, [&] { return A.nc && A.nc->lost(); }) && A.nc->lostReason.find("closed") != std::string::npos, "the server stopping lets everyone go: " + (A.nc ? A.nc->lostReason : ""));
  A.leave(); B2.leave();
  g_clients.clear();

  // ---- a dedicated server's new world: a newcomer starts on dry land (it put them on the sea floor: a black screen) ----
  {
    EventBus sb;
    LevelInfo li; li.name = "SpawnWorld"; li.seedText = "hello";
    auto lv = Level::create(userSub("saves"), li, &pool, &sb);
    net::HostOptions ho; ho.name = "SpawnWorld"; ho.port = port + 3; ho.dedicated = true; ho.lan = false; ho.upnp = false;
    net::ServerHost sh(*lv, ho);
    const vec3 sp = sh.spawnFor("Newcomer");
    ColumnSample s; if (DimGenerator* g = lv->gen->forDim(DIM_OVERWORLD)) g->sample(ifloor(sp.x), ifloor(sp.z), s);
    check(s.water < 0 && s.height > 0, "a dedicated server's newcomer starts on dry land, not under the sea (" + std::to_string((int)sp.x) + ", " + std::to_string((int)sp.z) + ")");
    // a world an older server saved with its start on the sea floor: chosen again
    lv->info.spawn = vec3(0.5f, dimBase(DIM_OVERWORLD) + 20, 0.5f);
    const vec3 sp2 = sh.spawnFor("Another");
    ColumnSample s2; if (DimGenerator* g = lv->gen->forDim(DIM_OVERWORLD)) g->sample(ifloor(sp2.x), ifloor(sp2.z), s2);
    check(s2.water < 0 && s2.height > 0, "a start an older server left on the sea floor is chosen again on dry land");
    lv->close();
  }

  // ---- "Open to LAN": a single-player world served by the game itself (the host plays in the same level) ----
  {
    EventBus hostBus;
    RecFx hostFx;
    LevelInfo li; li.name = "LanWorld"; li.seedText = "lan party";
    auto lv = Level::create(userSub("saves"), li, &pool, &hostBus);
    lv->fx = &hostFx;
    auto hp = std::make_unique<Player>();
    hp->name = "Hostess";
    DimGenerator* g = lv->gen->forDim(DIM_OVERWORLD);
    hp->pos = vec3(0.5f, dimBase(DIM_OVERWORLD) + (g ? g->spawnHeight(0, 0) : 100) + 2, 0.5f);
    Player* host = static_cast<Player*>(lv->spawn(std::move(hp)));
    net::HostOptions ho; ho.name = "LanWorld"; ho.hostName = "Hostess"; ho.port = port + 1; ho.dedicated = false; ho.lan = false; ho.upnp = false;
    auto sh = std::make_shared<net::ServerHost>(*lv, ho);
    std::string e2;
    check(sh->start(&e2), "open to LAN: the game serves its own world " + e2);
    lv->net = sh;
    auto hostStep = [&] {
      if (lv->net) lv->net->update(*lv, 1.0 / 30);
      lv->tick(1.0 / 30);
      std::vector<StreamAnchor> an = {{host->pos, 3, false}};
      if (lv->net) lv->net->streamAnchors(host->realm, an);
      lv->world().stream(an, 4);
    };
    for (int i = 0; i < 60; i++) { hostStep(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    Client C; C.name = "Carol";
    // joining blocks this thread, so the host is stepped from a helper meanwhile
    std::atomic<bool> joining{true};
    std::thread hostThread([&] { while (joining) { hostStep(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } });
    bool joined = C.join("127.0.0.1:" + std::to_string(port + 1), &pool);
    joining = false; hostThread.join();
    check(joined, "a friend joins the LAN world " + C.err);
    g_clients = {&C};
    auto both = [&](double secs, auto done) { double t0 = nowSeconds(); while (nowSeconds() - t0 < secs) { hostStep(); C.step(1.0 / 30); if (done()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(15)); } return done(); };
    check(both(5, [&] { return C.ghost("player", "Hostess") != nullptr; }), "the friend sees the host's own player");
    bool heardJoin = false;
    check(both(5, [&] { for (auto& m : hostFx.msgs) if (m.find("Carol joined the game") != std::string::npos) heardJoin = true; return heardJoin; }), "the host is told who joined");
    int hx = ifloor(host->pos.x) + 1, hz = ifloor(host->pos.z) + 2;
    both(8, [&] { return C.lv->world().isLoaded(hx, (int)host->pos.y, hz) && lv->world().isLoaded(hx, (int)host->pos.y, hz); });
    int hy = (int)host->pos.y + 3;
    lv->world().setBlock(hx, hy, hz, reg().blockId("glass") ? reg().blockId("glass") : stone, 0, SetFlags::Urgent);
    check(both(4, [&] { return C.lv->world().getBlock(hx, hy, hz) == lv->world().getBlock(hx, hy, hz) && lv->world().getBlock(hx, hy, hz) != AIR; }), "the host's block change reaches the friend");
    C.lv->world().setBlock(hx, hy + 1, hz, stone, 0, SetFlags::Urgent);
    check(both(4, [&] { return lv->world().getBlock(hx, hy + 1, hz) == stone; }), "the friend's block change reaches the host's world");
    submitChat(*lv, host, "hi Carol");
    check(both(3, [&] { return C.heard("<Hostess> hi Carol"); }), "the host's chat reaches the friend");
    host->give(ItemStack("apple", 2));   // not the friend's bags: nothing should change for Carol
    Player* mirror = sh->player("Carol");
    check(mirror && mirror->ghost, "the friend is a mirror player in the host's level");
    if (mirror) { mirror->hp -= 3; }   // the host's world hurts them (a creature, a fall...)
    float hpBefore = C.me->hp;
    check(both(3, [&] { return C.me->hp <= hpBefore - 2.9f; }), "damage the host's world does to a friend reaches their game");
    // the open journal: what's on its pages reaches the others (ui/book_ui.cpp; the host relays it in the snapshot)
    C.me->activity = Player::ACT_SCREEN; C.me->bookPic = "dGVzdA=="; C.me->bookPicV++;
    check(both(3, [&] { return mirror && mirror->bookPic == "dGVzdA=="; }), "a friend's open book: its pages reach the host");
    if (Player* hp = host) {
      hp->activity = Player::ACT_SCREEN; hp->bookPic = "aG9zdA=="; hp->bookPicV++;
      check(both(3, [&] { Entity* e = C.lv->entity(host->id); auto* g = e ? dynamic_cast<Player*>(e) : nullptr; return g && g->bookPic == "aG9zdA=="; }), "the host's open book: its pages reach the friend");
      hp->activity = 0;
    }
    C.me->activity = 0;
    // working a station: what and where reach the others, a set-down counted
    C.me->stationKind = 4; C.me->workAt = C.me->pos + vec3(0.5f, 0.6f, -1.f); C.me->workTap = 3;
    check(both(3, [&] { return mirror && mirror->stationKind == 4 && mirror->workTap == 3 && glm::distance(mirror->workAt, C.me->workAt) < 0.01f; }), "a friend at a furnace: the host sees where their hand works");
    C.me->stationKind = -1;
    check(both(3, [&] { return mirror && mirror->stationKind == -1; }), "  ...and sees them leave it");
    // the host draws its guest between the places their game said, at the times it said them (sim/net_interp.cpp; the game
    // swaps the view in to draw: client/session.cpp showReplicas), while the body keeps its newest place for the rules
    if (mirror) {
      const vec3 from = C.me->pos; const double t0 = nowSeconds();
      std::vector<std::array<double, 6>> f;
      both(3.5, [&] {
        const double tt = nowSeconds() - t0;
        C.me->pos = from + vec3((float)(4 * tt), 0, 0); C.me->yaw = C.me->bodyYaw = (float)std::remainder(2 * tt, 2 * PI);
        mirror->netUpdateView(nowSeconds());
        if (tt > 1.0 && mirror->hasView) f.push_back({nowSeconds(), mirror->viewPos.x - from.x, mirror->viewPos.y - from.y, mirror->viewPos.z - from.z, mirror->viewYaw, 0});
        return false;
      });
      const PathStats s = pathStats(f, 4, 2, 0, f.empty() ? 0 : f.front()[0] - f.front()[1] / 4);
      check(f.size() > 20 && s.back < 0.03 && s.minV > 1.5 && s.maxV < 7 && s.lagSpread < 0.08 && s.offPath < 0.05 && s.maxTurn < 8,
            "the host draws a walking guest smoothly (" + describe(s) + ")");
      check(glm::distance(mirror->pos, C.me->pos) < 1.0f, "...while their body for the rules is where they newly are");
      // and their doings, for the host's screen and passed on
      C.me->swingN = 9; C.me->mineAt = ivec3(C.me->pos) - ivec3(0, 1, 0); C.me->mineP = 0.3f; C.me->bowPull = 0.4f;
      C.me->carrying = {{"kind", "mob"}, {"type", "chicken"}, {"hp", 4}};
      check(both(3, [&] { return mirror->swingN == 9 && mirror->mineP > 0.25f && mirror->bowPull > 0.35f && mirror->netCarry.value("type", "") == "chicken" && mirror->carrying.is_null(); }),
            "a guest's swing, digging, bow and what they carry reach the host (drawn, not taken as the host's own carrying)");
      Entity* hg = C.lv->entity(host->id); auto* hgp = hg ? dynamic_cast<Player*>(hg) : nullptr;
      host->swingN = 4; host->bowPull = 0.9f;
      check(both(3, [&] { return hgp && hgp->swingN == 4 && hgp->bowPull > 0.85f && hgp->fxSent; }), "the host's own swing and bow reach the guest (its sounds marked as the host's)");
      C.me->mineP = 0; C.me->bowPull = 0; C.me->carrying = nullptr; host->bowPull = 0;
      {   // (walked back where they were, for what follows)
        const vec3 there = C.me->pos; const double t1 = nowSeconds(), back = glm::distance(there, from) / 4;
        both(back + 0.05, [&] { const float k = (float)std::min(1.0, (nowSeconds() - t1) / back); C.me->pos = glm::mix(there, from, k); return false; });
        C.me->pos = from; C.me->vel = vec3(0);
        both(1.0, [] { return false; });
      }
    }
    // ---- creatures between host and friend: how they look, using them, riding (net/server_sync.cpp, server_host.cpp) ----
    {
      vec3 at = C.me->pos + vec3(2.5f, 0, 0);
      Creature* pig = spawnCreature(*lv, 0, "pig", at);
      Creature* slime = spawnCreature(*lv, 0, "slime", at + vec3(0, 0, 3));
      if (pig) { pig->saddled = true; pig->kept = true; }
      if (slime) slime->setSize(3);
      auto ghostOf = [&](Creature* c) -> Creature* { Entity* e = c ? C.lv->entity(c->id) : nullptr; return e && e->ghost ? dynamic_cast<Creature*>(e) : nullptr; };
      check(both(5, [&] { Creature* g = ghostOf(pig); return g && g->saddled; }), "a creature's state reaches the friend (a saddled pig)");
      check(both(5, [&] { Creature* g = ghostOf(slime); return g && g->size == 3; }), "and its size (a big slime)");
      // a pecking chicken: its peck timer and a mount's pack once shared a key ("pk"), so a joined player's chicken had a
      // number for its "pack" and their game crashed drawing it
      if (Creature* ch = spawnCreature(*lv, 0, "chicken", at + vec3(0, 0, -3))) {
        ch->peckT = 0.3f;
        bool ok = false;
        both(5, [&] { Creature* g = ghostOf(ch); if (!g) return false; ch->peckT = 0.3f; ok = !g->data.contains("pack") || g->data["pack"].is_string(); return g->peckT > 0 || true; });
        Creature* g = ghostOf(ch);
        check(g && (!g->data.contains("pack") || g->data["pack"].is_string()), "a pecking chicken reaches the friend without a number for a pack (the LAN join crash)");
      }
      // the friend uses the pig with an empty hand: the host's game seats their mirror, and their game hears of it
      Creature* gp = ghostOf(pig);
      if (gp && mirror) {
        C.me->inv[C.me->selected] = ItemStack();
        C.lv->net->action(*C.me, "useEnt", json{{"id", gp->id}, {"sel", C.me->selected}, {"sn", 0}});
        bool on = both(5, [&] { return C.me->riding == gp->id && pig->riddenBy == mirror->id; });
        check(on, "a friend gets on a saddled pig (asked of the host, seated there and here)");
        vec3 p0 = pig->pos;
        both(4, [&] { rideControl(*C.lv, *C.me, 1, 0, false, false, false); return glm::distance(pig->pos, p0) > 2.5f; });
        check(glm::distance(pig->pos, p0) > 2.5f, strFormat("and rides it: the host's pig goes where the friend steers (%.1f blocks)", glm::distance(pig->pos, p0)));
        rideControl(*C.lv, *C.me, 0, 0, false, false, false);   // (a frame of riding: our seat follows the pig as it is here)
        check(glm::distance(C.me->pos, gp->pos) < 2.5f, strFormat("the friend sits on the pig in their own game (%.1f from it; riding %u, its rider %u, me %u)", glm::distance(C.me->pos, gp->pos), C.me->riding, gp->riddenBy, C.me->id));
        rideControl(*C.lv, *C.me, 0, 0, false, false, true);   // sneak: off
        check(both(4, [&] { return pig->riddenBy == 0 && C.me->riding == 0; }), "and gets off again");
      }
      // a screen the host's game opens for the friend opens on the friend's game
      int opened = 0;
      int h = C.lv->events->on("screenOpen", [&](const json& a) { if (a.is_array() && a.size() > 1 && a[1] == "table") opened++; return true; });
      if (mirror) openScreen(*lv, *mirror, "table");
      check(both(3, [&] { return opened > 0; }), "a screen the host's world opens for a friend opens on the friend's game");
      C.lv->events->off(h);
    }
    sh->stop("The host closed the world.");
    lv->net.reset();
    check(both(3, [&] { return C.nc && C.nc->lost(); }), "closing LAN lets the friend go");
    C.leave();
    g_clients.clear();
    lv->close();
  }
  printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
