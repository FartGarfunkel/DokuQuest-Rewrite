// launcher/server_dash.cpp — the running server's dashboard (game/server.js serverRenderRunning, serverRenderPlayers,
// serverRenderSettings, the Plugins tab's modsRender(…, 'plugin')): the header (name, what's going on, Save now, Stop
// server), the address to join, and the tabs. Everything that changes the world goes to the server as a console
// command (DedicatedServer::command), so it runs on the server thread and shows in the console like the original.
// Controller: LT / RT switch these tabs (LB / RB still switch the launcher's).
#include "launcher/server_ui.h"
#include "mod/modloader.h"
#include "sim/rules.h"
#include <imgui_stdlib.h>
#include <map>

namespace dq {

ServerDash& serverDash() { static ServerDash d; return d; }
const char* svTabId(SvTab t) {
  static const char* n[] = {"players", "map", "iso", "chat", "settings", "plugins"};
  return (int)t >= 0 && (int)t < (int)SvTab::Count ? n[(int)t] : "";
}

namespace {
const char* TAB_NAMES[] = {"Players", "World map", "Isometric", "Chat & console", "Settings", "Plugins"};

struct DashUi {
  std::string msgTo, msgText; bool openMsg = false;
  std::string motd; int maxp = 8; bool synced = false;     // the Settings fields (taken from the server when the tab opens)
  std::map<std::string, std::pair<bool, double>> pendingRule;   // clicked, until the server's copy agrees
  std::string pluginMsg;
};
DashUi& du() { static DashUi u; return u; }

std::string worldOf(const ViewPlayer& p) { return p.realm == REALM_POCKET ? "Pocket dimension" : p.realm != REALM_MAIN ? "Side realm" : dimName(p.dim); }

// ---- Players (serverRenderPlayers) ----
void drawPlayers(Launcher& L, DedicatedServer& srv, const ServerView* v) {
  DashUi& U = du();
  std::vector<ViewPlayer> ps = v ? v->players : std::vector<ViewPlayer>();
  if (ps.empty()) {
    ImGui::Spacing();
    dimText("Nobody yet. Friends join from Multiplayer: on this network the server shows up by itself; from elsewhere they type this computer's address and port (shown above).");
    return;
  }
  ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
  ImGui::PushStyleColor(ImGuiCol_TableRowBg, IM_COL32(0, 0, 0, 60)); ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, IM_COL32(255, 255, 255, 10));
  if (ImGui::BeginTable("##players", 6, tf)) {
    ImGui::TableSetupColumn("Name", 0, 1.2f); ImGui::TableSetupColumn("World", 0, 1.1f); ImGui::TableSetupColumn("Where", 0, 1.3f);
    ImGui::TableSetupColumn("Health", 0, 0.7f); ImGui::TableSetupColumn("Ping", 0, 0.5f); ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, 470);
    ImGui::TableHeadersRow();
    for (auto& p : ps) {
      ImGui::PushID(p.name.c_str());
      ImGui::TableNextRow(0, 34);
      ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(p.name.c_str());
      if (p.op) { ImGui::SameLine(); ImGui::PushFont(L.fSmall); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_GOLD), "op"); ImGui::PopFont(); }
      ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(worldOf(p).c_str());
      ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::Text("%d, %d, %d", (int)std::round(p.pos.x), (int)std::round(p.pos.y), (int)std::round(p.pos.z));
      ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
      if (p.dead) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_DIM), "down");
      else ImGui::Text("%g ♥", std::round(p.hp * 5) / 10);
      ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::Text("%u ms", p.ping);
      ImGui::TableNextColumn();
      float bh = ImGui::GetFrameHeight();
      if (mcButton(L, "Message", ImVec2(84, bh), BtnKind::Grey, true, L.fSmall)) { U.msgTo = p.name; U.msgText.clear(); U.openMsg = true; }
      ImGui::SameLine(0, 4);
      if (mcButton(L, "To spawn", ImVec2(84, bh), BtnKind::Grey, true, L.fSmall)) srv.command("tp " + p.name + " spawn");
      ImGui::SameLine(0, 4);
      if (mcButton(L, "Watch", ImVec2(70, bh), BtnKind::Grey, true, L.fSmall)) { serverDash().follow = p.name; serverDash().mapPan = ImVec2(0, 0); serverDash().tab = SvTab::Iso; }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("The isometric view round %s", p.name.c_str());
      ImGui::SameLine(0, 4);
      if (mcButton(L, p.op ? "Deop" : "Op", ImVec2(64, bh), BtnKind::Grey, true, L.fSmall)) srv.command((p.op ? "deop " : "op ") + p.name);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(p.op ? "Stop %s using commands" : "Let %s use commands", p.name.c_str());
      ImGui::SameLine(0, 4);
      if (mcButton(L, "Kick", ImVec2(64, bh), BtnKind::Grey, true, L.fSmall)) { srv.command("say " + p.name + " was asked to leave."); srv.command("kick " + p.name); }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleColor(2);
  // "Message to <name>" (the original's prompt())
  if (U.openMsg) { ImGui::OpenPopup("##svmsg"); U.openMsg = false; }
  ImGui::SetNextWindowSize(ImVec2(420, 0));
  if (ImGui::BeginPopupModal("##svmsg", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::Text("Message to %s", U.msgTo.c_str());
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    bool send = ImGui::InputText("##svmsgtext", &U.msgText, ImGuiInputTextFlags_EnterReturnsTrue);
    if (mcButton(L, "Send", ImVec2(120, 32), BtnKind::Go, !U.msgText.empty(), L.fUi)) send = true;
    ImGui::SameLine();
    bool cancel = mcButton(L, "Cancel", ImVec2(120, 32), BtnKind::Grey, true, L.fUi) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (send && !U.msgText.empty()) { srv.command("msg " + U.msgTo + " " + U.msgText); cancel = true; }
    if (cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

// ---- Settings (serverRenderSettings) ----
void drawSettings(Launcher& L, DedicatedServer& srv, const ServerStatus& st, const ServerView* v) {
  DashUi& U = du();
  if (!U.synced) { U.motd = st.motd.empty() ? srv.config().motd : st.motd; U.maxp = st.maxPlayers ? st.maxPlayers : srv.config().maxPlayers; U.synced = true; }
  float lw = 150;
  auto label = [&](const char* t) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(t); ImGui::SameLine(lw); };
  label("Welcome message"); ImGui::SetNextItemWidth(std::min(520.f, ImGui::GetContentRegionAvail().x));
  ImGui::InputText("##svmotd", &U.motd);
  if (ImGui::IsItemDeactivatedAfterEdit()) srv.command("motd " + U.motd);
  label("Players at once"); ImGui::SetNextItemWidth(260);
  ImGui::SliderInt("##svmax", &U.maxp, 1, 64);
  if (ImGui::IsItemDeactivatedAfterEdit()) srv.command("maxplayers " + std::to_string(U.maxp));
  ImGui::Spacing();
  float bh = ImGui::GetFrameHeight() + 4;
  label("Time of day");
  struct T { const char* n; double f; } times[] = {{"Morning", 0.02}, {"Noon", 0.25}, {"Evening", 0.48}, {"Night", 0.6}};
  for (auto& t : times) { if (mcButton(L, t.n, ImVec2(96, bh), BtnKind::Grey, true, L.fSmall)) srv.command(strFormat("time set %d", (int)std::round(DAY_LEN * t.f))); ImGui::SameLine(0, 6); }
  ImGui::PushFont(L.fSmall); ImGui::AlignTextToFramePadding(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_DIM), "now: %s, day %lld", v ? v->timeOfDay.c_str() : "…", v ? (long long)v->day : 0LL); ImGui::PopFont();
  label("Weather");
  struct Wt { const char* n; const char* k; } weathers[] = {{"Clear", "clear"}, {"Rain", "rain"}, {"Storm", "thunder"}};
  for (auto& w : weathers) { if (mcButton(L, w.n, ImVec2(96, bh), BtnKind::Grey, true, L.fSmall)) srv.command(std::string("weather ") + w.k); ImGui::SameLine(0, 6); }
  ImGui::PushFont(L.fSmall); ImGui::AlignTextToFramePadding();
  std::string wk = v ? (v->weather == "thunder" ? "storm" : v->weather) : "…";
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_DIM), "now: %s", wk.c_str()); ImGui::PopFont();
  ImGui::Spacing();
  label("Game rules");
  ImGui::BeginGroup();
  double now = L.time;
  for (const char* key : {"keepInventory", "mobSpawning", "haunting", "fireSpread"}) {
    std::string text = key;
    for (auto& r : ruleToggles()) if (r[0] == key) text = r[1];
    bool on = true;
    if (v && v->rules.is_object() && v->rules.contains(key) && v->rules[key].is_boolean()) on = v->rules[key].get<bool>();
    auto pend = U.pendingRule.find(key);
    if (pend != U.pendingRule.end()) { if (pend->second.first == on || now - pend->second.second > 3) U.pendingRule.erase(pend); else on = pend->second.first; }
    if (ImGui::Checkbox((text + "##" + key).c_str(), &on)) { srv.command(std::string("gamerule ") + key + (on ? " true" : " false")); U.pendingRule[key] = {on, now}; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", key);
  }
  ImGui::EndGroup();
  ImGui::Spacing();
  ImGui::PushFont(L.fSmall);
  dimText("Changes to the welcome message and the player limit apply at once. Rules are the world's own (the same as World settings). "
          "Port %d · %s · %s · PvP %s — these are set before the server starts.", st.port, srv.config().mode.c_str(), srv.config().difficulty.c_str(), srv.config().pvp ? "on" : "off");
  ImGui::PopFont();
  ImGui::Spacing();
  if (mcButton(L, "Open server folder", ImVec2(220, 32), BtnKind::Grey, true, L.fUi)) openPath(userSub("servers"));
}

