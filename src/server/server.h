// server/server.h — the dedicated server (game/server.js): runs a Level on its own thread at a fixed tick and
// serves players over the network (net/ServerHost), with plugins (mod/), a console and a log. Used by the
// launcher's Server tab (in-process, with a console view) and by the headless dokuquest-server.
#pragma once
#include "sim/level.h"
#include "server/server_view.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>

namespace dq {

namespace net { class ServerHost; }
class ModLoader;

struct ServerConfig {
  std::string world = "Server World";   // folder under userDir()/servers/ (created if missing)
  std::string seed;
  std::string mode = "survival", difficulty = "normal";
  std::string name = "DokuQuest Server"; // shown in LAN lists (the original's "Server name")
  int port = 25580;                      // UDP; the next 9 are tried when it's taken
  int maxPlayers = 8;
  std::string motd = "Welcome! Be kind, build big.";
  bool pvp = true, lan = true;           // lan: announce on the home network (UDP 25581)
  bool plugins = true;                   // run mods & plugins from userDir()/mods
  std::string password;
  int viewDistance = 8;
  json toJson() const;
  void fromJson(const json& j);
};

struct ServerPlayer { std::string name, address; int realm = 0; vec3 pos{0}; float hp = 20; bool op = false; u32 ping = 0; };
struct ServerPlugin { std::string id, name, kind, version, error; bool plugin = false, enabled = true, loaded = false; };
struct ServerStatus {
  bool running = false; int players = 0, maxPlayers = 0; double tps = 0; double mspt = 0;
  std::vector<std::string> playerNames; std::vector<ServerPlayer> playerList;
  std::string world, name, motd, address; int port = 0; i64 day = 0; double uptime = 0;   // address: this computer on the home network
  bool pluginsOn = false; std::vector<ServerPlugin> plugins;                               // the Plugins tab
};
struct ServerLogLine { u64 seq = 0; i64 at = 0; std::string text, kind; };   // kind: "", good, warn, chat

class DedicatedServer {
public:
  explicit DedicatedServer(ServerConfig cfg);
  ~DedicatedServer();
  bool start(std::string* err = nullptr);
  void stop();
  bool running() const { return runFlag; }
  void command(const std::string& line);     // console input (thread-safe): stop, save, say, list, kick, ban, op, tp, give... or any /command
  ServerStatus status() const;
  // the dashboard's map / isometric view / players (server/server_view.h): say what is being looked at, take the latest copy
  void setView(const ServerViewReq& r);
  std::shared_ptr<const ServerView> view() const;
  std::vector<ServerLogLine> logSince(u64 seq) const;   // the console view (thread-safe), last 400 lines
  const ServerConfig& config() const { return cfg; }
  ModLoader* modLoader() const { return mods.get(); }   // the Plugins tab (read it while the server runs only from callbacks)
  std::function<void(Level&)> onTick;        // extra per-tick work on the server thread (tests, launcher previews)
private:
  void loop();
  void console(const std::string& line);
  void log(const std::string& text, const std::string& kind = "");
  ServerConfig cfg;
  std::unique_ptr<ThreadPool> pool;
  std::unique_ptr<EventBus> events;
  std::unique_ptr<ModLoader> mods;
  std::unique_ptr<Level> level;
  std::shared_ptr<net::ServerHost> host;
  std::thread th;
  std::atomic<bool> runFlag{false};
  mutable std::mutex mx;
  std::deque<std::string> cmds;
  std::deque<ServerLogLine> logLines;
  u64 logSeq = 0;
  ServerStatus st;
  double startedAt = 0;
  ServerViewReq viewReq; double viewReqAt = -100; ServerView viewWork; std::shared_ptr<const ServerView> viewPub; double viewAt = -100;
  void updateView(double now);
};

} // namespace dq
