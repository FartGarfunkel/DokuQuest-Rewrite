// ui/menus.cpp — the pause menu ("Game Menu", ui.js pauseInfo), How to play, What's new
// and the "coming soon" panel used by entries whose workstream hasn't landed (skin editor, creator, LAN).
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "client/netplay.h"
#include "ui/char_preview.h"
#include <imgui.h>

namespace dq {
using namespace ui;

bool backupWorldFolder(const fs::path& folder, const std::string& name, fs::path& out, std::string& err);   // worlds.cpp
std::string agoText(i64 ms);
std::unique_ptr<Screen> makePhotoModeScreen();   // ui/photo_mode.cpp                                                                                  // worlds.cpp

// ---- pause ---------------------------------------------------------------------------------------------
class PauseScreen : public Screen {
public:
  const char* id() const override { return "pause"; }
  std::string info;
  bool focusPending = true;
  CharPreview headView, headViews[3];
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    float z = Z(), W = (float)a.width, H = (float)a.height;
    ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), IM_COL32(16, 16, 16, 192), IM_COL32(16, 16, 16, 192), IM_COL32(16, 16, 16, 208), IM_COL32(16, 16, 16, 208));   // BTA's GuiScreen gradient
    float bw = std::min(400.f, 0.9f * W / z) * z, bh = 40 * z, gap = 8 * z, half = (bw - gap) * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(W, H - 32 * z));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    padScrollHere();
    // .mc-title
    { ImVec2 ts = textSize(F_BIG, "Game Menu"); ImVec2 p = ImGui::GetCursorScreenPos();
      textShadow(dl, F_BIG, ImVec2(p.x + (bw - ts.x) * 0.5f, p.y), col::white, "Game Menu", ImVec2(2, 2) * z, col::rgb(0x3f3f3f)); ImGui::Dummy(ImVec2(bw, ts.y + 14 * z)); }
    if (focusPending && a.input.padCount()) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
    if (a.input.padCount() > 0) focusPending = false;   // a pad picked up later still gets its highlight
    Level* lv = a.session ? a.session->level.get() : nullptr;
    Player* p = a.session ? a.session->player : nullptr;
    bool creative = p && p->creative();
    if (mcButton("Back to Game", ImVec2(bw, bh))) { a.popScreen(); ImGui::End(); ImGui::PopStyleVar(); return; }
    if (mcButton("Options...", ImVec2(half, bh))) a.pushScreen(makeOptionsScreen());
    ImGui::SameLine(); if (mcButton("Players & Skins", ImVec2(half, bh))) a.pushScreen(makePlayersScreen());
    if (mcButton("Photo Mode...", ImVec2(bw, bh), !p || p->dead || a.session->split())) {   // BTA: the camera flies free (ui/photo_mode.cpp)
      a.popScreen(); a.pushScreen(makePhotoModeScreen()); ImGui::End(); ImGui::PopStyleVar(); return;
    }
    bool guest = netIsGuest(a);
    // (netui.js) a guest has no world settings, backups, mode switch or LAN of its own
    if (mcButton("How to Play", ImVec2(guest ? bw : half, bh))) a.pushScreen(makeHowToScreen());
    if (!guest) { ImGui::SameLine(); if (mcButton("World Settings", ImVec2(half, bh))) a.pushScreen(makeWorldSettingsScreen({})); }
    if (!guest) {
      if (mcButton(creative ? "Switch to Survival##mode" : "Switch to Creative##mode", ImVec2(half, bh)) && lv && p) {
        lv->info.mode = p->mode = creative ? "survival" : "creative";
        if (!p->creative()) p->flying = false;
        a.toast(p->creative() ? "Creative mode: every block in the inventory, double-tap jump to fly" : "Survival mode", "good");
      }
      ImGui::SameLine();
      if (mcButton("Back Up World", ImVec2(half, bh)) && lv) {
        a.session->save();
        fs::path out; std::string err;
        if (backupWorldFolder(lv->folder, lv->info.name, out, err)) { info = "Backup saved: " + out.filename().string(); a.toast("Backup saved", "good"); }
        else { info = "Backup failed: " + err; a.toast(info, "warn"); }
      }
      LanStatus ls = lanStatus(a);
      std::string lanLabel = ls.open ? "LAN: " + (ls.players.empty() ? std::string("open, nobody yet") : std::to_string(ls.players.size()) + " joined") + " \xC2\xB7 " + ls.address + "##lan" : "Open to LAN##lan";
      if (mcButton(lanLabel.c_str(), ImVec2(bw, bh))) a.pushScreen(makeLanHostScreen());
    }
    std::string quit = guest ? "Leave " + (lv ? lv->info.name : std::string("the world")) + "##quit" : "Save and Quit to Title##quit";
    if (mcButton(quit.c_str(), ImVec2(bw, bh))) uiDefer([](App& app) { app.leaveWorld(true); });
    // .pause-party: who is playing on this screen (ui.js ~305: one row per local player, Leave for the partners, how to join)
    ImGui::Dummy(ImVec2(1, 6 * z));
    int leaveIdx = -1;
    {
      ImVec2 p0 = ImGui::GetCursorScreenPos();
      float rowH = 30 * z;
      GameSession* gs = a.session.get();
      int rows = gs ? (int)gs->locals.size() : 1;
      const char* hint = rows >= 4 ? "Four players is the most that fit on one screen."
                       : a.input.padCount() > (rows > 1 ? rows - 1 : 0) ? "A free controller is connected \xE2\x80\x94 press Start on it to join."
                       : "Split-screen: plug in another controller and press Start on it to join.";
      float hintH = textSize(F_MID, hint, bw - 20 * z).y;
      float boxH = 8 * z + textSize(F_MID, "On this screen").y + 6 * z + rows * (rowH + 6 * z) + hintH + 8 * z;
      dl->AddRectFilled(p0, p0 + ImVec2(bw, boxH), IM_COL32(0, 0, 0, 115));
      dl->AddRect(p0, p0 + ImVec2(bw, boxH), col::black, 0, 0, 2 * z);
      float y = p0.y + 8 * z;
      text(dl, F_MID, ImVec2(p0.x + 10 * z, y), col::muted, "On this screen"); y += textSize(F_MID, "On").y + 6 * z;
      static const u32 PCOL[4] = {0xffb07a, 0x8fe3ff, 0xb6f06a, 0xe7a6ff};   // .phead.p1 .. p4
      for (int r = 0; r < rows; r++) {
        Player* rp = gs ? gs->locals[r].player : p;
        ImVec2 head(p0.x + 10 * z, y);
        // charparts.js drawSkinHead: your head as a little 3D cube seen from above and to one side (here the real
        // voxel head: a 3D character's hair, ears, horns and hat too)
        if (rp) {
          auto spec = resolveCharacter(rp->skin);
          CharPreviewOpts ho; ho.headOnly = true; ho.yaw = PI - 0.62f; ho.elev = 0.48f; ho.dist = 30; ho.time = (float)a.time;
          CharPose hp; hp.still = true; ho.pose = &hp;
          float fb = ImGui::GetIO().DisplayFramebufferScale.x, hs = rowH * 1.25f;
          GLuint tex = (r == 0 ? headView : headViews[std::min(r, 3) - 1]).render(*spec, (int)(hs * fb), (int)(hs * fb), ho);
          if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, head + ImVec2(-rowH * 0.12f, -rowH * 0.14f), head + ImVec2(-rowH * 0.12f, -rowH * 0.14f) + ImVec2(hs, hs), ImVec2(0, 1), ImVec2(1, 0));
        }
        dl->AddRectFilled(head + ImVec2(0, rowH - 2 * z), head + ImVec2(rowH, rowH), col::rgb(PCOL[r & 3]), 2 * z);
        text(dl, F_BODY, ImVec2(head.x + rowH + 10 * z, y + (rowH - textSize(F_BODY, "A").y) * 0.5f), col::rgb(PCOL[r & 3]), rp ? rp->name.c_str() : "");
        std::string dev;
        if (!gs || !gs->split()) dev = std::string("Keyboard & mouse") + (a.input.padCount() ? " + Controller" : "");
        else { const InputSource& src = gs->locals[r].src; dev = src.kb ? "Keyboard & mouse" : ""; if (!src.pads.empty()) dev += std::string(src.kb ? " + " : "") + "Controller"; }
        ImVec2 ds = textSize(F_SMALL, dev.c_str());
        float right = p0.x + bw - 10 * z;
        if (r > 0) {   // a partner can leave
          float lw = textSize(F_SMALL, "Leave").x + 22 * z;
          ImGui::SetCursorScreenPos(ImVec2(right - lw, y + (rowH - 24 * z) * 0.5f));
          if (button(("Leave##party" + std::to_string(r)).c_str(), BTN_NORMAL, true, lw)) leaveIdx = r;
          right -= lw + 10 * z;
        }
        text(dl, F_SMALL, ImVec2(right - ds.x, y + (rowH - ds.y) * 0.5f), col::muted, dev.c_str());
        y += rowH + 6 * z;
      }
      text(dl, F_MID, ImVec2(p0.x + 10 * z, y), col::rgb(0xd8d8d8), hint, bw - 20 * z);
      ImGui::SetCursorScreenPos(p0);
      ImGui::Dummy(ImVec2(bw, boxH));
    }
    if (leaveIdx > 0 && a.session) a.session->removeLocal(leaveIdx);
    // .mc-hint: name · dimension · mode · last saved
    if (lv && p) {
      bool guest = netIsGuest(a);
      std::string s = lv->info.name + (guest ? " \xC2\xB7 hosted by someone" : "") + " \xC2\xB7 " + dimName(dimOfY(ifloor(p->pos.y))) + " \xC2\xB7 " + (p->creative() ? "Creative" : "Survival");
      if (lv->info.lastPlayed && !guest) s += " \xC2\xB7 Last saved " + agoText(lv->info.lastPlayed) + ".";
      if (!info.empty()) s = info;
      ImVec2 ts = textSize(F_MID, s.c_str(), bw); ImVec2 q = ImGui::GetCursorScreenPos();
      textShadow(dl, F_MID, ImVec2(q.x + std::max(0.f, (bw - ts.x) * 0.5f), q.y + 4 * z), col::rgb(0xd8d8d8), s.c_str(), ImVec2(1, 1) * z, col::black, bw);
      ImGui::Dummy(ImVec2(bw, ts.y + 8 * z));
    }
    ImGui::End();
    ImGui::PopStyleVar();
  }
};
std::unique_ptr<Screen> makePauseScreen() { return std::make_unique<PauseScreen>(); }