// ---- Plugins (modsRender(g, body, 'plugin')) ----
void drawPlugins(Launcher& L, const ServerStatus& st) {
  DashUi& U = du();
  ImGui::PushFont(L.fSmall);
  dimText("Plugins run while this server hosts a world. A plugin is a .js file (or a mod folder) in the mods folder. They run with the game's full trust: only add ones you trust. "
          "Changes take effect the next time the server starts.");
  ImGui::PopFont();
  if (!st.pluginsOn) { ImGui::Spacing(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_GOLD), "Plugins are off for this server."); }
  ImGui::Spacing();
  int n = 0;
  if (L.mods) {
    std::string removeId;
    for (auto& m : L.mods->list()) {
      if (!m.plugin) continue;
      n++;
      ImGui::PushID(m.id.c_str());
      bool on = m.enabled;
      if (ImGui::Checkbox("##on", &on)) { L.mods->setEnabled(m.id, on); L.mods->saveState(); U.pluginMsg = "Saved. It takes effect the next time the server starts."; }
      ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(m.name.empty() ? m.id.c_str() : m.name.c_str());
      ImGui::SameLine(); ImGui::PushFont(L.fSmall); ImGui::AlignTextToFramePadding();
      const ServerPlugin* run = nullptr; for (auto& p : st.plugins) if (p.id == m.id) run = &p;
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_DIM), "%s%s", m.kind == "native" ? "native plugin" : m.kind == "data" ? "data pack" : "plugin",
                         run && run->loaded ? " · running" : run && run->enabled ? " · not running" : "");
      std::string err = run && !run->error.empty() ? run->error : m.error;
      if (!err.empty()) { ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_BAD), "%s", err.c_str()); }
      ImGui::PopFont();
      ImGui::SameLine();
      if (mcButton(L, "Remove", ImVec2(80, ImGui::GetFrameHeight()), BtnKind::Grey, true, L.fSmall)) removeId = m.id;
      ImGui::PopID();
    }
    if (!removeId.empty()) { L.mods->removeMod(removeId); U.pluginMsg = "Removed. The server keeps running it until it stops."; }
  }
  if (!n) dimText("None yet.");
  ImGui::Spacing();
  if (mcButton(L, "Open the mods folder", ImVec2(220, 34), BtnKind::Grey, true, L.fUi)) openPath(userSub("mods"));
  ImGui::SameLine();
  if (mcButton(L, "Save an example plugin", ImVec2(240, 34), BtnKind::Grey, L.mods != nullptr, L.fUi)) {
    fs::path tmp = fs::temp_directory_path() / "welcome-plugin.js";
    std::string err;
    if (writeFileAtomic(tmp, ModLoader::exampleMod()) && L.mods->addMod(tmp, true, &err)) U.pluginMsg = "Added an example plugin to the mods folder. It runs the next time the server starts.";
    else U.pluginMsg = err.empty() ? "Could not save the example." : err;
    std::error_code ec; fs::remove(tmp, ec);
  }
  ImGui::SameLine();
  if (mcButton(L, "Look again", ImVec2(140, 34), BtnKind::Grey, L.mods != nullptr, L.fUi)) { L.mods->scan(); U.pluginMsg.clear(); }
  if (!U.pluginMsg.empty()) { ImGui::PushFont(L.fSmall); dimText("%s", U.pluginMsg.c_str()); ImGui::PopFont(); }
}
} // namespace

