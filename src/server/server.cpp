#include "server/server.h"

namespace dq {

json ServerConfig::toJson() const {
  return {{"world", world}, {"seed", seed}, {"mode", mode}, {"difficulty", difficulty}, {"port", port}, {"maxPlayers", maxPlayers},
          {"motd", motd}, {"pvp", pvp}, {"lan", lan}, {"password", password}, {"viewDistance", viewDistance}};
}
void ServerConfig::fromJson(const json& j) {
  world = j.value("world", world); seed = j.value("seed", seed); mode = j.value("mode", mode); difficulty = j.value("difficulty", difficulty);
  port = j.value("port", port); maxPlayers = j.value("maxPlayers", maxPlayers); motd = j.value("motd", motd); pvp = j.value("pvp", pvp);
  lan = j.value("lan", lan); password = j.value("password", password); viewDistance = j.value("viewDistance", viewDistance);
}

DedicatedServer::DedicatedServer(ServerConfig c) : cfg(std::move(c)) {}
DedicatedServer::~DedicatedServer() { stop(); }

bool DedicatedServer::start() {
  if (runFlag) return true;
  pool = std::make_unique<ThreadPool>(0);
  events = std::make_unique<EventBus>();
  fs::path dir = userSub("servers") / cfg.world;
  level = Level::open(dir, pool.get(), events.get());
  if (!level) {
    LevelInfo info; info.name = cfg.world; info.seedText = cfg.seed; info.mode = cfg.mode; info.difficulty = cfg.difficulty;
    level = Level::create(userSub("servers"), info, pool.get(), events.get());
  }
  if (!level) { DQ_ERR("server: could not open world %s", cfg.world.c_str()); return false; }
  runFlag = true;
  events->emit("serverStart", json::array({cfg.world}));
  th = std::thread([this] { loop(); });
  DQ_LOG("server: '%s' started on port %d", cfg.world.c_str(), cfg.port);
  return true;
}

void DedicatedServer::stop() {
  if (!runFlag && !th.joinable()) return;
  runFlag = false;
  if (th.joinable()) th.join();
  if (level) { events->emit("serverStop", json::array()); level->close(); level.reset(); }
  net.reset();
  pool.reset();
  DQ_LOG("server: stopped");
}

void DedicatedServer::command(const std::string& line) { std::lock_guard<std::mutex> lk(mx); cmds.push_back(line); }

ServerStatus DedicatedServer::status() const { std::lock_guard<std::mutex> lk(mx); return st; }

void DedicatedServer::loop() {
  double acc = 0, last = nowSeconds(), tpsT = last; int ticks = 0; double busy = 0;
  while (runFlag) {
    std::deque<std::string> todo;
    { std::lock_guard<std::mutex> lk(mx); todo.swap(cmds); }
    for (auto& c : todo) {
      DQ_LOG("> %s", c.c_str());
      if (c == "stop") { runFlag = false; break; }
      else if (c == "save") { level->save(true); DQ_LOG("saved"); }
      else if (startsWith(c, "say ")) { events->emit("chat", json::array({"Server", c.substr(4)})); DQ_LOG("[Server] %s", c.substr(4).c_str()); }
      else if (!events->emit("serverCommand", json::array({c}))) {}
      else DQ_LOG("commands: stop, save, say <text> (more come from the game's command list when players are on)");
    }
    double now = nowSeconds(); acc += now - last; last = now;
    int steps = 0;
    while (acc >= TICK_DT && steps < 5) {
      double t0 = nowSeconds();
      // keep spawn loaded so the world is ready for joining players
      std::vector<StreamAnchor> anchors;
      vec3 sp = level->info.spawn.y >= 0 ? level->info.spawn : vec3(0, dimBase(DIM_OVERWORLD) + 80, 0);
      anchors.push_back({sp, 3, false});
      if (onTick) onTick(*level);
      level->world().stream(anchors, 4);
      level->tick(TICK_DT);
      busy += nowSeconds() - t0;
      acc -= TICK_DT; steps++; ticks++;
    }
    if (steps == 5) acc = 0;
    if (now - tpsT >= 1) {
      std::lock_guard<std::mutex> lk(mx);
      st.running = true; st.tps = ticks / (now - tpsT); st.mspt = ticks ? busy * 1000 / ticks : 0; st.world = cfg.world; st.port = cfg.port;
      ticks = 0; busy = 0; tpsT = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  std::lock_guard<std::mutex> lk(mx); st.running = false;
}

} // namespace dq
