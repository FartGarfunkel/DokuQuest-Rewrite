// launcher/home.cpp — launcher.html's side panel (your worlds, tips) and bottom bar (logo + version, notice,
// player name, Options… / Game folder, Play / Dedicated Server), and Play itself: the game runs as a child
// process (`dokuquest --launcher --renderer legacy|modern [--world NAME]`); the launcher shows it running and
// comes back when it exits.
#include "launcher/launcher.h"
#include <imgui_stdlib.h>
#include <ctime>

namespace dq {

std::vector<WorldEntry> listWorlds(const fs::path& dir) {
  std::vector<WorldEntry> out; std::error_code ec;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    if (!e.is_directory()) continue;
    auto j = readJson(e.path() / "level.json");
    if (!j) continue;
    WorldEntry w; w.folder = e.path().filename().string();
    w.name = j->value("name", w.folder); w.mode = j->value("mode", "survival"); w.lastPlayed = j->value("lastPlayed", (i64)0);
    out.push_back(w);
  }
  std::sort(out.begin(), out.end(), [](const WorldEntry& a, const WorldEntry& b) { return a.lastPlayed > b.lastPlayed; });
  return out;
}

void Launcher::refreshWorlds(bool force) {
  if (!force && time - worldsT < 5) return;
  worldsT = time;
  worlds = listWorlds(userSub("saves"));
}

void Launcher::play(const std::string& world) {
  if (gameRunning) return;
  std::string ver;
  fs::path exe = updater.executable("dokuquest", &ver);
  if (!fs::exists(exe)) { setNotice("The game program is missing next to the launcher. Reinstall DokuQuest."); return; }
  if (settings.playerName.empty()) settings.playerName = "Wanderer";
  saveSettings();
  std::vector<std::string> args = {"--launcher", "--renderer", settings.renderer == RendererKind::Legacy ? "legacy" : "modern"};
  if (!userDirArg.empty()) { args.push_back("--userdir"); args.push_back(userDirArg); }
  if (!world.empty()) { args.push_back("--world"); args.push_back(world); }
  ProcOptions o; o.cwd = exe.parent_path();
  if (!game.start(exe, args, o)) { setNotice("The game could not be started: " + game.error()); return; }
  DQ_LOG("launcher: started %s (%s)%s%s", exe.string().c_str(), ver.c_str(), world.empty() ? "" : " world ", world.c_str());
  gameRunning = true; gameVersion = ver; worldPlaying = world;
  setNotice("");
  if (!keepOpen() && !serverRunning()) quit = true;   // "Keep the launcher open" off: the game carries on alone
}

void Launcher::pollGame() {
  if (!gameRunning || game.running()) return;
  gameRunning = false;
  int code = game.exitCode();
  double ran = nowSeconds() - game.startTime();
  DQ_LOG("launcher: the game exited with code %d after %.1f s", code, ran);
  if (code != 0 && ran < 15 && gameVersion != DQ_VERSION) {
    // a freshly installed version that cannot start: fall back to the one before (updater keeps it)
    updater.markBad(gameVersion, strFormat("exit code %d after %.0f s", code, ran));
    std::string back; updater.executable("dokuquest", &back);
    setNotice("Version " + niceVersion(gameVersion) + " did not start here; using " + niceVersion(back) + " again. Press Play.");
    loadNews(updater.executable("dokuquest", nullptr).parent_path() / "assets");
  } else if (code != 0) setNotice(strFormat("The game closed with an error (code %d). Its log is latest.log in the game folder.", code));
  // the game saves its own options (settings.json) when it quits: take them over
  std::string name = settings.playerName;
  settings = Settings::load();
  if (settings.playerName.empty()) settings.playerName = name;
  SDL_RestoreWindow(window); SDL_RaiseWindow(window);
  refreshWorlds(true);
}

