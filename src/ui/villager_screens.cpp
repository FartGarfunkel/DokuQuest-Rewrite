// ui/villager_screens.cpp — the conversation and the shop (villagers.js NpcTalk, NpcShop), the quest board
// (QuestBoard) and the paths window (paths.js PathUI). The dialogue itself is worked out in sim (sim/villagers.h
// Talk): these draw it the way styles.css drew .npc-talk, .npc-shop, .quest-board and .path-view.
#include "ui/villager_screens.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "audio/audio.h"
#include "audio/babble.h"
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
// the cards' wood: a soft drop shadow, the black 2px outline, the gradient body, the bevelled 3px border (.nt-card / .npc-shop / .qb-card).
// Drawn into the background list (the windows clip to their own rectangle, which would cut the outline and the shadow).
void woodCard(ImDrawList* dl, ImVec2 a, ImVec2 b, u32 top, u32 bot, u32 hi, u32 lo, float shadowY = 18, float shadowBlur = 50) {
  float z = Z();
  dropShadow(dl, a, b, shadowY * z, shadowBlur * z, 0.65f);
  dl->AddRectFilled(a - ImVec2(2, 2) * z, b + ImVec2(2, 2) * z, col::black);
  dl->AddRectFilledMultiColor(a + ImVec2(3, 3) * z, b - ImVec2(3, 3) * z, col::rgb(top), col::rgb(top), col::rgb(bot), col::rgb(bot));
  bevel(dl, a, b, col::rgb(hi), col::rgb(lo), 3 * z);
}
// a CSS pixel size on one of our fonts, as the `scale` argument of text() / textSize() (.nt-opt is 14px on the 13px F_MID...)
float css(FontId f, float px) { return px / fontPx(f); }
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
  ivec2 nv = in.navStep();   // d-pad, left stick (with key repeat) and the arrow keys
  if (nv.y > 0 || (horizontalToo && nv.x > 0)) sel = (sel + 1) % n;
  else if (nv.y < 0 || (horizontalToo && nv.x < 0)) sel = (sel + n - 1) % n;
  return clampv(sel, 0, n - 1);
}
// text cut to a width, with an ellipsis (nothing is ever drawn past its box)
std::string fitText(FontId f, const std::string& str, float maxW) {
  if (textSize(f, str.c_str()).x <= maxW) return str;
  std::string r = str;
  while (!r.empty() && textSize(f, (r + "\xE2\x80\xA6").c_str()).x > maxW) { r.pop_back(); while (!r.empty() && ((unsigned char)r.back() & 0xC0) == 0x80) r.pop_back(); }
  return r + "\xE2\x80\xA6";
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
  void onClose(App& a) override {
    if (voiceH && a.audio) a.audio->stop(voiceH);
    if (GameSession* s = sess(a)) talkClosed(*s->level, *s->player, t);
  }
  bool onBack(App& a) override {
    if (t.mode == "shop") { if (GameSession* s = sess(a)) talkShopBack(*s->level, *s->player, t); sel = 0; return true; }
    return false;
  }
  void update(App& a, double dt) override {
    GameSession* s = sess(a);
    if (failed || !s || closeReq) { a.popScreen(); return; }
    Level& lv = *s->level; Player& p = *s->player;
    age += (float)dt;
    if (lines != t.voiceLines) { lines = t.voiceLines; shown = 0; }
    if (spokenSeen != t.spokenN) speak(a);
    shown = std::min((float)t.text.size(), shown + (float)dt * 26);   // 26 letters a second (NpcTalk.frame)
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
    bg->AddRectFilledMultiColor(ImVec2(0, H * 0.45f), ImVec2(W, H), 0, 0, IM_COL32(6, 4, 2, 115), IM_COL32(6, 4, 2, 115));   // .npc-talk: linear-gradient(transparent 45%, rgba(6,4,2,.45))
    const bool shop = t.mode == "shop";
    // ---- .nt-card: a 3px border, 12px 14px of padding, the 76px face and .nt-main side by side with a 12px gap ----
    const float bd = 3 * z, padX = 14 * z, padY = 12 * z, faceS = 76 * z;
    const float cw = std::min(640.f, W / z - 24.f) * z;
    const float mw = cw - 2 * (bd + padX) - faceS - 12 * z;
    const float tsz = css(F_BODY, 16), lh = 16 * 1.35f * z;                       // .nt-text: 16px / 1.35
    const float osz = css(F_MID, 14), olh = 14 * 1.2f * z, nsz = css(F_H3, 14);   // .nt-opt: 14px, its number in the display font
    const float nameH = textSize(F_H3, "A").y;
    const float textH = std::max(44 * z, textLinesSize(F_BODY, t.text.c_str(), mw, lh, tsz).y);   // min-height 44px, the whole line (it types out inside this)
    std::vector<float> oh;           // an option is a .btn: 44px at least, taller when its label wraps (padding 4 + border 2)
    std::vector<std::string> ics;
    float optsH = 0;
    if (!shop) for (size_t i = 0; i < t.opts.size(); i++) {
      ics.push_back(t.opts[i].icon.empty() ? std::to_string(i + 1) : t.opts[i].icon);
      float lw = mw - 20 * z - textSize(F_H3, ics[i].c_str(), 0, nsz).x - 8 * z;
      oh.push_back(std::max(44 * z, textLinesSize(F_MID, t.opts[i].label.c_str(), lw, olh, osz).y + 12 * z));
      optsH += oh[i] + (i ? 4 * z : 0);
    }
    float ch = std::min(2 * bd + 2 * padY + std::max(faceS, nameH + 6 * z + textH + 8 * z + optsH), H - 16 * z);
    // sits above the hotbar (padding-bottom 92px); on a short window it moves down rather than off the top
    ImVec2 c0((W - cw) / 2, std::max(8 * z, H - 92 * z - ch));
    if (c0.y + ch > H - 8 * z) c0.y = H - 8 * z - ch;
    cardR = ImVec4(c0.x, c0.y, c0.x + cw, c0.y + ch);
    woodCard(bg, c0, c0 + ImVec2(cw, ch), 0x3e2e1e, 0x2e2216, 0x7a5a38, 0x1a0f06);
    ImGui::SetNextWindowPos(c0); ImGui::SetNextWindowSize(ImVec2(cw, ch));
    ImGui::Begin("##npc-talk", nullptr, CARD);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 fp = c0 + ImVec2(bd + padX, bd + padY);
    // .nt-face: 76px, a 2px dark border, the face over it at 72px
    dl->AddRectFilled(fp, fp + ImVec2(faceS, faceS), col::rgb(0x1a120a));
    dl->AddRect(fp + ImVec2(1, 1) * z, fp + ImVec2(75, 75) * z, col::rgb(0x1a0f06), 0, 0, 2 * z);
    if (face) dl->AddImage((ImTextureID)(intptr_t)face, fp + ImVec2(2, 2) * z, fp + ImVec2(74, 74) * z);
    const float mx = fp.x + faceS + 12 * z;
    // .nt-name: the name (display 24px, a pixel shadow), the job and village in gold, the standing at the far right, on one baseline
    const ImVec2 nm = textSize(F_H3, t.name.c_str());
    const float base = fp.y + baselineOf(F_H3);
    textShadow(dl, F_H3, ImVec2(mx, fp.y), col::rgb(0xfff3d6), t.name.c_str(), ImVec2(2, 2) * z, col::rgb(0x1a0f06));
    float standW = 0; std::string st;
    const float ssz = css(F_SMALL, 12);
    if (!t.vid.empty()) { st = standingOf(repOf(p, t.vid)).name; standW = textSize(F_SMALL, st.c_str(), 0, ssz).x; }
    float titleRoom = mw - nm.x - 8 * z - (standW > 0 ? standW + 8 * z : 0);
    if (titleRoom > 24 * z) text(dl, F_MID, ImVec2(mx + nm.x + 8 * z, base - baselineOf(F_MID)), col::gold, fitText(F_MID, t.title, titleRoom).c_str());
    if (standW > 0 && nm.x + standW + 8 * z < mw) text(dl, F_SMALL, ImVec2(mx + mw - standW, base - baselineOf(F_SMALL, ssz)), col::muted, st.c_str(), 0, ssz);
    // .nt-text: types out letter by letter (never cut inside a UTF-8 character)
    size_t nshown = std::min(t.text.size(), (size_t)shown);
    while (nshown > 0 && nshown < t.text.size() && ((u8)t.text[nshown] & 0xC0) == 0x80) nshown--;
    const float ty = fp.y + nameH + 6 * z;
    textLines(dl, F_BODY, ImVec2(mx, ty), col::text, t.text.substr(0, nshown).c_str(), mw, lh, tsz);
    if (!shop) {
      // .nt-opts: a column of .btn rows, 4px apart. They sit in their own scrolling list (mouse wheel, right stick,
      // d-pad), so a long list never runs off the card; the list is 4px / 3px wider so a selected row's outline isn't cut.
      const float top = ty + textH + 8 * z, availH = std::max(40 * z, c0.y + ch - bd - padY - top);
      ImGui::SetCursorScreenPos(ImVec2(mx - 4 * z, top - 3 * z));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4 * z));
      // NoNavInputs: these screens move their own highlight (padStep in update); ImGui's pad nav would move a second one
      ImGui::BeginChild("##opts", ImVec2(mw + 8 * z, availH + 12 * z), false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavInputs);
      padScrollHere();
      ImDrawList* od = ImGui::GetWindowDrawList();
      ImGui::SetCursorPos(ImVec2(4 * z, 3 * z));
      const float rw = ImGui::GetContentRegionAvail().x - 4 * z;
      for (int i = 0; i < (int)t.opts.size() && i < (int)oh.size(); i++) {
        const TalkOpt o = t.opts[i];   // a copy: choosing changes the options
        const float optH = oh[i];
        ImGui::PushID(i);
        ImGui::SetCursorPosX(4 * z);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        bool click = ImGui::InvisibleButton("##o", ImVec2(rw, optH));
        bool hot = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        // the highlight follows the mouse (when it moves) and the pad's/keyboard's focus, so what's lit is what A / Enter picks
        if (hot && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) sel = i;
        if (sel == i && scrolledTo != sel) { ImGui::SetScrollHereY(); scrolledTo = sel; }
        btnFace(od, p0, p0 + ImVec2(rw, optH), hot, held);
        if (sel == i) od->AddRect(p0 - ImVec2(1, 1) * z, p0 + ImVec2(rw, optH) + ImVec2(1, 1) * z, col::gold, 5 * z, 0, 2 * z);   // .nt-opt.sel { outline: 2px solid gold }
        const float cy = p0.y + optH / 2 + (held ? z : 0);
        const ImVec2 ns = textSize(F_H3, ics[i].c_str(), 0, nsz);
        text(od, F_H3, ImVec2(p0.x + 10 * z, cy - ns.y / 2), col::gold, ics[i].c_str(), 0, nsz);
        const float lx = p0.x + 10 * z + ns.x + 8 * z, lw = rw - (lx - p0.x) - 10 * z;
        const float lhh = textLinesSize(F_MID, o.label.c_str(), lw, olh, osz).y;
        textLines(od, F_MID, ImVec2(lx, cy - lhh / 2), col::text, o.label.c_str(), lw, olh, osz);
        ImGui::PopID();
        if (click) { choose(lv, p, i); break; }
      }
      ImGui::EndChild();
      ImGui::PopStyleVar();
    }
    ImGui::End();
    if (shop) drawShop(a, lv, p, c0, cw); else shopR = ImVec4(0, 0, 0, 0);
    // a click on the dim backdrop (outside the card and the shop) closes it (NpcTalk: pointerdown on the overlay)
    if (age > 0.25f && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2))) {
      const ImVec2 m = ImGui::GetMousePos(); const float e = 3 * z;
      auto in = [&](const ImVec4& r) { return r.z > r.x && m.x >= r.x - e && m.x <= r.z + e && m.y >= r.y - e && m.y <= r.w + e; };
      if (!in(cardR) && !in(shopR)) closeReq = true;
    }
  }

