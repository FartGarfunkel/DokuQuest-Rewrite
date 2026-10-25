// launcher/server_tab.cpp — the dedicated server lives in the launcher (it was the game's server screen in
// the HTML version, game/server.js): choose or create a server world (userDir()/servers/), port, players,
// MOTD, mode, difficulty, password, LAN visibility; start/stop; live console (every log line of this process)
// with command input. While it runs, the right side is the original's dashboard (server_dash.cpp: Players, World
// map, Isometric, Chat & console, Settings, Plugins). It runs server/server.h's DedicatedServer in-process — the
// same code as the headless dokuquest-server.
#include "launcher/server_ui.h"
#include <imgui_stdlib.h>
#include <mutex>
#include <deque>
#include <ctime>

namespace dq {

namespace {
struct ConsoleLine { LogLevel lv; std::string text; };
struct ServerTab {
  ServerConfig cfg; bool loaded = false;
  std::unique_ptr<DedicatedServer> srv;
  double startedAt = 0;
  std::mutex mx; std::deque<ConsoleLine> lines; bool newLines = false; int sink = -1;
  std::string cmd; std::vector<std::string> history; int histPos = -1;
  std::vector<WorldEntry> worlds, saves; double listedAt = -100;
  std::string newName = "Server World", newSeed, msg;
  int copyIdx = -1;
};
ServerTab& st() { static ServerTab s; return s; }

fs::path cfgFile() { return userSub("servers") / "server.json"; }
void saveCfg() { writeJson(cfgFile(), st().cfg.toJson()); }

int histCallback(ImGuiInputTextCallbackData* d) {
  ServerTab& S = st();
  if (d->EventFlag != ImGuiInputTextFlags_CallbackHistory || S.history.empty()) return 0;
  if (d->EventKey == ImGuiKey_UpArrow) S.histPos = S.histPos < 0 ? (int)S.history.size() - 1 : std::max(0, S.histPos - 1);
  else if (d->EventKey == ImGuiKey_DownArrow) S.histPos = S.histPos < 0 ? -1 : (S.histPos + 1 >= (int)S.history.size() ? -1 : S.histPos + 1);
  d->DeleteChars(0, d->BufTextLen);
  if (S.histPos >= 0) d->InsertChars(0, S.history[S.histPos].c_str());
  return 0;
}

const char* modes[] = {"survival", "creative", "adventure"};
const char* modeLabels[] = {"Survival", "Creative", "Adventure"};
const char* diffs[] = {"peaceful", "easy", "normal", "hard"};
const char* diffLabels[] = {"Peaceful", "Easy", "Normal", "Hard"};
int indexOf(const char* const* arr, int n, const std::string& v) { for (int i = 0; i < n; i++) if (v == arr[i]) return i; return 0; }

bool validFolderName(const std::string& n) {
  if (n.empty() || n.size() > 48 || n == "." || n == "..") return false;
  for (char c : n) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32) return false;
  return true;
}
}

void serverInitConsole() {
  ServerTab& S = st();
  if (S.sink >= 0) return;
  S.sink = logAddSink([](LogLevel lv, const std::string& text) {   // called with the log lock held: never log here
    ServerTab& T = st();
    char ts[16]; std::time_t t = std::time(nullptr); std::strftime(ts, sizeof ts, "%H:%M:%S", std::localtime(&t));
    std::lock_guard<std::mutex> lk(T.mx);
    T.lines.push_back({lv, std::string("[") + ts + "] " + text});
    while (T.lines.size() > 3000) T.lines.pop_front();
    T.newLines = true;
  });
}

bool serverRunning() { return st().srv && st().srv->running(); }

void serverShutdown() {
  ServerTab& S = st();
  if (S.srv) { S.srv->stop(); S.srv.reset(); }
  if (S.sink >= 0) { logRemoveSink(S.sink); S.sink = -1; }
}

static void loadCfg() { ServerTab& S = st(); if (!S.loaded) { if (auto j = readJson(cfgFile())) S.cfg.fromJson(*j); S.loaded = true; } }

bool serverStartFromConfig(Launcher& L) {
  ServerTab& S = st();
  loadCfg();
  if (S.srv) return true;
  if (!validFolderName(S.cfg.world)) { S.msg = "Choose a server world first."; return false; }
  saveCfg();
  S.srv = std::make_unique<DedicatedServer>(S.cfg);
  if (S.srv->start()) { S.startedAt = L.time; return true; }
  S.srv.reset(); S.msg = "The server could not start; see the console.";
  return false;
}

