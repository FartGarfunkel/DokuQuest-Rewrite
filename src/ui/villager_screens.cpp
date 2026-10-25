// ui/villager_screens.cpp — the conversation and the shop (villagers.js NpcTalk, NpcShop), the quest board
// (QuestBoard) and the paths window (paths.js PathUI). The dialogue itself is worked out in sim (sim/villagers.h
// Talk): these draw it the way styles.css drew .npc-talk, .npc-shop, .quest-board and .path-view.
#include "ui/villager_screens.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "client/app.h"
#include "client/session.h"
#include "render/content_draw.h"
#include "render/gl.h"
#include "sim/villagers.h"
#include "sim/magic_data.h"
#include "sim/rpg.h"
#include "core/registry.h"
#include <imgui.h>

namespace dq {

using namespace ui;
namespace {
GameSession* sess(App& a) { return a.session && a.session->player ? a.session.get() : nullptr; }
constexpr ImGuiWindowFlags CARD = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                                  ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
// the cards' wood: a gradient body, a bevelled border, a black outline (.nt-card / .qb-card)
void woodCard(ImDrawList* dl, ImVec2 a, ImVec2 b, u32 top, u32 bot, u32 hi, u32 lo) {
  float z = Z();
  dl->AddRectFilled(a - ImVec2(2, 2) * z, b + ImVec2(2, 2) * z, col::black);
  dl->AddRectFilledMultiColor(a, b, col::rgb(top), col::rgb(top), col::rgb(bot), col::rgb(bot));
  bevel(dl, a, b, col::rgb(hi), col::rgb(lo), 3 * z);
}
// a row/option the pad can select: returns true when clicked (or A pressed on it)
bool rowButton(const char* id, ImVec2 size, bool sel, bool pressedA, u32 border = 0x5a4430) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool c = ImGui::InvisibleButton(id, size);
  bool hot = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, p + size, (hot || sel) ? IM_COL32(255, 220, 140, 31) : IM_COL32(0, 0, 0, 71));
  dl->AddRect(p, p + size, (hot || sel) ? col::gold : col::rgb(border), 0, 0, 1 * Z());
  return c || (sel && pressedA);
}
void outlineLast(bool on, ImU32 c = col::gold) { if (on) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin() - ImVec2(2, 2) * Z(), ImGui::GetItemRectMax() + ImVec2(2, 2) * Z(), c, 0, 0, 2 * Z()); }
int padStep(App& a, int sel, int n, bool horizontalToo) {
  if (n <= 0) return 0;
  Input& in = a.input;
  if (in.pressed(Act::MenuDown) || (horizontalToo && in.pressed(Act::MenuRight))) sel = (sel + 1) % n;
  if (in.pressed(Act::MenuUp) || (horizontalToo && in.pressed(Act::MenuLeft))) sel = (sel + n - 1) % n;
  return clampv(sel, 0, n - 1);
}
int digitPressed() { for (int i = 0; i < 9; i++) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) return i; return -1; }
}

