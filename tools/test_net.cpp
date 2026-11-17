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
static struct { std::atomic<int> stage{0}; int x = 0, y = 0, z = 0; std::atomic<bool> started{false}; } g_mv;
struct Client {
  std::string name;
  EventBus bus;
  std::unique_ptr<Level> lv;
  std::shared_ptr<net::NetClient> nc;
  Player* me = nullptr;
  RecFx fx;
  std::string err;
  bool join(const std::string& addr, ThreadPool* pool) {
    nc = std::make_shared<net::NetClient>();
    net::JoinOptions o; o.address = addr; o.name = name; o.viewDistance = 3; o.timeout = 15;
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
    lv->tick(dt);
    lv->world(me->realm).stream({{me->pos, 3, false}}, 4);
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

// a replica's path under uneven delivery: the host walks a creature at 2 blocks a second and sends where it is ten
// times a second; each packet arrives 0-80 ms late. The guest's view should move at a steady speed (sim/entity.h glide)
static double replicaSpeedSpread(bool timed) {
  using dq::g_netViewTime;
  Entity e; e.ghost = true;
  u32 seed = 12345; auto rnd01 = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
  struct Pkt { double arrive, st; vec3 p; };
  std::vector<Pkt> pkts;
  for (double st = 0; st < 4.0; st += 0.1) pkts.push_back({st + 0.02 + rnd01() * 0.08 + 100.0, st, vec3((float)(st * 2.0), 0, 0)});
  double off = 0; bool known = false; size_t next = 0;
  std::vector<float> xs;
  for (double now = 100.0; now < 104.0; now += 0.05) {
    while (next < pkts.size() && pkts[next].arrive <= now) {
      const Pkt& k = pkts[next++];
      const double o = k.st - k.arrive; if (!known || o > off) off = o; known = true;
      e.netPos = k.p; e.netYaw = 0;
      if (timed) e.netSample(k.st, k.p, 0);
    }
    g_netViewTime = timed && known ? now + off - 0.15 : -1;
    e.glide(0.05);
    xs.push_back(e.pos.x);
  }
  g_netViewTime = -1;
  float lo = 1e9f, hi = -1e9f;
  for (size_t i = 20; i + 1 < xs.size() - 10; i++) { const float v = (xs[i + 1] - xs[i]) / 0.05f; lo = std::min(lo, v); hi = std::max(hi, v); }
  return hi - lo;   // the spread of speeds seen (blocks a second): 0 is perfectly steady
}

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

  ServerConfig cfg; cfg.world = "NetTest"; cfg.seed = "multiplayer"; cfg.port = port; cfg.motd = "Hello from the test server"; cfg.viewDistance = 4;
  DedicatedServer srv(cfg);
  // the host's weather, read on the server thread (weather sync checks below)
  static std::atomic<int> hostKind{0}; static std::atomic<float> hostWind{0};
  srv.onTick = [](Level& lv) {
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
  {   // replicas drawn steadily between the host's positions (net/client.cpp clock, sim/entity.h glide)
    const double timed = replicaSpeedSpread(true), eased = replicaSpeedSpread(false);
    check(timed < 0.5, "a creature walking at 2 b/s is seen at a steady speed despite late packets (speeds spread " + std::to_string(timed) + " b/s; easing alone: " + std::to_string(eased) + ")");
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
    net::HostOptions ho; ho.name = "SpawnWorld"; ho.port = port + 3; ho.dedicated = true; ho.lan = false;
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
    net::HostOptions ho; ho.name = "LanWorld"; ho.hostName = "Hostess"; ho.port = port + 1; ho.dedicated = false; ho.lan = false;
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