void drawServer(Launcher& L) {
  ServerTab& S = st();
  loadCfg();
  if (L.time - S.listedAt > 3) { S.worlds = listWorlds(userSub("servers")); S.saves = listWorlds(userSub("saves")); S.listedAt = L.time; }
  // the server stopped itself ("stop" typed in the console): join its thread and save
  if (S.srv && !S.srv->running()) { S.srv->stop(); S.srv.reset(); }
  bool running = S.srv != nullptr;
  if (running) {   // the original's running view: the dashboard takes the whole tab
    ServerStatus status = S.srv->status();
    bool stop = false;
    drawServerDashboard(L, *S.srv, status, L.time - S.startedAt, stop);
    if (stop) { S.srv->stop(); S.srv.reset(); }
    return;
  }
  ServerConfig& c = S.cfg;
  bool cfgChanged = false;

  float leftW = std::min(400.f, ImGui::GetContentRegionAvail().x * 0.4f), h = ImGui::GetContentRegionAvail().y;
  beginDokuPanel("##srvcfg", ImVec2(leftW, h), true, ImGuiWindowFlags_NavFlattened);
  ImGui::PushFont(L.fMid); shadowText(ImGui::GetWindowDrawList(), L.fMid, ImGui::GetCursorScreenPos(), C_WHITE, IM_COL32(63, 63, 63, 255), 2, "Dedicated Server");
  ImGui::Dummy(ImVec2(0, L.fMid->FontSize + 2)); ImGui::PopFont();
  ImGui::PushFont(L.fSmall); dimText("Runs a world here for others to join. Players connect to this computer's address and port."); ImGui::PopFont();
  ImGui::Spacing();
  if (running) ImGui::BeginDisabled();
  sideHeading(L, "World");
  ImGui::SetNextItemWidth(-1);
  std::string curLabel = c.world;
  bool exists = false; for (auto& w : S.worlds) if (w.folder == c.world) { exists = true; if (w.name != w.folder) curLabel = w.name + " (" + w.folder + ")"; }
  if (!exists) curLabel += "  — new";
  if (ImGui::BeginCombo("##world", curLabel.c_str())) {
    for (auto& w : S.worlds) if (ImGui::Selectable((w.name + "##" + w.folder).c_str(), w.folder == c.world)) { c.world = w.folder; cfgChanged = true; }
    if (S.worlds.empty()) ImGui::TextDisabled("No server worlds yet");
    ImGui::EndCombo();
  }
  ImGui::PushFont(L.fSmall);
  ImGui::SetNextItemWidth(leftW * 0.42f); ImGui::InputTextWithHint("##nn", "New world name", &S.newName);
  ImGui::SameLine(); ImGui::SetNextItemWidth(leftW * 0.28f); ImGui::InputTextWithHint("##ns", "Seed", &S.newSeed);
  ImGui::PopFont();
  ImGui::SameLine();
  if (mcButton(L, "New", ImVec2(-1, ImGui::GetFrameHeight()), BtnKind::Grey, validFolderName(S.newName), L.fUi)) {
    c.world = S.newName; c.seed = S.newSeed; cfgChanged = true; S.msg = "A new world \"" + S.newName + "\" is made when the server starts.";
  }
  if (!S.saves.empty()) {
    ImGui::PushFont(L.fSmall);
    ImGui::SetNextItemWidth(leftW * 0.72f);
    std::string pv = S.copyIdx >= 0 && S.copyIdx < (int)S.saves.size() ? S.saves[S.copyIdx].name : "Copy one of your worlds…";
    if (ImGui::BeginCombo("##copy", pv.c_str())) {
      for (int i = 0; i < (int)S.saves.size(); i++) if (ImGui::Selectable((S.saves[i].name + "##" + S.saves[i].folder).c_str(), i == S.copyIdx)) S.copyIdx = i;
      ImGui::EndCombo();
    }
    ImGui::PopFont();
    ImGui::SameLine();
    if (mcButton(L, "Copy", ImVec2(-1, ImGui::GetFrameHeight()), BtnKind::Grey, S.copyIdx >= 0, L.fUi) && S.copyIdx < (int)S.saves.size()) {
      auto& w = S.saves[S.copyIdx];
      fs::path to = userSub("servers") / w.folder; std::error_code ec;
      if (fs::exists(to)) S.msg = "There is already a server world called " + w.folder + ".";
      else { fs::copy(userSub("saves") / w.folder, to, fs::copy_options::recursive, ec);
        S.msg = ec ? "Could not copy: " + ec.message() : "Copied " + w.name + " to the server worlds.";
        if (!ec) { c.world = w.folder; cfgChanged = true; S.listedAt = -100; } }
    }
  }
  if (!S.msg.empty()) { ImGui::PushFont(L.fSmall); dimText("%s", S.msg.c_str()); ImGui::PopFont(); }
  ImGui::Spacing();
  sideHeading(L, "Settings");
  float lw = 120;
  auto label = [&](const char* t) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(t); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1); };
  label("Port"); if (ImGui::InputInt("##port", &c.port, 1, 10)) { c.port = clampv(c.port, 1024, 65535); cfgChanged = true; }
  label("Max players"); if (ImGui::SliderInt("##maxp", &c.maxPlayers, 1, 64)) cfgChanged = true;
  label("MOTD"); if (ImGui::InputText("##motd", &c.motd)) cfgChanged = true;
  int mi = indexOf(modes, 3, c.mode), di = indexOf(diffs, 4, c.difficulty);
  label("Game mode"); if (ImGui::Combo("##mode", &mi, modeLabels, 3)) { c.mode = modes[mi]; cfgChanged = true; }
  label("Difficulty"); if (ImGui::Combo("##diff", &di, diffLabels, 4)) { c.difficulty = diffs[di]; cfgChanged = true; }
  label("Password"); if (ImGui::InputTextWithHint("##pw", "none", &c.password, ImGuiInputTextFlags_Password)) cfgChanged = true;
  label("View distance"); if (ImGui::SliderInt("##vd", &c.viewDistance, 2, 32, "%d chunks")) cfgChanged = true;
  if (ImGui::Checkbox("Show on the local network (LAN)", &c.lan)) cfgChanged = true;
  if (ImGui::Checkbox("Players can hurt each other (PvP)", &c.pvp)) cfgChanged = true;
  ImGui::PushFont(L.fSmall); dimText("Game mode and difficulty are used when the world is made; in a world that exists, change them with commands."); ImGui::PopFont();
  if (running) ImGui::EndDisabled();
  ImGui::Spacing();
  if (!running) {
    if (mcButton(L, "Start server", ImVec2(-1, 46), BtnKind::Go, validFolderName(c.world), L.fBig)) serverStartFromConfig(L);
  } else if (mcButton(L, "Stop server", ImVec2(-1, 46), BtnKind::Grey, true, L.fBig)) { S.srv->stop(); S.srv.reset(); }
  if (mcButton(L, "Open server folder", ImVec2(-1, 32), BtnKind::Grey, true, L.fUi)) openPath(userSub("servers"));
  endDokuPanel();
  if (cfgChanged) saveCfg();

  ImGui::SameLine(0, 16);
  float rightW = ImGui::GetContentRegionAvail().x;
  beginDokuPanel("##srvrun", ImVec2(rightW, h), false, ImGuiWindowFlags_NavFlattened);
  {
    ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(p.x + 7, p.y + L.fUi->FontSize * 0.5f + 1), 5, IM_COL32(120, 110, 100, 255));
    ImGui::Dummy(ImVec2(16, 0)); ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_DIM), "Stopped");
  }
  ImGui::PushFont(L.fSmall); dimText("Once it runs, this side shows who's on, a live map of the world with everyone on it, a live isometric view round any player, the chat and console, the settings and the plugins."); ImGui::PopFont();
  drawServerConsole(L, nullptr, ImGui::GetContentRegionAvail(), false);
  endDokuPanel();
}

