// launcher/mcp_server.cpp — `dokuquest-launcher --mcp [port]`: the engine bridge as an MCP server (Model Context
// Protocol, JSON-RPC 2.0 over stdin/stdout, one message per line), so a coding AI that speaks MCP — the user's Strata,
// Claude, or any other — can drive a running game: its state, chat commands, shaders reloaded from disk, screenshots,
// the log, settings. The game must have the bridge on (Options > Engine bridge, or dokuquest --bridge). Nothing but
// protocol goes to stdout. docs/engine-bridge.md.
//   An MCP client's config:  {"command": "/path/to/dokuquest-launcher", "args": ["--mcp"]}
#include "common/fs.h"
#include "net/line_socket.h"
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace dq {

namespace {
struct Tool { const char* name; const char* desc; json schema; };
std::vector<Tool> tools() {
  auto obj = [](json props, json req) { return json{{"type", "object"}, {"properties", props}, {"required", req}}; };
  return {
    {"game_state", "The running game's version, frame rate, open menu, world, day and time, and the player (position, look, health, land).", obj(json::object(), json::array())},
    {"run_command", "Run a chat command in the game as the player, e.g. \"/time set noon\", \"/tp ~ ~10 ~\", \"/give diamond 3\", \"/lineup 1\". Returns the command's reply.",
     obj({{"text", {{"type", "string"}, {"description", "The command, starting with /"}}}}, json::array({"text"}))},
    {"reload_shaders", "Build every shader again from disk (after editing assets/shaders/*.glsl or a shader pack). Returns how many built.", obj(json::object(), json::array())},
    {"screenshot", "Save a screenshot of the game's next frame to a PNG file.", obj({{"path", {{"type", "string"}, {"description", "Where to save the PNG"}}}}, json::array({"path"}))},
    {"read_log", "The last lines of the game's log (warnings and errors marked).", obj({{"lines", {{"type", "integer"}, {"description", "How many (1-400, default 40)"}}}}, json::array())},
    {"set_setting", "Change one of the game's settings (a settings.json key, e.g. renderDist, farTerrain, fov, shaderLevel) and save it.",
     obj({{"key", {{"type", "string"}}}, {"value", {{"description", "The new value (number, text or true/false)"}}}}, json::array({"key", "value"}))},
  };
}

// one request to the game over the bridge, its answer (or an error)
json ask(int port, const json& req, std::string& err) {
  LineClient c;
  if (!c.connect("127.0.0.1", (u16)port)) { err = "The game isn't answering on 127.0.0.1:" + std::to_string(port) + ". Start it with the engine bridge on (Options > Engine bridge, or dokuquest --bridge)."; return json(); }
  c.send(req.dump());
  const double t0 = nowSeconds();
  while (nowSeconds() - t0 < 8) {
    for (auto& l : c.poll()) { json j = json::parse(l, nullptr, false); if (!j.is_discarded()) return j; }
    if (!c.open()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  err = "The game didn't answer in time.";
  return json();
}

void reply(const json& id, const json& result) { std::cout << json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}.dump() << "\n" << std::flush; }
void fail(const json& id, int code, const std::string& msg) { std::cout << json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", msg}}}}.dump() << "\n" << std::flush; }
} // namespace

int runMcpServer(int port) {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    json m = json::parse(line, nullptr, false);
    if (m.is_discarded() || !m.is_object()) { fail(nullptr, -32700, "Parse error"); continue; }
    const std::string method = m.value("method", "");
    const json id = m.contains("id") ? m["id"] : json();
    const bool note = !m.contains("id");   // a notification: no answer
    if (method == "initialize") {
      std::string pv = m.contains("params") && m["params"].is_object() ? m["params"].value("protocolVersion", std::string("2024-11-05")) : "2024-11-05";
      reply(id, {{"protocolVersion", pv}, {"capabilities", {{"tools", json::object()}}},
                 {"serverInfo", {{"name", "dokuquest"}, {"version", DQ_VERSION}}},
                 {"instructions", "Drives a running DokuQuest game through its engine bridge (port " + std::to_string(port) + "). Edit the engine's files with your own file tools; use these to see and test the result live."}});
    } else if (method == "ping") reply(id, json::object());
    else if (method == "tools/list") {
      json L = json::array();
      for (auto& t : tools()) L.push_back({{"name", t.name}, {"description", t.desc}, {"inputSchema", t.schema}});
      reply(id, {{"tools", L}});
    } else if (method == "tools/call") {
      const json p = m.value("params", json::object());
      const std::string name = p.value("name", "");
      const json a = p.value("arguments", json::object());
      json req;
      if (name == "game_state") req = {{"cmd", "state"}};
      else if (name == "run_command") req = {{"cmd", "command"}, {"text", a.value("text", "")}};
      else if (name == "reload_shaders") req = {{"cmd", "reloadShaders"}};
      else if (name == "screenshot") req = {{"cmd", "shot"}, {"path", a.value("path", "")}};
      else if (name == "read_log") req = {{"cmd", "log"}, {"lines", a.value("lines", 40)}};
      else if (name == "set_setting") req = {{"cmd", "set"}, {"key", a.value("key", "")}, {"value", a.contains("value") ? a["value"] : json()}};
      else { fail(id, -32602, "No tool called " + name); continue; }
      std::string err;
      json r = ask(port, req, err);
      std::string text;
      bool bad = !err.empty() || (r.is_object() && !r.value("ok", true));
      if (!err.empty()) text = err;
      else if (name == "run_command") text = r.value("reply", std::string("(no reply)"));
      else if (name == "read_log" && r.contains("lines")) { for (auto& l : r["lines"]) text += l.get<std::string>() + "\n"; }
      else text = r.dump(2);
      if (r.is_object() && r.contains("error")) text = r.value("error", text);
      reply(id, {{"content", json::array({{{"type", "text"}, {"text", text}}})}, {"isError", bad}});
    } else if (!note) fail(id, -32601, "Method not found: " + method);
  }
  return 0;
}

} // namespace dq
