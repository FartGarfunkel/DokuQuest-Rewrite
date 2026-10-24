// tools/server_main.cpp — headless dedicated server (the launcher's Server tab runs the same code in-process).
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
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--userdir") co.userDirOverride = next();
    else if (a == "--world") cfg.world = next();
    else if (a == "--port") cfg.port = std::stoi(next());
    else if (a == "--help") { printf("dokuquest-server [--world NAME] [--port N] [--userdir DIR]\n"); return 0; }
  }
  if (!dq::initCore(co)) return 1;
  std::signal(SIGINT, [](int) { g_quit = true; });
  std::signal(SIGTERM, [](int) { g_quit = true; });
  dq::DedicatedServer srv(cfg);
  if (!srv.start()) return 2;
  std::thread console([&] { std::string line; while (!g_quit && std::getline(std::cin, line)) srv.command(line); });
  console.detach();
  while (!g_quit && srv.running()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  srv.stop();
  return 0;
}