// ------------------------------------------------------------------ the conversation
class TalkScreen : public Screen {
public:
  TalkScreen(u32 npc, bool confront, std::string victim, bool shop) : npc(npc), confront(confront), victim(std::move(victim)), startShop(shop) {}
  ~TalkScreen() override { if (face) glDeleteTextures(1, &face); }
  const char* id() const override { return "npc-talk"; }
  bool pausesGame() const override { return false; }
  void onOpen(App& a) override {
    GameSession* s = sess(a);
    auto* c = s ? dynamic_cast<Creature*>(s->level->entity(npc)) : nullptr;
    if (!c || !talkOpen(*s->level, *s->player, *c, t, confront, victim)) { failed = true; return; }
    if (startShop && hasShop(t.job)) for (int i = 0; i < (int)t.opts.size(); i++) if (t.opts[i].act == "shop") { talkChoose(*s->level, *s->player, t, i); break; }
    // the face, big (villagers.js npcPortrait): the head's front with its hat layer over it
    std::vector<u8> px; npcSkinPixels(t.seed, t.job, px);
    u8 img[8 * 8 * 4];
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
      const u8* b = &px[((8 + y) * 64 + 8 + x) * 4]; const u8* o = &px[((8 + y) * 64 + 40 + x) * 4];
      u8* d = &img[(y * 8 + x) * 4];
      for (int k = 0; k < 4; k++) d[k] = o[3] ? o[k] : b[k];
    }
    face = makeTexRGBA(8, 8, img, true, false);
  }
  void onClose(App& a) override { if (GameSession* s = sess(a)) talkClosed(*s->level, *s->player, t); }
  bool onBack(App& a) override {
    if (t.mode == "shop") { if (GameSession* s = sess(a)) talkShopBack(*s->level, *s->player, t); sel = 0; return true; }
    return false;
  }
  void update(App& a, double dt) override {
    GameSession* s = sess(a);
    if (failed || !s) { a.popScreen(); return; }
    Level& lv = *s->level; Player& p = *s->player;
    if (lines != t.voiceLines) { lines = t.voiceLines; shown = 0; }
    shown = std::min((float)t.text.size(), shown + (float)dt * 26);
    talkHold(lv, p, t);
    if (!talkStillOk(lv, p, t) || a.input.pressed(Act::Inventory) || a.input.pressed(Act::MenuY)) { a.popScreen(); return; }
    if (t.closeNow) {
      std::string next = t.openScreen, hint = t.pathHint;
      if (next == "path") uiDefer([hint](App& app) { app.pushScreen(makePathScreen(hint)); });
      a.popScreen();
      return;
    }
    Input& in = a.input;
    if (t.mode == "shop") {
      auto L = rows(lv, p);
      sel = padStep(a, sel, (int)L.size(), false);
      if (in.pressed(Act::MenuLeft) || in.pressed(Act::MenuRight) || in.pressed(Act::MenuTabLeft) || in.pressed(Act::MenuTabRight)) { t.tab = t.tab == "buy" ? "sell" : "buy"; sel = 0; }
      if (in.pressed(Act::MenuOk) && !L.empty()) talkShopTrade(lv, p, t, sel);
    } else {
      sel = padStep(a, sel, (int)t.opts.size(), false);
      int pick = in.pressed(Act::MenuOk) ? sel : digitPressed();
      if (pick >= 0 && pick < (int)t.opts.size()) { choose(lv, p, pick); }
    }
  }
  void draw(App& a) override {
    GameSession* s = sess(a);
    if (failed || !s) return;
    Level& lv = *s->level; Player& p = *s->player;
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilledMultiColor(ImVec2(0, H * 0.45f), ImVec2(W, H), 0, 0, IM_COL32(6, 4, 2, 115), IM_COL32(6, 4, 2, 115));
    const float cw = std::min(640 * z, W - 24 * z), pad = 12 * z;
    // ---- the card: face, name, words, options ----
    float optH = 30 * z, textH = std::max(44 * z, textSize(F_BODY, t.text.c_str(), cw - 76 * z - pad * 3).y);
    int nOpts = t.mode == "shop" ? 0 : (int)t.opts.size();
    float ch = pad * 2 + std::max(80 * z, 30 * z + textH + 14 * z + nOpts * (optH + 4 * z));
    ImVec2 c0((W - cw) / 2, H - 92 * z - ch);
    ImGui::SetNextWindowPos(c0); ImGui::SetNextWindowSize(ImVec2(cw, ch));
    ImGui::Begin("##npc-talk", nullptr, CARD);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    woodCard(dl, c0, c0 + ImVec2(cw, ch), 0x3e2e1e, 0x2e2216, 0x7a5a38, 0x1a0f06);
    ImVec2 fp = c0 + ImVec2(pad, pad);
    dl->AddRectFilled(fp, fp + ImVec2(76, 76) * z, col::rgb(0x1a120a)); dl->AddRect(fp, fp + ImVec2(76, 76) * z, col::rgb(0x1a0f06), 0, 0, 2 * z);
    dl->AddRect(fp + ImVec2(2, 2) * z, fp + ImVec2(74, 74) * z, col::rgb(0x7a5a38), 0, 0, 2 * z);
    if (face) dl->AddImage((ImTextureID)(intptr_t)face, fp + ImVec2(2, 2) * z, fp + ImVec2(74, 74) * z);
    float mx = fp.x + 76 * z + pad, mw = c0.x + cw - pad - mx;
    ImVec2 nm = textSize(F_H3, t.name.c_str());
    textShadow(dl, F_H3, ImVec2(mx, fp.y - 4 * z), col::rgb(0xfff3d6), t.name.c_str(), ImVec2(2, 2) * z, col::rgb(0x1a0f06));
    text(dl, F_MID, ImVec2(mx + nm.x + 8 * z, fp.y + nm.y - textSize(F_MID, "A").y - 6 * z), col::gold, t.title.c_str());
    if (!t.vid.empty()) { std::string st = standingOf(repOf(p, t.vid)).name; ImVec2 sz = textSize(F_SMALL, st.c_str()); text(dl, F_SMALL, ImVec2(mx + mw - sz.x, fp.y + 4 * z), col::muted, st.c_str()); }
    std::string typed = t.text.substr(0, (size_t)shown);
    text(dl, F_BODY, ImVec2(mx, fp.y + 30 * z), col::text, typed.c_str(), mw);
    if (t.mode != "shop") {
      ImGui::SetCursorScreenPos(ImVec2(mx, fp.y + 30 * z + textH + 10 * z));
      for (int i = 0; i < (int)t.opts.size(); i++) {
        const TalkOpt o = t.opts[i];   // a copy: choosing changes the options
        ImGui::PushID(i);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (rowButton("##o", ImVec2(mw, optH), sel == i, false)) choose(lv, p, i);
        std::string icon = o.icon.empty() ? std::to_string(i + 1) : o.icon;
        text(dl, F_BIG, p0 + ImVec2(8 * z, (optH - textSize(F_BIG, "1").y) / 2), col::gold, icon.c_str());
        text(dl, F_MID, p0 + ImVec2(30 * z, (optH - textSize(F_MID, "A").y) / 2), col::text, o.label.c_str());
        ImGui::Dummy(ImVec2(1, 4 * z));
        ImGui::PopID();
        if (i >= (int)t.opts.size()) break;
      }
    }
    ImGui::End();
    if (t.mode == "shop") drawShop(a, lv, p, c0, cw);
  }