// the log (every line of this process) and the command line under it
void drawServerConsole(Launcher& L, DedicatedServer* srv, ImVec2 size, bool withPlayers) {
  ServerTab& S = st();
  bool running = srv != nullptr;
  ServerStatus status = running ? srv->status() : ServerStatus();
  float playersW = withPlayers ? 180 : 0, gap = withPlayers ? 10 : 0, consH = size.y - ImGui::GetFrameHeight() - 10;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 200));
  ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(77, 63, 51, 255));
  ImGui::BeginChild("##console", ImVec2(size.x - playersW - gap, consH), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PushFont(L.fMono);
  {
    std::lock_guard<std::mutex> lk(S.mx);
    bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4;
    ImGuiListClipper clip; clip.Begin((int)S.lines.size());
    while (clip.Step())
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
        auto& ln = S.lines[i];
        ImU32 col = ln.lv == LogLevel::Error ? C_BAD : ln.lv == LogLevel::Warn ? IM_COL32(255, 215, 90, 255) : startsWith(ln.text.substr(ln.text.find(']') + 2), ">") ? C_FROST : IM_COL32(228, 221, 210, 255);
        ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(ln.text.c_str()); ImGui::PopStyleColor();
      }
    if (S.newLines && atBottom) ImGui::SetScrollHereY(1.0f);
    S.newLines = false;
  }
  ImGui::PopFont();
  ImGui::EndChild();
  if (withPlayers) {
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##players", ImVec2(playersW, consH), ImGuiChildFlags_Borders);
    sideHeading(L, "Players");
    if (status.playerNames.empty()) { ImGui::PushFont(L.fSmall); dimText(running ? "Nobody is on yet." : "The server is stopped."); ImGui::PopFont(); }
    for (auto& n : status.playerNames) ImGui::TextUnformatted(n.c_str());
    ImGui::EndChild();
  }
  ImGui::PopStyleColor(2);
  // command input
  ImGui::SetNextItemWidth(size.x - 110);
  bool send = ImGui::InputTextWithHint("##cmd", running ? "Say something to everyone, or type a command (stop, save, say, list, kick, /time…)" : "Start the server to type commands", &S.cmd,
                                       ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, histCallback);
  ImGui::SameLine();
  if (mcButton(L, "Send", ImVec2(-1, ImGui::GetFrameHeight()), BtnKind::Grey, running && !S.cmd.empty(), L.fUi)) send = true;
  if (send && running && !S.cmd.empty()) {
    srv->command(S.cmd); S.history.push_back(S.cmd); S.histPos = -1; S.cmd.clear();
    ImGui::SetKeyboardFocusHere(-1);
  }
}

void serverOpenPage(const std::string& page, const std::string& follow) {
  for (int i = 0; i < (int)SvTab::Count; i++) if (page == svTabId((SvTab)i)) serverDash().tab = (SvTab)i;
  if (!follow.empty()) serverDash().follow = follow;
}

} // namespace dq
