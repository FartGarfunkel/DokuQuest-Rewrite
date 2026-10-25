// server/server.cpp — the dedicated server: the world on its own thread, the network, plugins and the console
// (game/server.js serverStart / serverStop / serverCommand / serverSay).
#include "server/server.h"
#include "net/server_host.h"
#include "mod/modloader.h"
#include "sim/commands.h"

namespace dq {

json ServerConfig::toJson() const {
  return {{"world", world}, {"seed", seed}, {"mode", mode}, {"difficulty", difficulty}, {"name", name}, {"port", port}, {"maxPlayers", maxPlayers},
          {"motd", motd}, {"pvp", pvp}, {"lan", lan}, {"plugins", plugins}, {"password", password}, {"viewDistance", viewDistance}};
}
void ServerConfig::fromJson(const json& j) {
  world = j.value("world", world); seed = j.value("seed", seed); mode = j.value("mode", mode); difficulty = j.value("difficulty", difficulty);
  name = j.value("name", name); port = j.value("port", port); maxPlayers = clampv(j.value("maxPlayers", maxPlayers), 1, 64); motd = j.value("motd", motd);
  pvp = j.value("pvp", pvp); lan = j.value("lan", lan); plugins = j.value("plugins", plugins); password = j.value("password", password);
  viewDistance = clampv(j.value("viewDistance", viewDistance), 2, 32);
}

DedicatedServer::DedicatedServer(ServerConfig c) : cfg(std::move(c)) {}
DedicatedServer::~DedicatedServer() { stop(); }

void DedicatedServer::log(const std::string& text, const std::string& kind) {
  std::lock_guard<std::mutex> lk(mx);
  logLines.push_back({++logSeq, unixMillis(), text, kind});
  while (logLines.size() > 400) logLines.pop_front();
}
std::vector<ServerLogLine> DedicatedServer::logSince(u64 seq) const {
  std::lock_guard<std::mutex> lk(mx);
  std::vector<ServerLogLine> v;
  for (auto& l : logLines) if (l.seq > seq) v.push_back(l);
  return v;
}

bool DedicatedServer::start(std::string* err) {
  if (runFlag) return true;
  pool = std::make_unique<ThreadPool>(0);
  events = std::make_unique<EventBus>();
  fs::path dir = userSub("servers") / cfg.world;
  level = Level::open(dir, pool.get(), events.get());
  if (!level) {
    LevelInfo info; info.name = cfg.world; info.seedText = cfg.seed; info.mode = cfg.mode; info.difficulty = cfg.difficulty;
    level = Level::create(userSub("servers"), info, pool.get(), events.get());
  }
  if (!level) { if (err) *err = "The world could not be opened"; DQ_ERR("server: could not open world %s", cfg.world.c_str()); return false; }
  if (cfg.plugins) {
    mods = std::make_unique<ModLoader>();
    mods->scan();
    mods->loadData();
    mods->onWorldOpen(*level, *events, true);
  }
  net::HostOptions o;
  o.name = cfg.name; o.hostName = "Server"; o.port = cfg.port; o.maxPlayers = cfg.maxPlayers; o.motd = cfg.motd; o.password = cfg.password;
  o.lan = cfg.lan; o.viewDistance = cfg.viewDistance; o.pvp = cfg.pvp; o.dedicated = true;
  host = std::make_shared<net::ServerHost>(*level, o);
  host->onLog = [this](const std::string& t, const std::string& k) { log(t, k); };
  std::string e;
  if (!host->start(&e)) {
    if (err) *err = e;
    DQ_ERR("server: %s", e.c_str());
    if (mods) mods->onWorldClose();
    host.reset(); level->close(); level.reset(); mods.reset(); events.reset(); pool.reset();
    return false;
  }
  level->net = host;
  startedAt = nowSeconds();
  runFlag = true;
  events->emit("worldOpen", json::array({level->info.name}));
  events->emit("serverStart", json::array({cfg.toJson()}));
  th = std::thread([this] { loop(); });
  return true;
}

void DedicatedServer::stop() {
  if (!runFlag && !th.joinable()) return;
  runFlag = false;
  if (th.joinable()) th.join();
  if (level) {
    if (host) host->say("The server is stopping. Thanks for playing!");
    events->emit("serverStop", json::array());
    if (host) { host->update(*level, 0); host->stop("The host closed the world."); }
    level->net.reset();
    if (mods) mods->onWorldClose();
    level->close(); level.reset();
  }
  host.reset();
  mods.reset();
  events.reset();
  pool.reset();
  std::lock_guard<std::mutex> lk(mx); st.running = false;
}

void DedicatedServer::command(const std::string& line) { std::lock_guard<std::mutex> lk(mx); cmds.push_back(line); }

ServerStatus DedicatedServer::status() const { std::lock_guard<std::mutex> lk(mx); return st; }

static std::vector<std::string> words(const std::string& s) {
  std::vector<std::string> v; std::string c;
  for (char ch : s) { if (isspace((unsigned char)ch)) { if (!c.empty()) v.push_back(c), c.clear(); } else c += ch; }
  if (!c.empty()) v.push_back(c);
  return v;
}

// The console (the Chat & console tab): server commands, then any chat command, else it's said to everyone.
void DedicatedServer::console(const std::string& line0) {
  std::string line = line0;
  while (!line.empty() && isspace((unsigned char)line.back())) line.pop_back();
  if (line.empty()) return;
  log("> " + line, "");
  bool slash = line[0] == '/';
  auto a = words(slash ? line.substr(1) : line);
  if (a.empty()) return;
  std::string c = toLower(a[0]);
  auto rest = [&](size_t from) { std::string o; for (size_t i = from; i < a.size(); i++) o += (i > from ? " " : "") + a[i]; return o; };
  if (c == "stop") { runFlag = false; return; }
  if (c == "save") { host->saveAll(); level->save(true); host->serverLog("Saved", "good"); return; }
  if (c == "say") { host->say(rest(1)); return; }
  if (c == "help") {
    std::string cs = "Console: stop, save, say <text>, list, kick <name>, ban <name>, pardon <name>, op <name>, deop <name>, tp <name> <x y z|name|spawn>, give <name> <item> [n], msg <name> <text>, motd [text], maxplayers <n>, plugins. Also:";
    for (auto& cmd : allChatCommands()) cs += " /" + cmd.name;
    host->serverLog(cs); return;
  }
  if (c == "tp" && a.size() >= 3) {
    vec3 to; int realm = -1;
    if (a.size() >= 5) to = vec3(std::stof(a[2]), std::stof(a[3]), std::stof(a[4]));
    else if (toLower(a[2]) == "spawn") to = level->info.spawn;
    else if (Player* o = host->player(a[2])) { to = o->pos; realm = o->realm; }
    else { host->serverLog("Nobody called " + a[2], "warn"); return; }
    if (host->teleport(a[1], to, realm)) { host->tell(a[1], "The server sent you somewhere", ""); host->serverLog(strFormat("Sent %s to %.0f, %.0f, %.0f", a[1].c_str(), to.x, to.y, to.z)); }
    else host->serverLog("Nobody called " + a[1], "warn");
    return;
  }
  if (c == "give" && a.size() >= 3) {
    Player* p = host->player(a[1]);
    if (!p) { host->serverLog("Nobody called " + a[1], "warn"); return; }
    if (!reg().item(a[2])) { host->serverLog("No such item: " + a[2], "warn"); return; }
    int n = a.size() >= 4 ? std::max(1, std::atoi(a[3].c_str())) : 1;
    p->give(ItemStack(a[2], n));
    host->serverLog(strFormat("Gave %d %s to %s", n, a[2].c_str(), p->name.c_str()));
    return;
  }
  if (c == "motd") { if (a.size() > 1) host->opts.motd = rest(1); host->serverLog("Welcome message: " + host->opts.motd); return; }
  if (c == "maxplayers" && a.size() > 1) { host->opts.maxPlayers = clampv(std::atoi(a[1].c_str()), 1, 64); host->serverLog(strFormat("Players at once: %d", host->opts.maxPlayers)); return; }
  if (c == "plugins" || c == "mods") {
    if (!mods) { host->serverLog("Plugins are off for this server"); return; }
    for (auto& m : mods->list()) host->serverLog(m.name + " (" + m.kind + (m.plugin ? ", plugin" : "") + ")" + (m.enabled ? "" : " off") + (m.error.empty() ? "" : " — " + m.error));
    if (mods->list().empty()) host->serverLog("No mods or plugins in " + userSub("mods").string());
    return;
  }
  if (slash || findChatCommand(c)) {
    CommandSource src; src.level = level.get(); src.player = nullptr; src.name = "Server"; src.op = true;
    std::string out = runCommand(*level, src, line);
    if (!out.empty()) host->serverLog(out);
    return;
  }
  host->say(line);   // not a command: said to everyone, like the original's console box
}

void DedicatedServer::loop() {
  double acc = 0, last = nowSeconds(), tpsT = last; int ticks = 0; double busy = 0;
  while (runFlag) {
    std::deque<std::string> todo;
    { std::lock_guard<std::mutex> lk(mx); todo.swap(cmds); }
    for (auto& c : todo) {
      try { console(c); } catch (const std::exception& e) { host->serverLog(std::string("That went wrong: ") + e.what(), "warn"); }
      if (!runFlag) break;
    }
    double now = nowSeconds(); acc += now - last; last = now;
    int steps = 0;
    while (acc >= TICK_DT && steps < 5 && runFlag) {
      double t0 = nowSeconds();
      host->update(*level, TICK_DT);
      if (onTick) onTick(*level);
      // keep spawn loaded so the world is ready for joining players, and everyone's surroundings
      vec3 sp = level->info.spawn.y >= 0 ? level->info.spawn : vec3(0, dimBase(DIM_OVERWORLD) + 80, 0);
      std::map<int, std::vector<StreamAnchor>> anchors;
      anchors[REALM_MAIN].push_back({sp, 3, false});
      for (auto& [r, w] : level->allRealms()) host->streamAnchors(r, anchors[r]);
      for (int r : {REALM_POCKET, REALM_HIS}) { std::vector<StreamAnchor> v; host->streamAnchors(r, v); if (!v.empty()) anchors[r] = v; }
      for (auto& [r, v] : anchors) if (!v.empty() || level->worldIfOpen(r)) level->world(r).stream(v, 4);
      level->tick(TICK_DT);
      busy += nowSeconds() - t0;
      acc -= TICK_DT; steps++; ticks++;
    }
    if (steps == 5) acc = 0;
    if (now - tpsT >= 1) {
      auto ps = host->players();
      std::lock_guard<std::mutex> lk(mx);
      st.running = true; st.tps = ticks / (now - tpsT); st.mspt = ticks ? busy * 1000 / ticks : 0; st.world = cfg.world; st.name = cfg.name;
      st.port = host->port(); st.maxPlayers = host->opts.maxPlayers; st.day = level->info.day; st.uptime = now - startedAt;
      st.players = (int)ps.size(); st.playerNames.clear(); st.playerList.clear();
      for (auto& p : ps) { st.playerNames.push_back(p.name); st.playerList.push_back({p.name, p.address, p.realm, p.pos, p.hp, p.op, p.rttMs}); }
      ticks = 0; busy = 0; tpsT = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  std::lock_guard<std::mutex> lk(mx); st.running = false;
}

} // namespace dq