private:
  u32 npc; bool confront; std::string victim; bool startShop;
  Talk t; bool failed = false; int sel = 0, lines = -1; float shown = 0; GLuint face = 0;
  void choose(Level& lv, Player& p, int i) { talkChoose(lv, p, t, i); sel = 0; }
  std::vector<ShopLine> rows(Level& lv, Player& p) { VillageRef v = villageById(lv, t.vid); return v ? shopLines(lv, p, *v, t.idx, t.job, t.tab) : std::vector<ShopLine>{}; }
  void drawShop(App& a, Level& lv, Player& p, ImVec2 card, float cw) {
    const float z = Z(), pad = 8 * z, rowH = 34 * z;
    auto L = rows(lv, p);
    float listH = std::min((float)L.size() * (rowH + 3 * z) + 4 * z, std::min(380 * z, a.height * 0.52f) - 80 * z);
    if (L.empty()) listH = 30 * z;
    float sh = pad * 2 + 34 * z + listH + 24 * z;
    ImVec2 s0(card.x, card.y - 6 * z - sh);
    ImGui::SetNextWindowPos(s0); ImGui::SetNextWindowSize(ImVec2(cw, sh));
    ImGui::Begin("##npc-shop", nullptr, CARD);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    woodCard(dl, s0, s0 + ImVec2(cw, sh), 0x3e2e1e, 0x2e2216, 0x7a5a38, 0x1a0f06);
    ImGui::SetCursorScreenPos(s0 + ImVec2(pad, pad));
    if (button("Buy##tab", BTN_NORMAL, true)) { t.tab = "buy"; sel = 0; }
    outlineLast(t.tab == "buy");
    ImGui::SameLine();
    if (button("Sell##tab", BTN_NORMAL, true)) { t.tab = "sell"; sel = 0; }
    outlineLast(t.tab == "sell");
    int em = 0; for (auto& st : p.inv) if (st.id == "emerald") em += st.n;
    std::string ems = std::to_string(em);
    float bw = textSize(F_MID, "Back").x + 24 * z, ex = s0.x + cw - pad - bw - 8 * z - textSize(F_BIG, ems.c_str()).x;
    drawIcon(dl, "emerald", ImVec2(ex - 26 * z, s0.y + pad + 4 * z), 22 * z);
    text(dl, F_BIG, ImVec2(ex, s0.y + pad + 4 * z), col::rgb(0x9ff0a0), ems.c_str());
    ImGui::SameLine(cw - pad - bw);
    if (button("Back##shop", BTN_NORMAL, true, bw)) { talkShopBack(lv, p, t); sel = 0; ImGui::End(); return; }
    ImGui::SetCursorScreenPos(s0 + ImVec2(pad, pad + 36 * z));
    ImGui::BeginChild("##rows", ImVec2(cw - pad * 2, listH), false, ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoBackground);
    if (L.empty()) label(F_MID, col::muted, "Nothing today.");
    const json& TN = VD()["tierName"];
    for (int k = 0; k < (int)L.size(); k++) {
      const ShopLine& l = L[k];
      ImGui::PushID(k);
      ImVec2 p0 = ImGui::GetCursorScreenPos(); float rw = ImGui::GetContentRegionAvail().x;
      bool click = rowButton("##r", ImVec2(rw, rowH), sel == k, false);
      if (sel == k) ImGui::SetScrollHereY();
      ImDrawList* d2 = ImGui::GetWindowDrawList();
      float al = l.locked ? 0.4f : l.can ? 1.f : 0.55f, ic = 22 * z, ty = p0.y + (rowH - textSize(F_BIG, "1").y) / 2, my = p0.y + (rowH - textSize(F_MID, "A").y) / 2;
      bool buy = t.tab == "buy";
      std::string giveN = std::to_string(buy ? l.price : l.n), getN = std::to_string(buy ? l.n : l.price);
      float x = p0.x + 8 * z;
      drawIcon(d2, buy ? "emerald" : l.id, ImVec2(x, p0.y + (rowH - ic) / 2), ic, al); x += ic + 4 * z;
      text(d2, F_BIG, ImVec2(x, ty), withAlpha(col::rgb(0xfff3d6), al), giveN.c_str()); x = p0.x + 80 * z;
      text(d2, F_MID, ImVec2(x, my), withAlpha(col::muted, al), "\xE2\x86\x92"); x += 20 * z;
      drawIcon(d2, buy ? l.id : "emerald", ImVec2(x, p0.y + (rowH - ic) / 2), ic, al); x += ic + 4 * z;
      text(d2, F_BIG, ImVec2(x, ty), withAlpha(col::rgb(0xfff3d6), al), getN.c_str()); x += textSize(F_BIG, getN.c_str()).x + 6 * z;
      std::string nm = buy ? itemNameOf(l.id) : "for " + itemNameOf(l.id);
      text(d2, F_MID, ImVec2(x, my), withAlpha(col::text, al), nm.c_str());
      std::string right = l.locked ? TN[l.tier].get<std::string>() + " only" : l.left > 0 ? std::to_string(l.left) + " left" : "sold out";
      ImVec2 rs = textSize(F_SMALL, right.c_str());
      text(d2, F_SMALL, ImVec2(p0.x + rw - rs.x - 8 * z, p0.y + (rowH - rs.y) / 2), withAlpha(col::muted, al), right.c_str());
      if (click) { sel = k; talkShopTrade(lv, p, t, k); }
      ImGui::Dummy(ImVec2(1, 3 * z));
      ImGui::PopID();
    }
    ImGui::EndChild();
    std::string hint = standingOf(repOf(p, t.vid)).name + " prices \xC2\xB7 they restock each morning";
    ImVec2 hs = textSize(F_SMALL, hint.c_str());
    text(dl, F_SMALL, ImVec2(s0.x + (cw - hs.x) / 2, s0.y + sh - pad - hs.y), col::muted, hint.c_str());
    ImGui::End();
  }
  static std::string itemNameOf(const std::string& id) { const ItemDef* d = reg().item(id); return d ? d->name : id; }
};
std::unique_ptr<Screen> makeVillagerTalkScreen(u32 npcId, bool confront, const std::string& victim, bool startInShop) { return std::make_unique<TalkScreen>(npcId, confront, victim, startInShop); }