void drawServerDashboard(Launcher& L, DedicatedServer& srv, const ServerStatus& st, double up, bool& stop) {
  ServerDash& D = serverDash();
  // what the map / isometric view need from the server thread
  ServerViewReq req; req.map = D.tab == SvTab::Map; req.iso = D.tab == SvTab::Iso; req.follow = D.follow; req.isoR = D.isoR;
  srv.setView(req);
  auto vp = srv.view();
  const ServerConfig& c = srv.config();

  beginDokuPanel("##svdash", ImGui::GetContentRegionAvail(), false, ImGuiWindowFlags_NavFlattened);
  // header: the server's name, what's going on, Save now / Stop server
  float bw = 150, bh = 40, right = ImGui::GetContentRegionAvail().x;
  ImVec2 p0 = ImGui::GetCursorPos();
  ImGui::PushFont(L.fMid);
  shadowText(ImGui::GetWindowDrawList(), L.fMid, ImGui::GetCursorScreenPos(), C_WHITE, IM_COL32(63, 63, 63, 255), 2, (c.name.empty() ? c.world : c.name).c_str());
  ImGui::Dummy(ImVec2(0, L.fMid->FontSize + 4)); ImGui::PopFont();
  int u = (int)up;
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(C_DIM));
  ImGui::Text("%s · %d of %d playing · up %dh %dm · %.1f ticks/s (%.1f ms) · day %lld", c.world.c_str(), st.players, st.maxPlayers ? st.maxPlayers : c.maxPlayers,
              u / 3600, u / 60 % 60, st.tps, st.mspt, (long long)st.day);
  ImGui::PopStyleColor();
  // the address to join (sv-addr)
  ImGui::PushFont(L.fSmall);
  std::string addr = (st.address.empty() ? std::string("127.0.0.1") : st.address) + ":" + std::to_string(st.port ? st.port : c.port);
  ImGui::AlignTextToFramePadding(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_DIM), "Join at"); ImGui::SameLine();
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_FROST), "%s", addr.c_str()); ImGui::SameLine();
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(C_DIM), "%s", c.lan ? "· on this network it shows up in Multiplayer by itself · from the internet the router must let UDP in on that port"
                                                                         : "· from the internet the router must let UDP in on that port");
  ImGui::PopFont();
  ImVec2 after = ImGui::GetCursorPos();
  ImGui::SetCursorPos(ImVec2(p0.x + right - bw * 2 - 8, p0.y));
  if (mcButton(L, "Save now", ImVec2(bw, bh), BtnKind::Grey, true, L.fUi)) srv.command("save");
  ImGui::SameLine(0, 8);
  if (mcButton(L, "Stop server", ImVec2(bw, bh), BtnKind::Grey, true, L.fUi)) stop = true;
  ImGui::SetCursorPos(after);
  ImGui::Spacing();
  // the tabs (LT / RT on a controller)
  bool typing = ImGui::GetIO().WantTextInput;
  if (!typing && ImGui::IsKeyPressed(ImGuiKey_GamepadL2)) D.tab = (SvTab)(((int)D.tab + (int)SvTab::Count - 1) % (int)SvTab::Count);
  if (!typing && ImGui::IsKeyPressed(ImGuiKey_GamepadR2)) D.tab = (SvTab)(((int)D.tab + 1) % (int)SvTab::Count);
  for (int i = 0; i < (int)SvTab::Count; i++) {
    if (i) ImGui::SameLine(0, 6);
    if (tabButton(L, TAB_NAMES[i], (int)D.tab == i)) D.tab = (SvTab)i;
  }
  if (D.tab != SvTab::Settings) du().synced = false;
  ImGui::Spacing();
  ImVec2 body = ImGui::GetContentRegionAvail();
  switch (D.tab) {
    case SvTab::Players: ImGui::BeginChild("##svbody", body, 0, ImGuiWindowFlags_NavFlattened); drawPlayers(L, srv, vp.get()); ImGui::EndChild(); break;
    case SvTab::Map: drawServerMap(L, srv, body); break;
    case SvTab::Iso: drawServerIso(L, srv, body); break;
    case SvTab::Chat: drawServerConsole(L, &srv, body, false); break;
    case SvTab::Settings: ImGui::BeginChild("##svbody", body, 0, ImGuiWindowFlags_NavFlattened); drawSettings(L, srv, st, vp.get()); ImGui::EndChild(); break;
    case SvTab::Plugins: ImGui::BeginChild("##svbody", body, 0, ImGuiWindowFlags_NavFlattened); drawPlugins(L, st); ImGui::EndChild(); break;
    default: break;
  }
  endDokuPanel();
}

} // namespace dq
