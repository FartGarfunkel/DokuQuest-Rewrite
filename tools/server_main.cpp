// tools/server_main.cpp — headless dedicated server (the launcher's Server tab runs the same code in-process).
// Settings come from <userdir>/servers/<world>.server.json when it exists, then the command line.
// Type console commands on stdin: stop, save, say, list, kick, ban, op, tp, give, help...
#include "core/init.h"
#include "server/server.h"
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>
#include <iostream>

static std::atomic<bool> g_quit{false};
int main(int argc, char** argv) {
  dq::CoreOptions co; co.server = true;
  dq::ServerConfig cfg;
  dq::json cli = dq::json::object();
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--userdir") co.userDirOverride = next();
    else if (a == "--world") cli["world"] = next();
    else if (a == "--port") cli["port"] = std::atoi(next().c_str());
    else if (a == "--seed") cli["seed"] = next();
    else if (a == "--mode") cli["mode"] = next();
    else if (a == "--name") cli["name"] = next();
    else if (a == "--motd") cli["motd"] = next();
    else if (a == "--password") cli["password"] = next();
    else if (a == "--max") cli["maxPlayers"] = std::atoi(next().c_str());
    else if (a == "--view") cli["viewDistance"] = std::atoi(next().c_str());
    else if (a == "--no-lan") cli["lan"] = false;
    else if (a == "--no-plugins") cli["plugins"] = false;
    else if (a == "--no-pvp") cli["pvp"] = false;
    else if (a == "--help") {
      printf("dokuquest-server [--world NAME] [--port N] [--seed S] [--mode survival|creative] [--name TEXT] [--motd TEXT]\n"
             "                 [--password P] [--max N] [--view CHUNKS] [--no-lan] [--no-plugins] [--no-pvp] [--userdir DIR]\n");
      return 0;
    }
  }
  if (!dq::initCore(co)) return 1;
  std::string world = cli.value("world", cfg.world);
  if (auto saved = dq::readJson(dq::userSub("servers") / (world + ".server.json"))) cfg.fromJson(*saved);
  cfg.fromJson(cli);
  std::signal(SIGINT, [](int) { g_quit = true; });
  std::signal(SIGTERM, [](int) { g_quit = true; });
  dq::DedicatedServer srv(cfg);
  std::string err;
  if (!srv.start(&err)) { fprintf(stderr, "The server could not start: %s\n", err.c_str()); return 2; }
  std::thread console([&] { std::string line; while (!g_quit && std::getline(std::cin, line)) srv.command(line); });
  console.detach();
  while (!g_quit && srv.running()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  srv.stop();
  return 0;
}
