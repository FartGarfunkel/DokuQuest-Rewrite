// server/server.h — the dedicated server: runs a Level on its own thread at a fixed tick and
// serves players over the network (net/). Used by the launcher's Server tab (in-process, with a
// console view) and by the headless dokuquest-server.
#pragma once
#include "sim/level.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>

namespace dq {

struct ServerConfig {
  std::string world = "Server World";   // folder under userDir()/servers/ (created if missing)
  std::string seed;
  std::string mode = "survival", difficulty = "normal";
  int port = 25580;
  int maxPlayers = 8;
  std::string motd = "A DokuQuest server";
  bool pvp = true, lan = true;          // lan: answer LAN discovery broadcasts
  std::string password;
  int viewDistance = 8;
  json toJson() const;
  void fromJson(const json& j);
};

struct ServerStatus { bool running = false; int players = 0; double tps = 0; double mspt = 0; std::vector<std::string> playerNames; std::string world; int port = 0; };

class DedicatedServer {
public:
  explicit DedicatedServer(ServerConfig cfg);
  ~DedicatedServer();
  bool start();
  void stop();
  bool running() const { return runFlag; }
  void command(const std::string& line);     // console input (thread-safe)
  ServerStatus status() const;
  const ServerConfig& config() const { return cfg; }
  // hook for net/: the network layer attaches here (created in start())
  std::function<void(Level&)> onTick;
private:
  void loop();
  ServerConfig cfg;
  std::unique_ptr<ThreadPool> pool;
  std::unique_ptr<EventBus> events;
  std::unique_ptr<Level> level;
  std::thread th;
  std::atomic<bool> runFlag{false};
  mutable std::mutex mx;
  std::deque<std::string> cmds;
  ServerStatus st;
  std::shared_ptr<void> net;   // net::ServerHost (type-erased to keep this header light)
};

} // namespace dq