void drawHomeSide(Launcher& L, ImVec2 size) {
  beginDokuPanel("##side", size, true, ImGuiWindowFlags_NavFlattened);
  L.refreshWorlds();
  sideHeading(L, "Your worlds");
  if (L.worlds.empty()) { ImGui::PushFont(L.fSmall); dimText("Worlds you play appear here."); ImGui::PopFont(); }
  float w = ImGui::GetContentRegionAvail().x;
  for (size_t i = 0; i < L.worlds.size() && i < 8; i++) {
    auto& wd = L.worlds[i];
    ImGui::PushID((int)i);
    ImVec2 a = ImGui::GetCursorScreenPos();
    float h = L.fUi->FontSize + L.fSmall->FontSize + 18;
    bool clicked = ImGui::InvisibleButton("##w", ImVec2(w, h));
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    if (hot) ImGui::SetTooltip(L.gameRunning ? "The game is running" : "Play %s", wd.name.c_str());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, hot ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 13), 3);
    dl->AddRect(a, b, hot ? IM_COL32(125, 135, 196, 255) : IM_COL32(255, 255, 255, 20), 3, 0, 1);
    dl->AddText(L.fUi, L.fUi->FontSize, ImVec2(a.x + 9, a.y + 7), C_WHITE, wd.name.c_str());
    char date[32] = "";
    if (wd.lastPlayed > 0) { std::time_t t = (std::time_t)(wd.lastPlayed / 1000); std::strftime(date, sizeof date, "%Y-%m-%d", std::localtime(&t)); }
    std::string sub = std::string(wd.mode == "creative" ? "Creative" : wd.mode == "adventure" ? "Adventure" : wd.mode == "spectator" ? "Spectator" : "Survival") + (date[0] ? std::string(" · ") + date : "");
    dl->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(a.x + 9, a.y + 9 + L.fUi->FontSize), C_DIM, sub.c_str());
    if (hot && !L.gameRunning) dl->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(b.x - 40, a.y + 7), IM_COL32(255, 255, 160, 255), "Play");
    if (clicked && !L.gameRunning) L.play(wd.folder);
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(0, 4));
  sideHeading(L, "Tips");
  ImGui::PushFont(L.fSmall);
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(216, 208, 196, 255));
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted("F11 or Alt+Enter: full screen.\nF2: screenshot. F5: change view.\nT or /: chat and commands.");
  ImGui::Spacing();
  ImGui::TextUnformatted("Worlds are kept in the game folder, so newer versions of the launcher find them.");
  ImGui::Spacing();
  ImGui::TextUnformatted("Controllers work in every menu: the left stick or D-pad moves, A chooses.");
  ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::PopFont();
  endDokuPanel();
}

static int nameFilter(ImGuiInputTextCallbackData* d) {
  ImWchar c = d->EventChar;
  return (c < 32 || c == '"' || c == '\\' || c == '/') ? 1 : 0;
}