// ------------------------------------------------------------------ the quest board
class BoardScreen : public Screen {
public:
  explicit BoardScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "quest-board"; }
  bool pausesGame() const override { return false; }
  void onOpen(App& a) override { if (GameSession* s = sess(a)) { v = boardVillage(*s->level, s->player->realm, at); s->level->fx->soundUi(s->player->id, "map_paper"); } }
  void update(App& a, double dt) override {
    GameSession* s = sess(a);
    if (!s || !v) { a.popScreen(); return; }
    if (a.input.pressed(Act::Inventory) || a.input.pressed(Act::MenuY)) { a.popScreen(); return; }
    if ((refreshT -= (float)dt) <= 0) { refreshT = 1; posted = boardQuests(*s->level, *v); }
    sel = padStep(a, sel, nButtons, true);
    pressA = a.input.pressed(Act::MenuOk);
  }
  void draw(App& a) override {
    GameSession* s = sess(a);
    if (!s || !v || !posted.is_array()) return;
    Level& lv = *s->level; Player& p = *s->player;
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(6, 4, 2, 140));
    const float cw = std::min(720 * z, W - 16 * z), chMax = H - 16 * z;
    ImGui::SetNextWindowPos(ImVec2(W / 2, H / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(cw, std::min(chMax, lastH > 0 ? lastH : 520 * z)));
    ImGui::Begin("##quest-board", nullptr, CARD);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 w0 = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    woodCard(dl, w0, w0 + ws, 0x5a4028, 0x46321e, 0x8a6a44, 0x2a1a0a);
    const float pad = 14 * z;
    json& S = npcStats(p);
    Standing st = standingOf(repOf(p, v->id));
    std::string head = v->name + " \xE2\x80\x94 Quest Board";
    std::string sub = "You are a " + st.name + " here (" + std::to_string((int)repOf(p, v->id)) + (st.hasNext ? " / " + std::to_string((int)st.next) : std::string()) + ")";
    ImGui::SetCursorPos(ImVec2(pad, pad));
    ImVec2 hp = ImGui::GetCursorScreenPos();
    textShadow(dl, F_H3, hp, col::rgb(0xfff3d6), head.c_str(), ImVec2(2, 2) * z, col::rgb(0x1a0f06));
    ImVec2 hsz = textSize(F_H3, head.c_str());
    text(dl, F_MID, ImVec2(hp.x + hsz.x + 10 * z, hp.y + hsz.y - textSize(F_MID, "A").y - 4 * z), col::gold, sub.c_str());
    ImGui::Dummy(ImVec2(1, hsz.y + 8 * z));
    int btn = 0; std::string take, fin;
    const int cols = std::max(1, (int)((cw - pad * 2) / (200 * z)));
    const float nw = (cw - pad * 2 - (cols - 1) * 8 * z) / cols;
    auto notes = [&](const std::vector<std::pair<json, bool>>& list) {
      int k = 0; float rowTop = ImGui::GetCursorPosY(), rowH = 0;
      for (auto& [q0, mine] : list) {
        json q = q0;
        const int col = k % cols;
        if (col == 0 && k) { rowTop += rowH + 8 * z; rowH = 0; }
        ImGui::SetCursorPos(ImVec2(pad + col * (nw + 8 * z), rowTop));
        ImVec2 n0 = ImGui::GetCursorScreenPos();
        std::string title = q.value("title", std::string()), body = q.value("text", std::string());
        float th = textSize(F_BIG, title.c_str(), nw - 20 * z).y, bh = textSize(F_MID, body.c_str(), nw - 20 * z).y;
        float nh = 10 * z + th + 4 * z + bh + 6 * z + 22 * z + 6 * z + 30 * z + 8 * z;
        const bool hunt = q.value("kind", "") == "hunt";
        bool done = S["qdone"].contains(q.value("id", ""));
        int need = q.value("need", 1), have = mine ? std::min(need, questHave(lv, p, q)) : 0;
        bool ready = mine && have >= need;
        float al = done ? 0.55f : 1.f;
        dl->AddRectFilled(n0 + ImVec2(2, 3) * z, n0 + ImVec2(nw, nh) + ImVec2(2, 3) * z, IM_COL32(0, 0, 0, (int)(89 * al)));
        dl->AddRectFilled(n0, n0 + ImVec2(nw, nh), withAlpha(col::rgb(0xe8dcc0), al));
        dl->AddCircleFilled(n0 + ImVec2(nw / 2, 6 * z), 3.5f * z, withAlpha(col::rgb(hunt ? 0x2a6ac8 : 0xc83a2a), al));
        text(dl, F_BIG, n0 + ImVec2(10, 12) * z, withAlpha(col::rgb(0x3a1a0a), al), title.c_str(), nw - 20 * z);
        text(dl, F_MID, n0 + ImVec2(10 * z, 12 * z + th + 4 * z), withAlpha(col::rgb(0x3a2a1a), al), body.c_str(), nw - 20 * z);
        float ry = n0.y + 12 * z + th + 4 * z + bh + 6 * z;
        drawIcon(dl, "emerald", ImVec2(n0.x + 10 * z, ry), 22 * z, al);
        std::string rew = std::to_string(q["reward"].value("em", 0)) + " \xC2\xB7 " + std::to_string(q["reward"].value("xp", 0)) + " xp";
        text(dl, F_MID, ImVec2(n0.x + 36 * z, ry + 3 * z), withAlpha(col::rgb(0x2a5a2a), al), rew.c_str());
        ImGui::SetCursorScreenPos(ImVec2(n0.x + nw - 10 * z - 90 * z, ry + 26 * z));
        ImGui::PushID(q.value("id", std::string()).c_str());
        if (done) text(dl, F_BIG, ImVec2(n0.x + nw - 10 * z - textSize(F_BIG, "Done").x, ry + 30 * z), col::rgb(0x6a3a1a), "Done");
        else if (ready) { bool sl = sel == btn; if (button("Hand in", BTN_PRIMARY, true, 90 * z) || (sl && pressA)) fin = q.value("id", ""); outlineLast(sl, col::rgb(0xc83a2a)); btn++; }
        else if (mine) { std::string hv = std::to_string(have) + "/" + std::to_string(need); text(dl, F_BIG, ImVec2(n0.x + nw - 10 * z - textSize(F_BIG, hv.c_str()).x, ry + 30 * z), col::rgb(0x6a3a1a), hv.c_str()); }
        else { bool sl = sel == btn; if (button("Take it", BTN_NORMAL, true, 90 * z) || (sl && pressA)) take = q.value("id", ""); outlineLast(sl, col::rgb(0xc83a2a)); btn++; }
        ImGui::PopID();
        rowH = std::max(rowH, nh); k++;
      }
      ImGui::SetCursorPos(ImVec2(pad, rowTop + rowH + 8 * z));
    };
    std::vector<std::pair<json, bool>> board;
    for (auto& q : posted) { json* m = findQuest(p, q.value("id", "")); board.push_back({m ? *m : q, m != nullptr}); }
    notes(board);
    std::vector<std::pair<json, bool>> errands;
    for (auto& q : S["quests"]) if (q.value("vid", "") == v->id && q.value("giver", -1) >= 0) errands.push_back({q, true});
    if (!errands.empty()) { label(F_BODY, col::gold, ("Errands for the folk of " + v->name).c_str()); ImGui::Dummy(ImVec2(1, 4 * z)); notes(errands); }
    std::string hint = "New bounties every morning \xC2\xB7 villagers with a ! over their heads have errands \xC2\xB7 E to close";
    ImVec2 hs = textSize(F_SMALL, hint.c_str());
    ImGui::SetCursorPosX((cw - hs.x) / 2); label(F_SMALL, col::rgb(0xe8d8b8, 204), hint.c_str());
    lastH = ImGui::GetCursorPosY() + pad;
    nButtons = btn; pressA = false;
    ImGui::End();
    if (!take.empty()) for (auto& q : posted) if (q.value("id", "") == take) questTake(lv, p, q);
    if (!fin.empty()) if (json* q = findQuest(p, fin)) {
      if (q->value("giver", -1) >= 0) lv.fx->message(p.id, "Take it to " + q->value("from", std::string("them")) + " yourself", "");
      else questFinish(lv, p, fin);
    }
  }
