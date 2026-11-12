// client/engine_bridge.cpp — the engine bridge: a running game answers on 127.0.0.1 (port 25590, or --bridge PORT) to
// the launcher's Engine page and to outside tools (the user's Strata coding AI): one JSON object per line in, one per
// line out. Only on when asked for (--bridge, or Options > "Engine bridge"), and only from this computer.
//   {"cmd":"state"}                       version, fps, the world, where you are, the open menu
//   {"cmd":"command","text":"/time set noon"}   any chat command, as if typed; its reply
//   {"cmd":"reloadShaders"}               every shader again from disk (live shader editing)
//   {"cmd":"shot","path":"/tmp/a.png"}    a screenshot of the next frame
//   {"cmd":"log","lines":40}              the last lines of the game's log
//   {"cmd":"set","key":"renderDist","value":8}   change a setting (any of settings.json's), saved
// docs/engine-bridge.md has the whole of it.
#include "client/app.h"
#include "client/session.h"
#include "net/line_socket.h"
#include "render/gl.h"
#include "sim/commands.h"
#include "sim/player.h"
#include <deque>
#include <algorithm>
#include <mutex>

namespace dq {

int reloadAllShaders();   // render/gl.cpp

namespace {
struct Bridge {
  LineServer srv; bool tried = false; int sink = 0;
  std::mutex mx; std::deque<std::string> log;
  double fpsAt = 0; int frames = 0; float fps = 0;
};
Bridge& B() { static Bridge* b = new Bridge(); return *b; }
} // namespace

void engineBridgeFrame(App& app, int port) {
  Bridge& b = B();
  b.frames++;
  const double now = nowSeconds();
  if (now - b.fpsAt >= 1) { b.fps = b.frames / (float)(now - b.fpsAt); b.frames = 0; b.fpsAt = now; }
  if (!b.tried) {
    b.tried = true;
    if (!b.srv.listen((u16)port)) { DQ_WARN("engine bridge: port %d is taken", port); return; }
    b.sink = logAddSink([](LogLevel lv, const std::string& m) {
      Bridge& b = B(); std::lock_guard<std::mutex> lk(b.mx);
      b.log.push_back((lv == LogLevel::Error ? "[error] " : lv == LogLevel::Warn ? "[warn] " : "") + m);
      while (b.log.size() > 400) b.log.pop_front();
    });
    DQ_LOG("engine bridge: listening on 127.0.0.1:%d", port);
  }
  b.srv.poll([&](const std::string& line) -> std::string {
    json q, r = json::object();
    try { q = json::parse(line); } catch (...) { return json{{"ok", false}, {"error", "not JSON"}}.dump(); }
    const std::string cmd = q.value("cmd", "");
    if (q.contains("id")) r["id"] = q["id"];
    r["ok"] = true;
    GameSession* s = app.session.get();
    if (cmd == "state") {
      r["version"] = DQ_VERSION; r["fps"] = std::round(b.fps * 10) / 10; r["screen"] = app.topScreen() ? app.topScreen()->id() : "";
      if (s && s->level) {
        r["world"] = s->level->info.name; r["day"] = s->level->info.day; r["time"] = s->level->info.time;
        if (Player* p = s->player) r["player"] = {{"x", p->pos.x}, {"y", p->pos.y}, {"z", p->pos.z}, {"yaw", p->yaw}, {"pitch", p->pitch}, {"hp", p->hp}, {"dim", dimOfY((int)std::floor(p->pos.y))}, {"realm", p->realm}};
      }
    } else if (cmd == "command") {
      if (!s || !s->level) { r["ok"] = false; r["error"] = "no world open"; }
      else {   // (the bridge is the person at the keyboard, a developer: commands work even where cheats are off)
        CommandSource src; src.level = s->level.get(); src.player = s->player; src.name = s->player ? s->player->name : "Engine"; src.op = true;
        r["reply"] = runCommand(*s->level, src, q.value("text", ""));
      }
    } else if (cmd == "reloadShaders") {
      r["reloaded"] = reloadAllShaders();
    } else if (cmd == "shot") {
      const std::string path = q.value("path", "");
      if (path.empty()) { r["ok"] = false; r["error"] = "path?"; } else { app.takeScreenshot(path); r["path"] = path; }
    } else if (cmd == "log") {
      std::lock_guard<std::mutex> lk(b.mx);
      const int n = std::clamp(q.value("lines", 40), 1, 400);
      json L = json::array();
      for (size_t i = b.log.size() > (size_t)n ? b.log.size() - n : 0; i < b.log.size(); i++) L.push_back(b.log[i]);
      r["lines"] = L;
    } else if (cmd == "set") {
      json j = app.settings.toJson();
      const std::string k = q.value("key", "");
      if (k.empty() || !q.contains("value")) { r["ok"] = false; r["error"] = "key and value"; }
      else { j[k] = q["value"]; app.settings.fromJson(j); app.settings.save(); r["key"] = k; }
    } else { r["ok"] = false; r["error"] = "unknown cmd (state, command, reloadShaders, shot, log, set)"; }
    return r.dump();
  });
}

} // namespace dq