void drawBottomBar(Launcher& L, float barH) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 a(0, (float)L.height - barH), b((float)L.width, (float)L.height);
  drawDirt(dl, a, b, L, 0.5f);
  dl->AddLine(ImVec2(a.x, a.y + 1), ImVec2(b.x, a.y + 1), IM_COL32(0, 0, 0, 255), 2);
  dl->AddLine(ImVec2(a.x, a.y + 3), ImVec2(b.x, a.y + 3), IM_COL32(255, 255, 255, 20), 2);
  float pad = 18, cy = a.y + barH * 0.5f;
  // brand: icon, name, version
  float iconS = 56;
  if (L.texIcon) {
    dl->AddImage((ImTextureID)(intptr_t)L.texIcon, ImVec2(pad + 2, cy - iconS / 2 + 3), ImVec2(pad + iconS + 2, cy + iconS / 2 + 3), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, 150));
    dl->AddImage((ImTextureID)(intptr_t)L.texIcon, ImVec2(pad, cy - iconS / 2), ImVec2(pad + iconS, cy + iconS / 2));
  }
  float nx = pad + iconS + 12;
  shadowText(dl, L.fBig, ImVec2(nx, cy - L.fBig->FontSize + 2), C_WHITE, IM_COL32(63, 63, 63, 255), 3, "DokuQuest");
  dl->AddText(L.fBig, L.fBig->FontSize, ImVec2(nx + 1, cy - L.fBig->FontSize + 2), C_WHITE, "DokuQuest");
  std::string ver = "Alpha " + niceVersion(L.gameRunning ? L.gameVersion : L.updater.currentVersion()) + " · Launcher";
  dl->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(nx, cy + 6), C_DIM, ver.c_str());

  // right side, laid out from the right edge: [notice] [name] [Options/Folder] [Play/Server]
  float playW = 170, smallW = 140, nameW = 200;
  float xPlay = b.x - pad - playW, xOpts = xPlay - 16 - smallW, xName = xOpts - 16 - nameW;
  float top = cy - (52 + 6 + 28) * 0.5f;
  ImGui::SetCursorScreenPos(ImVec2(xPlay, top));
  const char* playLabel = L.gameRunning ? "Running…" : "Play";
  if (mcButton(L, playLabel, ImVec2(playW, 52), BtnKind::Go, !L.gameRunning, L.fBig)) L.play();
  if (L.tab == LTab::News && ImGui::GetFrameCount() < 3) ImGui::SetKeyboardFocusHere(-1);
  ImGui::SetCursorScreenPos(ImVec2(xPlay, top + 52 + 6));
  if (mcButton(L, serverRunning() ? "Server running" : "Dedicated Server", ImVec2(playW, 28), BtnKind::Grey, true, L.fSmall)) L.tab = LTab::Server;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run one of your worlds as a server for others to join");
  float stackTop = cy - (34 + 6 + 34) * 0.5f;
  ImGui::SetCursorScreenPos(ImVec2(xOpts, stackTop));
  if (mcButton(L, "Options...", ImVec2(smallW, 34), BtnKind::Grey, true, L.fUi)) L.tab = L.tab == LTab::Options ? LTab::News : LTab::Options;
  ImGui::SetCursorScreenPos(ImVec2(xOpts, stackTop + 40));
  if (mcButton(L, "Game folder", ImVec2(smallW, 34), BtnKind::Grey, true, L.fUi)) openPath(userDir());
  // player name
  ImGui::SetCursorScreenPos(ImVec2(xName, cy - 30));
  ImGui::PushFont(L.fSmall); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_DIM), "Player name"); ImGui::PopFont();
  ImGui::SetCursorScreenPos(ImVec2(xName, cy - 10));
  ImGui::SetNextItemWidth(nameW);
  ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(160, 160, 160, 255));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, (36 - L.fUi->FontSize) * 0.5f));
  static char name[24] = "";
  static bool inited = false;
  if (!inited) { std::snprintf(name, sizeof name, "%s", L.settings.playerName.c_str()); inited = true; }
  if (ImGui::InputText("##pname", name, 17, ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_EnterReturnsTrue, nameFilter)) L.play();
  if (ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemEdited() && std::string(name) != L.settings.playerName)) {
    std::string n = name; while (!n.empty() && n.back() == ' ') n.pop_back(); while (!n.empty() && n[0] == ' ') n.erase(0, 1);
    L.settings.playerName = n.empty() ? "Wanderer" : n;
    if (ImGui::IsItemDeactivatedAfterEdit()) L.saveSettings();
  }
  ImGui::PopStyleVar(); ImGui::PopStyleColor();
  // notice (green, clickable)
  std::string note = L.notice;
  if (note.empty() && L.gameRunning) note = "DokuQuest is running" + (L.worldPlaying.empty() ? std::string() : " · " + L.worldPlaying);
  if (!note.empty()) {
    ImFont* f = L.fSmall;
    float maxW = std::min(300.f, xName - 24 - (nx + 220));
    if (maxW > 80) {
      ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, maxW - 20, note.c_str());
      ImVec2 na(xName - 20 - ts.x - 20, cy - ts.y * 0.5f - 6), nb(xName - 20, cy + ts.y * 0.5f + 6);
      ImGui::SetCursorScreenPos(na);
      if (ImGui::InvisibleButton("##notice", ImVec2(nb.x - na.x, nb.y - na.y))) { if (L.noticeTab != LTab::Count) L.tab = L.noticeTab; else if (!L.gameRunning) L.setNotice(""); }
      dl->AddRectFilled(na, nb, L.gameRunning && L.notice.empty() ? IM_COL32(60, 70, 120, 217) : IM_COL32(79, 138, 47, 217), 3);
      dl->AddRect(na, nb, IM_COL32(0, 0, 0, 255), 3, 0, 2);
      dl->AddText(f, f->FontSize, ImVec2(na.x + 10, na.y + 6), C_WHITE, note.c_str(), nullptr, maxW - 20);
    }
  }
}

} // namespace dq
