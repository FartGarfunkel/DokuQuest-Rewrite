// tools/test_net.cpp — headless multiplayer + mods test: a dedicated server (with JS mods, a data/JS folder mod
// and a native plugin) and two scripted clients on this computer. Checks joining, LAN discovery, block and
// block-entity sync, entities as ghosts, chat, mod and plugin commands/events, server console commands
// (give, tp, kick), movement checks, saved players and the server going away.
//   test_net [--userdir DIR] [--port N]       exit code 0 = everything passed
#include "core/init.h"
#include "server/server.h"
#include "net/client.h"
#include "net/server_host.h"
#include "net/lan.h"
#include "mod/modloader.h"
#include "sim/commands.h"
#include "gen/generator.h"
#include <atomic>
#include <thread>
#include <cstdio>

using namespace dq;

static int fails = 0, passes = 0;
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
  std::string err;
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
    net::JoinOptions o; o.address = addr; o.name = "no/slashes"; o.timeout = 5;
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
  check(A.me && A.me->id >= net::LOCAL_ID_BASE, "the local player has a client-side id");

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

  // ---- chat & commands ----
  submitChat(*A.lv, A.me, "hello there");
  check(pump(3, [&] { return B.heard("<Alice> hello there"); }), "chat reaches the other player");
  submitChat(*A.lv, A.me, "/roll");
  check(pump(3, [&] { return A.heard("Alice rolls a"); }), "a mod's command (DQ.command) answers in chat");
  submitChat(*B.lv, B.me, "/who");
  check(pump(3, [&] { return B.heard("Alice,Bob"); }), "DQ.players() lists everyone");
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
  check(pump(5, [&] { return B.ghost("cow") != nullptr; }), "a creature the server spawns (DQ.spawn) appears for Bob as a ghost");
  if (Entity* cow = B.ghost("cow")) check(glm::distance(cow->pos, B.me->pos) < 20 && cow->hp > 0, "the ghost is where the server says");
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
  auto st = srv.status();
  check(st.running && st.players == 2 && st.tps > 10, strFormat("status: %d players, %.0f ticks/s", st.players, st.tps));
  check(A.nc->stats.records + A.nc->stats.sums > 0, strFormat("chunks came as records/checksums (%llu records, %llu sums in, %llu compared, %zu waiting, %llu asked again)",
        (unsigned long long)A.nc->stats.records, (unsigned long long)A.nc->stats.sumsIn, (unsigned long long)A.nc->stats.sums, A.nc->sumsWaiting(), (unsigned long long)A.nc->stats.needs));
  check(A.nc->stats.needs == 0, "chunks made here from the seed match the server's (no checksum mismatches)");
  check(A.lv->info.time > 0, "time of day is sent");

  // a mod that never stops is switched off, the game goes on
  {
    ModInfo* hog = srv.modLoader() ? srv.modLoader()->find("hog") : nullptr;
    check(hog && hog->error.find("switched off") != std::string::npos, "a mod stuck in a loop is stopped and switched off: " + (hog ? hog->error : std::string("?")));
  }

  // ---- kick, coming back, the server stopping ----
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
  pumpFor(0.5);
  srv.stop();
  check(pump(4, [&] { return A.nc && A.nc->lost(); }) && A.nc->lostReason.find("closed") != std::string::npos, "the server stopping lets everyone go: " + (A.nc ? A.nc->lostReason : ""));
  A.leave(); B2.leave();
  g_clients.clear();

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