// ---- simple text panels -------------------------------------------------------------------------------
namespace {
struct Block { int kind; std::string a, b; };   // 0 h3, 1 paragraph, 2 key row (a = keys, b = what), 3 numbered
class TextPanel : public Screen {
public:
  TextPanel(const char* id, std::string t, std::string close, float w, std::vector<std::vector<Block>> cols) : sid(id), title(std::move(t)), closeLbl(std::move(close)), width(w), columns(std::move(cols)) {}
  const char* sid; std::string title, closeLbl; float width; std::vector<std::vector<Block>> columns;
  const char* id() const override { return sid; }
  bool pausesGame() const override { return true; }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    Panel pn;
    pn.begin((std::string("##") + sid).c_str(), title.c_str(), width, closeLbl.c_str());
    pn.beginBody();
    float z = Z(), pad = 16 * z, gapc = 20 * z;
    int nc = (int)columns.size();
    if (pn.width < 560 * z) nc = 1;   // one column on narrow screens
    float colW = (pn.width - pad * 2 - gapc * (nc - 1)) / nc;
    ImGui::SetCursorPos(ImVec2(pad, pad));
    std::vector<Block> flat; std::vector<std::vector<Block>> cols = columns;
    if (nc == 1 && columns.size() > 1) { for (auto& c : columns) flat.insert(flat.end(), c.begin(), c.end()); cols = {flat}; }
    float top = ImGui::GetCursorPosY(), maxY = top;
    for (int c = 0; c < (int)cols.size(); c++) {
      float x = pad + c * (colW + gapc);
      ImGui::SetCursorPos(ImVec2(x, top));
      ImGui::BeginGroup();
      int num = 0;
      for (auto& b : cols[c]) {
        if (b.kind == 0) { ImGui::Dummy(ImVec2(1, 2 * z)); label(F_H3, col::text, b.a.c_str()); num = 0; }
        else if (b.kind == 1) { label(F_BODY, col::muted, b.a.c_str(), colW); ImGui::Dummy(ImVec2(1, 4 * z)); }
        else if (b.kind == 2) {
          float kw = 130 * z; ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
          float h1 = textSize(F_MID, b.a.c_str(), kw).y, h2 = textSize(F_MID, b.b.c_str(), colW - kw - 12 * z).y;
          text(dl, F_MID, p, col::ember, b.a.c_str(), kw); text(dl, F_MID, ImVec2(p.x + kw + 12 * z, p.y), col::muted, b.b.c_str(), colW - kw - 12 * z);
          ImGui::Dummy(ImVec2(colW, std::max(h1, h2) + 2 * z));
        } else {
          std::string n = std::to_string(++num) + ".";
          ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
          text(dl, F_BODY, p, col::muted, n.c_str());
          float hh = textSize(F_BODY, b.a.c_str(), colW - 22 * z).y;
          text(dl, F_BODY, ImVec2(p.x + 22 * z, p.y), col::muted, b.a.c_str(), colW - 22 * z);
          ImGui::Dummy(ImVec2(colW, hh + 4 * z));
        }
      }
      ImGui::EndGroup();
      maxY = std::max(maxY, ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y + ImGui::GetScrollY());
    }
    ImGui::SetCursorPos(ImVec2(0, maxY)); ImGui::Dummy(ImVec2(1, pad));
    pn.endBody();
    pn.end();
    if (pn.closed) a.popScreen();
  }
};
}

