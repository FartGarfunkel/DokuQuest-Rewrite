// launcher/engine_tools.cpp — the working half of the Mod Studio's Engine page (the user's list: "the engine GUI with
// Strata"): edit the engine's files in place (src/, the shaders, data/, docs/), build, start the game with the engine
// bridge on and drive it live (client/engine_bridge.cpp: commands, shaders reloaded as you save them, screenshots, the
// log), and ask Strata — the user's coding AI, or any assistant with an OpenAI-style chat endpoint — about the file
// that's open, then take its code into the editor to look over before saving. Strata (or any other tool) can also
// drive the running game itself through the same bridge (docs/engine-bridge.md).
#include "launcher/launcher.h"
#include "launcher/modstudio.h"
#include "launcher/process.h"
#include "net/line_socket.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <deque>
#include <cstdlib>

namespace dq::studio {

namespace {
struct Tools {
  // the editor
  fs::path file; std::string text, saved; std::string status;
  // building
  ChildProcess build; fs::path buildLog; bool building = false; std::string buildTail; double buildAt = 0;
  // the running game
  ChildProcess game; LineClient link; std::deque<std::string> console; std::string cmdLine; double tryAt = -10; json state;
  // Strata
  ChildProcess ask; bool asking = false; std::string question, answer, endpoint, model, keyEnv; bool cfgLoaded = false; fs::path askOut;
  // the local models running on this computer, found the way Zed finds them: Ollama (11434), LM Studio (1234),
  // llama.cpp's server (8080), each asked for its models; Strata shows up among them when it's running
  struct Found { std::string label, model, endpoint; };
  std::vector<Found> found; ChildProcess look[3]; bool looking = false; double lookedAt = -1e9; fs::path lookOut[3];
};
struct Provider { const char* name; const char* list; const char* chat; };
const Provider PROVIDERS[3] = {
  {"Ollama", "http://127.0.0.1:11434/api/tags", "http://127.0.0.1:11434/v1/chat/completions"},
  {"LM Studio", "http://127.0.0.1:1234/v1/models", "http://127.0.0.1:1234/v1/chat/completions"},
  {"llama.cpp", "http://127.0.0.1:8080/v1/models", "http://127.0.0.1:8080/v1/chat/completions"},
};
Tools& T() { static Tools* t = new Tools(); return *t; }

void say(const std::string& s) { Tools& t = T(); t.console.push_back(s); while (t.console.size() > 300) t.console.pop_front(); }
std::string tailOf(const fs::path& p, size_t n) {
  auto s = readFile(p); if (!s) return "";
  size_t pos = s->size(); int lines = 0;
  while (pos > 0 && lines < (int)n) { pos = s->rfind('\n', pos - 1); if (pos == std::string::npos) { pos = 0; break; } lines++; }
  return s->substr(pos);
}
fs::path gameExe(const fs::path& root) {
  for (fs::path p : {root / "build" / exeName("dokuquest"), installDir() / exeName("dokuquest")}) { std::error_code ec; if (fs::exists(p, ec)) return p; }
  return {};
}
std::string firstCodeBlock(const std::string& a) {
  size_t i = a.find("```"); if (i == std::string::npos) return "";
  size_t nl = a.find('\n', i); if (nl == std::string::npos) return "";
  size_t e = a.find("```", nl); if (e == std::string::npos) return "";
  return a.substr(nl + 1, e - nl - 1);
}
} // namespace

// open a file into the editor (called from the Engine page's file list)
void engineOpenFile(const fs::path& p) {
  Tools& t = T();
  auto s = readFile(p);
  t.file = p; t.text = s ? *s : ""; t.saved = t.text; t.status = s ? "" : "Couldn't read it";
}
bool engineFileOpen() { return !T().file.empty(); }

void drawEngineTools(Launcher& L, const fs::path& root) {
  Tools& t = T();
  // ---- the editor ----
  if (!t.file.empty()) {
    panelBegin(L, ("Editing " + fs::relative(t.file, root).generic_string()).c_str());
    const bool dirty = t.text != t.saved;
    if (button(L, dirty ? "Save" : "Saved", dirty)) {
      if (dirty && writeFileAtomic(t.file, t.text)) {
        t.saved = t.text; t.status = "Saved";
        // a shader: the running game takes it at once
        if (t.file.extension() == ".glsl" && t.link.connected()) { t.link.send(R"({"cmd":"reloadShaders"})"); t.status += ", and the game reloaded its shaders"; }
      } else if (dirty) t.status = "Couldn't save it";
    }
    ImGui::SameLine();
    if (button(L, "Put it back") && dirty) { t.text = t.saved; t.status = "Back as it was saved"; }
    ImGui::SameLine();
    if (button(L, "Close")) { t.file.clear(); t.text.clear(); t.saved.clear(); }
    if (!t.status.empty()) { ImGui::SameLine(); hint(L, t.status.c_str()); }
    if (!t.file.empty()) {
      ImGui::PushFont(L.fMono);
      ImGui::InputTextMultiline("##edit", &t.text, ImVec2(-16, 420), ImGuiInputTextFlags_AllowTabInput);
      ImGui::PopFont();
    }
    panelEnd();
  }

  // ---- build and run ----
  panelBegin(L, "Build and run");
  hint(L, "Builds the engine from this source (the build/ folder next to src/, made with CMake), then starts the game with the "
          "engine bridge on, so this page, and Strata, can talk to it while it runs.");
  if (t.building && !t.build.running()) {
    t.building = false;
    t.buildTail = tailOf(t.buildLog, 14);
    say(t.build.exitCode() == 0 ? strFormat("Built in %.0f s", nowSeconds() - t.buildAt) : strFormat("The build failed (exit %d): see below", t.build.exitCode()));
  }
  if (button(L, t.building ? "Building…" : "Build", !t.building) && !t.building) {
    std::error_code ec; fs::create_directories(root / "build", ec);
    t.buildLog = userSub("logs") / "engine_build.log";
    ProcOptions o; o.cwd = root; o.stdoutFile = t.buildLog; o.stderrFile = t.buildLog; o.hidden = true; o.searchPath = true;
    if (t.build.start("cmake", {"--build", (root / "build").string(), "--parallel"}, o)) { t.building = true; t.buildAt = nowSeconds(); t.buildTail.clear(); say("Building…"); }
    else say("Couldn't start cmake: " + t.build.error());
  }
  ImGui::SameLine();
  const fs::path exe = gameExe(root);
  if (button(L, "Start the game (bridge on)") && !exe.empty()) {
    ProcOptions o; o.cwd = exe.parent_path();
    if (t.game.start(exe, {"--launcher", "--bridge", "25590"}, o)) { say("Game started: " + exe.string()); t.tryAt = nowSeconds() + 2; }
    else say("Couldn't start the game: " + t.game.error());
  }
  if (t.building) { t.buildTail = tailOf(t.buildLog, 6); }
  if (!t.buildTail.empty()) { ImGui::PushFont(L.fMono); ImGui::TextWrapped("%s", t.buildTail.c_str()); ImGui::PopFont(); }
  panelEnd();

  // ---- the running game ----
  panelBegin(L, "The game, live");
  if (!t.link.connected() && nowSeconds() > t.tryAt) {
    t.tryAt = nowSeconds() + 3;
    if (t.link.connect("127.0.0.1", 25590)) { say("Connected to the game"); t.link.send(R"({"cmd":"state"})"); }
  }
  for (auto& l : t.link.poll()) {
    json j = json::parse(l, nullptr, false);
    if (j.is_discarded()) { say(l); continue; }
    if (j.contains("version") && j.contains("fps")) { t.state = j; continue; }
    if (j.contains("reply")) say("> " + j["reply"].get<std::string>());
    else if (j.contains("lines")) for (auto& x : j["lines"]) say(x.get<std::string>());
    else if (j.contains("reloaded")) say(strFormat("Shaders reloaded: %d", j["reloaded"].get<int>()));
    else if (j.contains("path")) say("Screenshot: " + j["path"].get<std::string>());
    else if (j.value("ok", true) == false) say("The game says: " + j.value("error", std::string("no")));
  }
  if (!t.link.connected()) hint(L, "Not connected. Start the game here, or turn on Options > Engine bridge in the game.");
  else {
    if (t.state.is_object()) {
      std::string st = "DokuQuest " + t.state.value("version", std::string()) + strFormat(" · %.0f fps", t.state.value("fps", 0.0));
      if (t.state.contains("world")) st += " · " + t.state.value("world", std::string());
      if (t.state.contains("player")) { auto& p = t.state["player"]; st += strFormat(" · at %.0f %.0f %.0f", p.value("x", 0.0), p.value("y", 0.0), p.value("z", 0.0)); }
      hint(L, st.c_str());
    }
    if (button(L, "Reload shaders")) t.link.send(R"({"cmd":"reloadShaders"})");
    ImGui::SameLine();
    if (button(L, "Screenshot")) t.link.send(json{{"cmd", "shot"}, {"path", (userSub("screenshots") / strFormat("engine_%lld.png", (long long)unixMillis())).string()}}.dump());
    ImGui::SameLine();
    if (button(L, "Its log")) t.link.send(R"({"cmd":"log","lines":30})");
    ImGui::SameLine();
    if (button(L, "Where am I")) t.link.send(R"({"cmd":"state"})");
    ImGui::SetNextItemWidth(-120);
    bool go = ImGui::InputTextWithHint("##cmd", "/time set noon", &t.cmdLine, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((button(L, "Send") || go) && !t.cmdLine.empty()) { t.link.send(json{{"cmd", "command"}, {"text", t.cmdLine}}.dump()); say("< " + t.cmdLine); t.cmdLine.clear(); }
  }
  ImGui::BeginChild("##console", ImVec2(-16, 140), true);
  ImGui::PushFont(L.fMono);
  for (auto& l : t.console) ImGui::TextUnformatted(l.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1);
  ImGui::PopFont();
  ImGui::EndChild();
  panelEnd();

  // ---- Strata ----
  panelBegin(L, "Ask Strata");
  if (!t.cfgLoaded) {
    t.cfgLoaded = true;
    const json& x = L.settings.extra;
    t.endpoint = x.value("strataEndpoint", std::string("http://127.0.0.1:11434/v1/chat/completions"));
    t.model = x.value("strataModel", std::string("strata")); t.keyEnv = x.value("strataKeyEnv", std::string("STRATA_API_KEY"));
  }
  hint(L, "Strata, or any coding model running on this computer: like Zed, the Studio finds the models Ollama, LM Studio and "
          "llama.cpp are serving, so Strata shows up here by itself while it runs. It gets your question with the file you "
          "have open; code it sends back can go into the editor for you to look over, then Save. It can also drive the "
          "running game itself, through the engine bridge or as an MCP server (docs/engine-bridge.md).");
  // looking for local models: at once, then every 20 s while the page is open (and on "Look again")
  if (!t.looking && nowSeconds() - t.lookedAt > 20) {
    t.looking = true; t.lookedAt = nowSeconds();
    for (int i = 0; i < 3; i++) {
      t.lookOut[i] = userSub("logs") / strFormat("models_%d.json", i);
      std::error_code ec; fs::remove(t.lookOut[i], ec);
      ProcOptions o; o.hidden = true; o.searchPath = true;
      t.look[i].start(exeName("curl"), {"-sS", "--max-time", "2", "-o", t.lookOut[i].string(), PROVIDERS[i].list}, o);
    }
  }
  if (t.looking && !t.look[0].running() && !t.look[1].running() && !t.look[2].running()) {
    t.looking = false;
    std::vector<Tools::Found> f;
    for (int i = 0; i < 3; i++) {
      auto j = readJson(t.lookOut[i]);
      if (!j || !j->is_object()) continue;
      const json& list = (*j).contains("models") ? (*j)["models"] : (*j).contains("data") ? (*j)["data"] : json();
      if (list.is_array()) for (auto& m : list) {
        std::string id = m.is_object() ? m.value("name", m.value("id", m.value("model", std::string()))) : "";
        if (!id.empty()) f.push_back({id + "  (" + PROVIDERS[i].name + ")", id, PROVIDERS[i].chat});
      }
    }
    const bool first = t.found.empty();
    if (f.size() != t.found.size()) { std::string l; for (auto& x : f) l += (l.empty() ? "" : ", ") + x.label; DQ_LOG("engine: models on this computer: %s", l.empty() ? "none" : l.c_str()); }
    t.found = f;
    // Strata running and nothing else chosen yet: it's the one
    if (first) for (auto& x : t.found) if (toLower(x.model).find("strata") != std::string::npos && L.settings.extra.value("strataModel", std::string()).empty()) { t.model = x.model; t.endpoint = x.endpoint; break; }
  }
  bool cfg = false;
  {
    std::string cur = "Choose a model";
    for (auto& x : t.found) if (x.model == t.model && x.endpoint == t.endpoint) cur = x.label;
    if (t.found.empty()) cur = "None running (start Strata, Ollama or LM Studio)";
    ImGui::SetNextItemWidth(420);
    if (ImGui::BeginCombo("Model on this computer", cur.c_str())) {
      for (auto& x : t.found) if (ImGui::Selectable(x.label.c_str(), x.model == t.model && x.endpoint == t.endpoint)) { t.model = x.model; t.endpoint = x.endpoint; cfg = true; }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (button(L, "Look again")) t.lookedAt = -1e9;
  }
  if (ImGui::TreeNode("Elsewhere (a hosted model, or another address)")) {
    ImGui::SetNextItemWidth(420); cfg |= ImGui::InputText("Endpoint", &t.endpoint);
    ImGui::SetNextItemWidth(200); cfg |= ImGui::InputText("Model", &t.model);
    ImGui::SetNextItemWidth(200); cfg |= ImGui::InputText("API key from the variable", &t.keyEnv);
    ImGui::TreePop();
  }
  if (cfg) { L.settings.extra["strataEndpoint"] = t.endpoint; L.settings.extra["strataModel"] = t.model; L.settings.extra["strataKeyEnv"] = t.keyEnv; L.saveSettings(); }
  ImGui::InputTextMultiline("##q", &t.question, ImVec2(-16, 70));
  if (t.asking && !t.ask.running()) {
    t.asking = false;
    auto r = readJson(t.askOut);
    if (r && (*r).contains("choices") && (*r)["choices"].is_array() && !(*r)["choices"].empty()) t.answer = (*r)["choices"][0]["message"].value("content", std::string());
    else if (r && r->contains("error")) t.answer = "Strata answered with an error: " + (*r)["error"].dump();
    else { auto raw = readFile(t.askOut); t.answer = "No answer (curl exit " + std::to_string(t.ask.exitCode()) + ")" + (raw ? ":\n" + raw->substr(0, 600) : std::string()); }
  }
  if (button(L, t.asking ? "Asking…" : "Ask", !t.asking) && !t.asking && !t.question.empty()) {
    std::string ctx = t.file.empty() ? "" : "\n\nThe file " + fs::relative(t.file, root).generic_string() + ":\n```\n" + t.text.substr(0, 60000) + "\n```";
    json body = {{"model", t.model}, {"messages", json::array({
      {{"role", "system"}, {"content", "You are Strata, helping build DokuQuest, a C++17 voxel game engine (OpenGL 3.3/4.5, SDL2, ImGui). "
                                        "Follow the repository's CLAUDE.md rules: one feature per file, small targeted changes. When you change the open file, "
                                        "send the whole new file in one ``` code block."}},
      {{"role", "user"}, {"content", t.question + ctx}}})}};
    const fs::path dir = userSub("logs"); t.askOut = dir / "strata_answer.json";
    writeFileAtomic(dir / "strata_request.json", body.dump());
    std::vector<std::string> a = {"-sS", "--max-time", "300", "-H", "Content-Type: application/json", "-d", "@" + (dir / "strata_request.json").string(), "-o", t.askOut.string()};
    if (const char* k = std::getenv(t.keyEnv.c_str()); k && *k) { a.push_back("-H"); a.push_back(std::string("Authorization: Bearer ") + k); }
    a.push_back(t.endpoint);
    ProcOptions o; o.hidden = true; o.searchPath = true;
    std::error_code ec; fs::remove(t.askOut, ec);
    if (t.ask.start(exeName("curl"), a, o)) { t.asking = true; t.answer.clear(); } else t.answer = "Couldn't run curl: " + t.ask.error();
  }
  if (!t.answer.empty()) {
    ImGui::InputTextMultiline("##a", &t.answer, ImVec2(-16, 220), ImGuiInputTextFlags_ReadOnly);
    const std::string code = firstCodeBlock(t.answer);
    if (!code.empty() && !t.file.empty() && button(L, "Put its code in the editor (then look it over and Save)")) { t.text = code; t.status = "Strata's version: not saved yet"; }
  }
  panelEnd();
}

} // namespace dq::studio