private:
  ivec3 at; VillageRef v; json posted; float refreshT = 0, lastH = 0; int sel = 0, nButtons = 0; bool pressA = false;
};
std::unique_ptr<Screen> makeQuestBoardScreen(ivec3 at) { return std::make_unique<BoardScreen>(at); }

// ------------------------------------------------------------------ the paths (paths.js PathUI)
class PathScreen : public Screen {
public:
  explicit PathScreen(std::string hint) : hint(std::move(hint)) {}
  const char* id() const override { return "path-view"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override {
    if (!sess(a)) { a.popScreen(); return; }
    if (a.input.pressed(Act::Inventory) || a.input.pressed(Act::MenuY)) { a.popScreen(); return; }
    sel = padStep(a, sel, nButtons, true);
    pressA = a.input.pressed(Act::MenuOk);
  }
  void draw(App& a) override {
    GameSession* s = sess(a); if (!s) return;
    Level& lv = *s->level; Player& p = *s->player;
    const json& PD = magicData().paths;
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 102));
    const float cw = std::min(760 * z, W * 0.94f);
    ImGui::SetNextWindowPos(ImVec2(W / 2, H / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(cw, std::min(H * 0.88f, lastH > 0 ? lastH : 560 * z)));
    ImGui::Begin("##path-view", nullptr, CARD);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 w0 = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->AddRectFilled(w0, w0 + ws, IM_COL32(16, 14, 12, 242)); dl->AddRect(w0, w0 + ws, IM_COL32(232, 192, 96, 89), 0, 0, 1 * z);
    const float pad = 14 * z;
    ImGui::SetCursorPos(ImVec2(pad, 12 * z));
    const json* P = p.stats.contains("path") && p.stats["path"].is_object() && PD.contains(p.stats["path"].value("k", "")) ? &p.stats["path"] : nullptr;
    const int cl = combatLevel(p), pts = pathPoints(p), spent = pathSpent(p), AT = VD().value("pathAt", 20);
    auto colOf = [](const json& D) { std::string c = D.value("col", std::string("#ffd88a")); return col::rgb((u32)std::stoul(c.substr(1), nullptr, 16)); };
    int btn = 0; std::string pick, learn; bool forget = false;
    auto headRow = [&](const std::string& h, ImU32 hc, const std::string& sub) {
      ImVec2 p0 = ImGui::GetCursorScreenPos(); ImVec2 hs = textSize(F_H3, h.c_str());
      text(dl, F_H3, p0, hc, h.c_str());
      ImVec2 ss = textSize(F_MID, sub.c_str()); text(dl, F_MID, ImVec2(w0.x + cw - pad - ss.x, p0.y + hs.y - ss.y - 4 * z), col::muted, sub.c_str());
      ImGui::Dummy(ImVec2(1, hs.y + 6 * z));
    };
    if (!P) {
      headRow("Choose your path", col::rgb(0xffd88a), "Combat level " + std::to_string(cl) + (cl < AT ? " \xC2\xB7 paths open at " + std::to_string(AT) : std::string()));
      const float colW = (cw - pad * 2 - 16 * z) / 3;
      float top = ImGui::GetCursorPosY(), maxH = 0; int k = 0;
      for (const char* key : {"knight", "ranger", "mage"}) {
        if (!PD.contains(key)) continue;
        const json& D = PD[key];
        ImGui::SetCursorPos(ImVec2(pad + k * (colW + 8 * z), top));
        ImVec2 c0 = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(colW, 6 * z));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8 * z);
        label(F_H3, colOf(D), D.value("name", std::string(key)).c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8 * z); label(F_MID, col::muted, D.value("blurb", std::string()).c_str(), colW - 16 * z);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8 * z); label(F_MID, col::rgb(0xfff3d6), D.value("base", std::string()).c_str(), colW - 16 * z);
        for (auto& row : D["tiers"]) for (auto& tl : row) { ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14 * z); label(F_SMALL, col::muted, ("\xC2\xB7 " + tl[1].get<std::string>() + " \xE2\x80\x94 " + tl[2].get<std::string>()).c_str(), colW - 26 * z); }
        ImGui::Dummy(ImVec2(1, 4 * z));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8 * z);
        bool sl = sel == btn, can = cl >= AT;
        if (button(("Walk the " + D.value("name", std::string(key)) + "'s path##" + key).c_str(), can ? BTN_PRIMARY : BTN_NORMAL, true, colW - 16 * z, !can) || (sl && pressA && can)) pick = key;
        outlineLast(sl); btn++;
        ImGui::Dummy(ImVec2(1, 8 * z));
        ImGui::EndGroup();
        ImVec2 c1 = ImGui::GetItemRectMax();
        dl->AddRectFilled(c0, ImVec2(c0.x + colW, c1.y), IM_COL32(255, 255, 255, 13));
        if (hint == key) dl->AddRect(c0, ImVec2(c0.x + colW, c1.y), IM_COL32(255, 216, 138, 128), 0, 0, 1 * z);
        maxH = std::max(maxH, c1.y - c0.y); k++;
      }
      ImGui::SetCursorPos(ImVec2(pad, top + maxH + 8 * z));
    } else {
      const std::string k = (*P)["k"]; const json& D = PD[k];
      int left = pts - spent;
      headRow("The " + D.value("name", k) + "'s path", colOf(D), "Combat level " + std::to_string(cl) + " \xC2\xB7 " +
              (left > 0 ? std::to_string(left) + " talent point" + (left > 1 ? "s" : "") + " to spend" : pts < 6 ? "next point at combat level " + std::to_string(AT + std::max(0, pts) * 6) : std::string("every point spent")));
      label(F_MID, col::rgb(0xfff3d6), D.value("base", std::string()).c_str(), cw - pad * 2);
      ImGui::Dummy(ImVec2(1, 6 * z));
      static const char* TN[3] = {"First", "Second", "Third"};
      const float tw = (cw - pad * 2 - 6 * z) / 2;
      for (int i = 0; i < (int)D["tiers"].size(); i++) {
        bool open = spent >= i * 2;
        ImGui::SetCursorPosX(pad);
        label(F_SMALL, col::muted, (std::string(TN[i]) + " tier" + (open ? "" : " \xC2\xB7 opens after " + std::to_string(i * 2) + " talents")).c_str());
        float top = ImGui::GetCursorPosY(), rh = 0; int j = 0;
        for (auto& tl : D["tiers"][i]) {
          std::string id = tl[0], nm = tl[1], ds = tl[2];
          bool got = P->contains("t") && (*P)["t"].contains(id), can = !got && left > 0 && open;
          ImGui::SetCursorPos(ImVec2(pad + j * (tw + 6 * z), top));
          ImVec2 c0 = ImGui::GetCursorScreenPos();
          ImGui::BeginGroup();
          ImGui::Dummy(ImVec2(tw, 4 * z));
          float al = open ? 1.f : 0.55f;
          ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8 * z); label(F_BODY, withAlpha(col::rgb(0xfff3d6), al), nm.c_str());
          ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8 * z); label(F_SMALL, withAlpha(col::muted, al), ds.c_str(), tw - 16 * z);
          ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8 * z);
          if (got) label(F_SMALL, col::xp, "Learned");
          else { bool sl = sel == btn; if (button(("Learn##" + id).c_str(), can ? BTN_PRIMARY : BTN_NORMAL, true, 0, !can) || (sl && pressA && can)) learn = id; outlineLast(sl); btn++; }
          ImGui::Dummy(ImVec2(1, 4 * z));
          ImGui::EndGroup();
          ImVec2 c1 = ImGui::GetItemRectMax();
          dl->AddRectFilled(c0, ImVec2(c0.x + tw, c1.y), IM_COL32(255, 255, 255, 13));
          if (got) dl->AddRectFilled(c0, ImVec2(c0.x + 3 * z, c1.y), col::xp);
          rh = std::max(rh, c1.y - c0.y); j++;
        }
        ImGui::SetCursorPos(ImVec2(pad, top + rh + 6 * z));
      }
      ImGui::SetCursorPosX(pad);
      bool sl = sel == btn;
      if (button("Forget it all (20 emeralds)", BTN_NORMAL, true) || (sl && pressA)) forget = true;
      outlineLast(sl); btn++;
    }
    ImGui::SetCursorPosX(pad); label(F_SMALL, col::muted, "E to close");
    lastH = ImGui::GetCursorPosY() + 12 * z;
    nButtons = btn; pressA = false;
    ImGui::End();
    if (!pick.empty()) pathPick(lv, p, pick);
    if (!learn.empty()) pathLearn(lv, p, learn);
    if (forget) pathForget(lv, p);
  }
private:
  std::string hint; int sel = 0, nButtons = 0; bool pressA = false; float lastH = 0;
};
std::unique_ptr<Screen> makePathScreen(const std::string& hint) { return std::make_unique<PathScreen>(hint); }

} // namespace dq