private:
  u32 npc; bool confront; std::string victim; bool startShop;
  Talk t; bool failed = false, closeReq = false; int sel = 0, lines = -1, scrolledTo = -1, spokenSeen = 0, voiceH = 0; float shown = 0, age = 0; GLuint face = 0;
  ImVec4 cardR{0, 0, 0, 0}, shopR{0, 0, 0, 0};   // the windows' rectangles (x0 y0 x1 y1), for the backdrop click
  void choose(Level& lv, Player& p, int i) { playClick(); talkChoose(lv, p, t, i); sel = 0; scrolledTo = -1; }   // b.onclick: sfx.play('click'), then the option
  // villagers.js babble(sfx, text, npcVoice(seed), { rate: 26 }): the line starts to babble as its words start to type out;
  // a new line cuts the old one off (the original let two voices run over each other)
  void speak(App& a) {
    spokenSeen = t.spokenN;
    if (!a.audio || t.spoken.empty()) return;
    if (voiceH) a.audio->stop(voiceH);
    const NpcVoice nv = npcVoice(t.seed);
    snd::BabbleVoice v; v.pitch = nv.pitch; v.formant = nv.formant; v.triangle = nv.triangle; v.seed = nv.seed;
    voiceH = babble(*a.audio, t.spoken, v, t.spokenVol);
  }
  std::vector<ShopLine> rows(Level& lv, Player& p) { VillageRef v = villageById(lv, t.vid); return v ? shopLines(lv, p, *v, t.idx, t.job, t.tab) : std::vector<ShopLine>{}; }
  // .npc-shop: a panel stacked on top of the card (left/right 0 of its padding box, 6px above it): Buy / Sell, the emeralds, Back,
  // the list of trades and a hint
  void drawShop(App& a, Level& lv, Player& p, ImVec2 card, float cw) {
    const float z = Z(), bd = 3 * z, pad = 8 * z, rowH = 30 * z, gap = 3 * z, headH = 36 * z;
    const float osz = css(F_MID, 14), bsz = css(F_H3, 18), ssz = css(F_SMALL, 11), esz = css(F_H3, 20);
    auto L = rows(lv, p);
    const std::string hint = standingOf(repOf(p, t.vid)).name + " prices \xC2\xB7 they restock each morning";
    const float hintH = textSize(F_SMALL, hint.c_str(), 0, ssz).y + 6 * z, fixedH = 2 * bd + 2 * pad + headH + hintH;
    const float sw = cw - 2 * bd, bottom = card.y + bd - 6 * z;
    float natural = L.empty() ? 30 * z : (float)L.size() * rowH + (float)(L.size() - 1) * gap;
    float listH = std::min(natural, std::min(380 * z, a.height * 0.52f) - fixedH);   // max-height: min(380px, 52vh)
    listH = std::max(std::min(listH, bottom - 4 * z - fixedH), 40 * z);
    const float sh = fixedH + listH;
    const ImVec2 s0(card.x + bd, std::max(4 * z, bottom - sh));
    shopR = ImVec4(s0.x, s0.y, s0.x + sw, s0.y + sh);
    woodCard(ImGui::GetBackgroundDrawList(), s0, s0 + ImVec2(sw, sh), 0x3e2e1e, 0x2e2216, 0x7a5a38, 0x1a0f06, 12, 40);
    ImGui::SetNextWindowPos(s0); ImGui::SetNextWindowSize(ImVec2(sw, sh));
    ImGui::Begin("##npc-shop", nullptr, CARD);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cx = s0.x + bd + pad, cy = s0.y + bd + pad, inner = sw - 2 * (bd + pad);
    ImGui::SetCursorScreenPos(ImVec2(cx, cy));
    if (button("Buy##tab", BTN_NORMAL, true)) { t.tab = "buy"; sel = 0; }
    outlineLast(t.tab == "buy");
    ImGui::SameLine(0, 6 * z);
    if (button("Sell##tab", BTN_NORMAL, true)) { t.tab = "sell"; sel = 0; }
    outlineLast(t.tab == "sell");
    int em = 0; for (auto& st : p.inv) if (st.id == "emerald") em += st.n;
    const std::string ems = std::to_string(em);
    const float bw = textSize(F_MID, "Back").x + 24 * z;
    const ImVec2 es = textSize(F_H3, ems.c_str(), 0, esz);
    const float ex = cx + inner - bw - 6 * z - es.x;   // .sh-em: margin-left auto, so it hugs Back
    drawIcon(dl, "emerald", ImVec2(ex - 26 * z, cy + 4 * z), 22 * z);
    text(dl, F_H3, ImVec2(ex, cy + 15 * z - es.y / 2), col::rgb(0x9ff0a0), ems.c_str(), 0, esz);
    ImGui::SetCursorScreenPos(ImVec2(cx + inner - bw, cy));
    if (button("Back##shop", BTN_NORMAL, true, bw)) { talkShopBack(lv, p, t); sel = 0; ImGui::End(); return; }
    ImGui::SetCursorScreenPos(ImVec2(cx, cy + headH));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, gap));
    // NoNavInputs: these screens move their own highlight (padStep in update); ImGui's pad nav would move a second one
    ImGui::BeginChild("##rows", ImVec2(inner, listH), false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavInputs);
    padScrollHere();
    if (L.empty()) label(F_MID, col::muted, "Nothing today.");
    const json& TN = VD()["tierName"];
    for (int k = 0; k < (int)L.size(); k++) {
      const ShopLine& l = L[k];
      ImGui::PushID(k);
      const ImVec2 p0 = ImGui::GetCursorScreenPos(); const float rw = ImGui::GetContentRegionAvail().x;
      bool click = rowButton("##r", ImVec2(rw, rowH), sel == k, false);   // .sh-row: 1px border, hover / .sel lights it
      if (ImGui::IsItemHovered() && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) sel = k;
      if (sel == k && scrolledTo != sel) { ImGui::SetScrollHereY(); scrolledTo = sel; }
      ImDrawList* d2 = ImGui::GetWindowDrawList();
      const float al = l.locked ? 0.4f : l.can ? 1.f : 0.55f, ic = 22 * z, cyr = p0.y + rowH / 2;
      const bool buy = t.tab == "buy";
      const std::string giveN = std::to_string(buy ? l.price : l.n), getN = std::to_string(buy ? l.n : l.price);
      const ImVec2 gs = textSize(F_H3, giveN.c_str(), 0, bsz), ns = textSize(F_H3, getN.c_str(), 0, bsz), ms = textSize(F_MID, "A", 0, osz);
      float x = p0.x + 9 * z;                                  // border 1 + padding 8
      drawIcon(d2, buy ? "emerald" : l.id, ImVec2(x, cyr - ic / 2), ic, al);
      text(d2, F_H3, ImVec2(x + ic + 4 * z, cyr - gs.y / 2), withAlpha(col::rgb(0xfff3d6), al), giveN.c_str(), 0, bsz);
      x += std::max(64 * z, ic + 4 * z + gs.x) + 6 * z;        // .sh-give { min-width: 64px } then the flex gap
      text(d2, F_MID, ImVec2(x, cyr - ms.y / 2), withAlpha(col::muted, al), "\xE2\x86\x92", 0, osz);
      x += textSize(F_MID, "\xE2\x86\x92", 0, osz).x + 6 * z;
      drawIcon(d2, buy ? l.id : "emerald", ImVec2(x, cyr - ic / 2), ic, al); x += ic + 6 * z;   // .sh-get { gap: 6px }
      text(d2, F_H3, ImVec2(x, cyr - ns.y / 2), withAlpha(col::rgb(0xfff3d6), al), getN.c_str(), 0, bsz); x += ns.x + 4 * z;
      const std::string nm = buy ? itemNameOf(l.id) : "for " + itemNameOf(l.id);
      const std::string right = l.locked ? TN[l.tier].get<std::string>() + " only" : l.left > 0 ? std::to_string(l.left) + " left" : "sold out";
      const ImVec2 rs = textSize(F_SMALL, right.c_str(), 0, ssz);
      text(d2, F_MID, ImVec2(x, cyr - ms.y / 2), withAlpha(col::text, al), fitText(F_MID, nm, p0.x + rw - rs.x - 9 * z - 6 * z - x).c_str(), 0, osz);
      text(d2, F_SMALL, ImVec2(p0.x + rw - rs.x - 9 * z, cyr - rs.y / 2), withAlpha(col::muted, al), right.c_str(), 0, ssz);
      ImGui::PopID();
      if (click) { sel = k; talkShopTrade(lv, p, t, k); }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    const ImVec2 hs = textSize(F_SMALL, hint.c_str(), 0, ssz);
    text(dl, F_SMALL, ImVec2(s0.x + (sw - hs.x) / 2, s0.y + sh - bd - pad - hs.y), col::muted, hint.c_str(), 0, ssz);   // .sh-hint
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
    if (!s || !v || closeReq) { a.popScreen(); return; }
    age += (float)dt;
    if (a.input.pressed(Act::Inventory) || a.input.pressed(Act::MenuY)) { a.popScreen(); return; }
    if ((refreshT -= (float)dt) <= 0) { refreshT = 1; posted = boardQuests(*s->level, *v); }
    int was = sel;
    sel = padStep(a, sel, nButtons, true);
    selMoved = sel != was;
    pressA = a.input.pressed(Act::MenuOk);
  }
  // .quest-board / .qb-card: a dim backdrop, the 720px wooden card, notes pinned in a grid (auto-fill, min 200px, 8px gaps)
  void draw(App& a) override {
    GameSession* s = sess(a);
    if (!s || !v || !posted.is_array()) return;
    Level& lv = *s->level; Player& p = *s->player;
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(6, 4, 2, 140));
    const float bd = 3 * z, padX = 14 * z, padY = 12 * z;
    const float cw = std::min(720.f, W / z - 16.f) * z, ch = std::min(H - 16 * z, lastH > 0 ? lastH : 520 * z);
    const ImVec2 w0((W - cw) / 2, (H - ch) / 2);
    cardR = ImVec4(w0.x, w0.y, w0.x + cw, w0.y + ch);
    woodCard(bg, w0, w0 + ImVec2(cw, ch), 0x5a4028, 0x46321e, 0x8a6a44, 0x2a1a0a);
    ImGui::SetNextWindowPos(w0); ImGui::SetNextWindowSize(ImVec2(cw, ch));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##quest-board", nullptr, CARD);   // its own pad highlight (padStep)
    ImGui::PopStyleVar();
    padScrollHere();   // the right stick scrolls a tall board
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ox = bd + padX, iw = cw - 2 * ox;
    ImGui::SetCursorPos(ImVec2(ox, bd + padY));
    const ImVec2 org = ImGui::GetCursorScreenPos() - ImGui::GetCursorPos();   // where content (0, 0) is on screen, scroll included
    auto scr = [&](float x, float y) { return org + ImVec2(x, y); };
    json& S = npcStats(p);
    Standing st = standingOf(repOf(p, v->id));
    // .qb-head: the title and the standing on one baseline, space-between (the standing drops under the title when they don't fit)
    std::string head = v->name + " \xE2\x80\x94 Quest Board";
    std::string sub = "You are a " + st.name + " here (" + std::to_string((int)repOf(p, v->id)) + (st.hasNext ? " / " + std::to_string((int)st.next) : std::string()) + ")";
    float y = bd + padY;
    const ImVec2 hsz = textSize(F_H3, head.c_str()), ssz = textSize(F_MID, sub.c_str());
    textShadow(dl, F_H3, scr(ox, y), col::rgb(0xfff3d6), head.c_str(), ImVec2(2, 2) * z, col::rgb(0x1a0f06));
    if (hsz.x + 10 * z + ssz.x <= iw) { text(dl, F_MID, scr(ox + iw - ssz.x, y + baselineOf(F_H3) - baselineOf(F_MID)), col::gold, sub.c_str()); y += hsz.y; }
    else { text(dl, F_MID, scr(ox, y + hsz.y), col::gold, sub.c_str()); y += hsz.y + ssz.y; }
    y += 8 * z;
    int btn = 0; std::string take, fin;
    const int cols = std::max(1, (int)((iw + 8 * z) / (208 * z)));
    const float nw = (iw - (cols - 1) * 8 * z) / cols, nwi = nw - 20 * z;
    const float tsz = css(F_H3, 18), tlh = 18 * z, plh = 13 * 1.3f * z;   // note title: display 18px; text 13px / 1.3
    auto notes = [&](const std::vector<std::pair<json, bool>>& list) {
      // heights first: the notes of a row stretch to the tallest (grid items)
      struct N { json q; bool mine, done, ready; int have, need; float h; };
      std::vector<N> ns;
      for (auto& [q0, mine] : list) {
        N n{q0, mine, S["qdone"].contains(q0.value("id", "")), false, 0, q0.value("need", 1), 0};
        n.have = mine ? std::min(n.need, questHave(lv, p, n.q)) : 0;
        n.ready = mine && n.have >= n.need;
        const bool plain = !n.done && !n.ready && n.mine;   // just "have/need" in the display font, no button
        float th = textLinesSize(F_H3, n.q.value("title", std::string()).c_str(), nwi, tlh, tsz).y;
        float bh = textLinesSize(F_MID, n.q.value("text", std::string()).c_str(), nwi, plh).y;
        n.h = 10 * z + th + 4 * z + bh + 4 * z + 22 * z + 4 * z + ((n.done || plain) ? 18 * z : 30 * z) + 8 * z;
        ns.push_back(n);
      }
      for (size_t r0 = 0; r0 < ns.size(); r0 += cols) {
        float rowH = 0; for (size_t k = r0; k < ns.size() && k < r0 + cols; k++) rowH = std::max(rowH, ns[k].h);
        for (size_t k = r0; k < ns.size() && k < r0 + cols; k++) {
          const N& n = ns[k]; const json& q = n.q;
          const float nx = ox + (float)(k - r0) * (nw + 8 * z);
          const ImVec2 n0 = scr(nx, y), n1 = n0 + ImVec2(nw, rowH);
          const bool hunt = q.value("kind", "") == "hunt";
          const float al = n.done ? 0.55f : 1.f;
          dl->AddRectFilled(n0 + ImVec2(2, 3) * z, n1 + ImVec2(2, 3) * z, IM_COL32(0, 0, 0, (int)(89 * al)));   // box-shadow: 2px 3px 0 rgba(0,0,0,.35)
          dl->AddRectFilled(n0, n1, withAlpha(col::rgb(0xe8dcc0), al));
          dl->AddCircleFilled(n0 + ImVec2(nw / 2 + 1.5f, 7.5f) * z, 3.5f * z, IM_COL32(0, 0, 0, (int)(102 * al)));   // the pin and its shadow
          dl->AddCircleFilled(n0 + ImVec2(nw / 2 + 0.5f, 6.5f) * z, 3.5f * z, withAlpha(col::rgb(hunt ? 0x2a6ac8 : 0xc83a2a), al));
          float ty = n0.y + 10 * z;
          std::string title = q.value("title", std::string()), body = q.value("text", std::string());
          ty += textLines(dl, F_H3, ImVec2(n0.x + 10 * z, ty), withAlpha(col::rgb(0x3a1a0a), al), title.c_str(), nwi, tlh, tsz) + 4 * z;
          ty += textLines(dl, F_MID, ImVec2(n0.x + 10 * z, ty), withAlpha(col::rgb(0x3a2a1a), al), body.c_str(), nwi, plh) + 4 * z;
          drawIcon(dl, "emerald", ImVec2(n0.x + 10 * z, ty), 22 * z, al);   // .qb-rew
          const json R = q.value("reward", json::object());
          std::string rew = std::to_string(R.value("em", 0)) + " \xC2\xB7 " + std::to_string(R.value("xp", 0)) + " xp";
          text(dl, F_MID, ImVec2(n0.x + 36 * z, ty + (22 * z - textSize(F_MID, rew.c_str()).y) / 2), withAlpha(col::rgb(0x2a5a2a), al), rew.c_str());
          ty += 22 * z + 4 * z;
          const float right = n0.x + nw - 10 * z;   // align-self: flex-end
          ImGui::PushID(q.value("id", std::string()).c_str());
          if (n.done || (n.mine && !n.ready)) {   // <em>Done</em> / <em>have/need</em>
            std::string em = n.done ? "Done" : std::to_string(n.have) + "/" + std::to_string(n.need);
            text(dl, F_H3, ImVec2(right - textSize(F_H3, em.c_str(), 0, tsz).x, ty), col::rgb(0x6a3a1a), em.c_str(), 0, tsz);
          } else {
            const char* lbl = n.ready ? "Hand in" : "Take it";
            const float bw = textSize(F_MID, lbl).x + 24 * z;
            ImGui::SetCursorScreenPos(ImVec2(right - bw, ty));
            bool sl = sel == btn;
            if (button(lbl, n.ready ? BTN_PRIMARY : BTN_NORMAL, true, bw) || (sl && pressA)) (n.ready ? fin : take) = q.value("id", "");
            outlineLast(sl, col::rgb(0xc83a2a));   // .qb-note .btn.sel
            if (sl && selMoved) ImGui::SetScrollHereY(0.5f);
            btn++;
          }
          ImGui::PopID();
        }
        y += rowH + 8 * z;
      }
      y -= ns.empty() ? 0 : 8 * z;   // the grid's last row has no gap under it
    };
    std::vector<std::pair<json, bool>> board;
    for (auto& q : posted) { json* m = findQuest(p, q.value("id", "")); board.push_back({m ? *m : q, m != nullptr}); }
    notes(board);
    std::vector<std::pair<json, bool>> errands;
    for (auto& q : S["quests"]) if (q.value("vid", "") == v->id && q.value("giver", -1) >= 0) errands.push_back({q, true});
    if (!errands.empty()) {   // .qb-sub: margin 10px 0 6px, gold 14px
      const float osz = css(F_MID, 14);
      y += 10 * z;
      std::string sb = "Errands for the folk of " + v->name;
      text(dl, F_MID, scr(ox, y), col::gold, sb.c_str(), 0, osz);
      y += textSize(F_MID, sb.c_str(), 0, osz).y + 6 * z;
      notes(errands);
    }
    // .qb-hint: 12px, centred, margin-top 10px
    const std::string hint = "New bounties every morning \xC2\xB7 villagers with a ! over their heads have errands \xC2\xB7 E to close";
    const float hsz2 = css(F_SMALL, 12); const ImVec2 hs = textSize(F_SMALL, hint.c_str(), 0, hsz2);
    y += 10 * z;
    text(dl, F_SMALL, scr(ox + (iw - hs.x) / 2, y), col::rgb(0xe8d8b8, 204), hint.c_str(), 0, hsz2);
    y += hs.y;
    lastH = y + padY + bd;
    ImGui::SetCursorPos(ImVec2(0, lastH - 1)); ImGui::Dummy(ImVec2(1, 1));   // the scrollable height
    nButtons = btn; pressA = false;
    ImGui::End();
    // a click on the dim backdrop closes the board (QuestBoard: pointerdown on the overlay)
    if (age > 0.25f && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2))) {
      const ImVec2 m = ImGui::GetMousePos(); const float e = 3 * z;
      if (!(m.x >= cardR.x - e && m.x <= cardR.z + e && m.y >= cardR.y - e && m.y <= cardR.w + e)) closeReq = true;
    }
    if (!take.empty()) for (auto& q : posted) if (q.value("id", "") == take) questTake(lv, p, q);
    if (!fin.empty()) if (json* q = findQuest(p, fin)) {
      if (q->value("giver", -1) >= 0) lv.fx->message(p.id, "Take it to " + q->value("from", std::string("them")) + " yourself", "");
      else questFinish(lv, p, fin);
    }
  }
private:
  ivec3 at; VillageRef v; json posted; float refreshT = 0, lastH = 0, age = 0; int sel = 0, nButtons = 0; bool pressA = false, selMoved = false, closeReq = false;
  ImVec4 cardR{0, 0, 0, 0};
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
    ImGui::Begin("##path-view", nullptr, CARD | ImGuiWindowFlags_NoNavInputs);
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