// makeHowToScreen: the guidebook in ui/help_book.cpp

std::unique_ptr<Screen> makeNewsScreen() {
  // the same notes the launcher shows: data/news.json [{version, date, title, items[]}] when it is there
  std::vector<Block> b;
  if (auto j = readJson(dataDir() / "news.json"); j && j->is_array()) {
    for (auto& e : *j) {
      b.push_back({0, e.value("version", "") + (e.contains("date") ? "  " + e.value("date", "") : ""), ""});
      if (e.contains("title")) b.push_back({1, e.value("title", ""), ""});
      if (e.contains("items")) for (auto& it : e["items"]) if (it.is_string()) b.push_back({2, "\xC2\xB7", it.get<std::string>()});
    }
  }
  if (b.empty()) b = {{0, std::string("DokuQuest ") + DQ_VERSION, ""}, {1, "The patch notes for this version are in the launcher's News tab.", ""}};
  return std::make_unique<TextPanel>("news", "What's new", "Close", 860, std::vector<std::vector<Block>>{b});
}

std::unique_ptr<Screen> makeComingSoonScreen(const std::string& title, const std::string& txt) {
  return std::make_unique<TextPanel>("comingsoon", title, "Back", 460, std::vector<std::vector<Block>>{{{0, "Coming soon", ""}, {1, txt, ""}}});
}

// ---- players & skins: ui/players_screen.cpp


} // namespace dq
