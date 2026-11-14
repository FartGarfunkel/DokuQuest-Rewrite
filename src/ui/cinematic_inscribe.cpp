// ui/cinematic_inscribe.cpp — the inscription table worked by hand (scene kind 16; the work: sim/station_work.h
// inscribeWork*): you lean over its board and lay on it your spellbook, a spell scroll, paper and lapis. The spells written
// in the book stand in a column over it, touch one to take it out as a scroll; with paper and lapis laid there the spells
// you've learnt stand over the paper, touch one and your hand writes it out. An empty hand on the scroll, with the book
// there, presses it into the book. What's laid there stays on the board (synced, spilled if it's broken). The work's beats
// (INSCRIBE_LENGTH 1.4 s, made at INSCRIBE_CONTACT 0.7): the scroll carried over and pressed into the book; a scroll rising
// out of the book; the hand writing over the paper, the scroll rising off it
#include "ui/cinematic.h"
#include "ui/cinematic_internal.h"
#include "ui/book_ui.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "client/app.h"
#include "client/session.h"
#include "render/camera.h"
#include "audio/audio.h"
#include "sim/station_work.h"
#include "sim/spell_items.h"
#include "sim/magic.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "core/registry.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace dq {
using namespace cine;

namespace {
InscribeView g_iv; bool g_ivOn = false;
bool project(App& a, vec3 wp, ImVec2& out) {
  Camera& cam = a.session->frame.cam;
  const vec4 q = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1);
  if (q.w <= 0.01f) return false;
  out = ImVec2((q.x / q.w * 0.5f + 0.5f) * a.width, (1 - (q.y / q.w * 0.5f + 0.5f)) * a.height);
  return true;
}
constexpr int LIST_SHOWN = 5;   // the lines of a column shown at once (the rest scrolled to)

class CinematicInscribeScreen : public Screen {
public:
  explicit CinematicInscribeScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_inscribe"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 16; S.front = vec3(0);
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    g_iv = InscribeView{}; g_iv.at = at; g_ivOn = true;
  }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool here = reg().block(w.getBlock(at.x, at.y, at.z)).str("station") == "inscribe";
    const bool working = workAt >= 0;
    if ((!here && !working) || (sceneWalkOff(in) && !working) || p.dead || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f) { a.popScreen(); return; }
    if (in.pressed(Act::Inventory) && !in.pressed(Act::MenuY)) showBag = !showBag;
    for (int k = 0; k < 9; k++) if (in.pressed((Act)((int)Act::Hot1 + k))) p.selected = k;
    if (working) tick(a, *s, p, w);
    if (bagPadUpdate(in, p, bag)) return;
    // the pad walks the targets in one line: the four places on the board, then the book's spells, then those to write
    const ivec2 nav = in.navStep();
    const int n = 4 + (int)bookList.size() + (int)writeList.size();
    if (nav.x || nav.y) { pad = true; target = ((target < 0 ? 0 : target) + (nav.x ? nav.x : nav.y) + n) % n; }
    if (pad && target >= 0 && in.pressed(Act::MenuOk)) act(a, target, false);
    if (pad && target >= 0 && in.pressed(Act::MenuX)) act(a, target, true);
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world();
    const float W = (float)a.width, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook();
    if (mouseTookOver(a)) pad = false;
    const std::vector<ItemStack> v = inscribeWorkItems(w, at);
    // the columns: the book's spells over the book; with paper and lapis there, the spells you've learnt over the paper
    bookList.clear(); writeList.clear();
    if (!v[0].empty()) for (auto& [k, r] : bookSpells(v[0])) bookList.push_back({k, r});
    if (!v[2].empty() && !v[3].empty()) for (const std::string& k : knownSpells(p)) if (learntSpell(p, k)) writeList.push_back({k, spellRank(p, k)});
    const int n = 4 + (int)bookList.size() + (int)writeList.size();
    if (target >= n) target = n - 1;
    const vec3 up(0, 1, 0), toYou = vec3(std::sin(scene().viewYaw), 0, std::cos(scene().viewYaw));
    auto linePos = [&](int col, int i) {   // line i of a column (0 the book's, 1 the paper's), bottom up, leant toward you
      const int first = col == 0 ? bookScroll : writeScroll;
      const vec3 rgt(toYou.z, 0, -toYou.x);   // (the paper's column drawn in toward the middle: clear of the bag beside)
      return inscribeLiePos(w, at, col == 0 ? 0 : 2) - (col == 1 ? rgt * 0.08f : vec3(0)) + up * (0.15f + 0.058f * (i - first)) + toYou * 0.04f;
    };
    // what the mouse is on
    int hover = pad ? target : -1;
    const ImVec2 mp = ImGui::GetIO().MousePos;
    ImVec2 sp[4]; bool vis[4];
    for (int k = 0; k < 4; k++) vis[k] = project(a, inscribeLiePos(w, at, k), sp[k]);
    struct Card { int target; ImVec2 a, b; std::string text; u32 col; };
    std::vector<Card> cards;
    auto column = [&](int col, const std::vector<std::pair<std::string, int>>& list, int base, int& scroll) {
      if (pad && hover >= base && hover < base + (int)list.size()) { const int i = hover - base; if (i < scroll) scroll = i; if (i >= scroll + LIST_SHOWN) scroll = i - LIST_SHOWN + 1; }
      scroll = std::clamp(scroll, 0, std::max(0, (int)list.size() - LIST_SHOWN));
      for (int i = scroll; i < (int)list.size() && i < scroll + LIST_SHOWN; i++) {
        ImVec2 c; if (!project(a, linePos(col, i), c)) continue;
        const std::string t = spellLabel(list[i].first, list[i].second);
        const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        cards.push_back({base + i, ImVec2(c.x - ts.x / 2 - 6 * z, c.y - ts.y / 2 - 3 * z), ImVec2(c.x + ts.x / 2 + 6 * z, c.y + ts.y / 2 + 3 * z), t, spellRarityColor(list[i].second)});
      }
    };
    column(0, bookList, 4, bookScroll);
    column(1, writeList, 4 + (int)bookList.size(), writeScroll);
    const bool live = scene().phase == 2 && workAt < 0;
    if (!pad && !overUi && live) {
      for (auto& c : cards) if (mp.x >= c.a.x && mp.x <= c.b.x && mp.y >= c.a.y && mp.y <= c.b.y) hover = c.target;
      if (hover < 0) { float best = 44 * z; for (int k = 0; k < 4; k++) if (vis[k]) { const float d = std::hypot(mp.x - sp[k].x, mp.y - sp[k].y); if (d < best) { best = d; hover = k; } } }
      if (const float wh = ImGui::GetIO().MouseWheel; wh != 0) {   // over a column: scroll it; else the hotbar
        bool onCol = false;
        for (auto& c : cards) if (mp.x >= c.a.x - 20 * z && mp.x <= c.b.x + 20 * z && mp.y >= c.a.y - 20 * z && mp.y <= c.b.y + 20 * z) {
          onCol = true;
          if (c.target < 4 + (int)bookList.size()) bookScroll -= wh > 0 ? 1 : -1; else writeScroll -= wh > 0 ? 1 : -1;
          break;
        }
        if (!onCol) p.selected = ((p.selected - (wh > 0 ? 1 : -1)) % 9 + 9) % 9;
      }
    }
    // the cards, the rings on the places
    for (auto& c : cards) {
      const bool hv = live && c.target == hover;
      dl->AddRectFilled(c.a, c.b, hv ? IM_COL32(60, 44, 24, 230) : IM_COL32(24, 18, 12, 190), 4 * z);
      dl->AddRect(c.a, c.b, hv ? IM_COL32(255, 236, 160, 230) : IM_COL32(120, 96, 60, 140), 4 * z, 0, hv ? 2 * z : 1 * z);
      dl->AddText(ImVec2(c.a.x + 6 * z, c.a.y + 3 * z), IM_COL32((c.col >> 16) & 255, (c.col >> 8) & 255, c.col & 255, 255), c.text.c_str());
    }
    if (live && hover >= 0 && hover < 4 && vis[hover]) dl->AddCircle(sp[hover], 26 * z, IM_COL32(255, 236, 160, 220), 24, 2 * z);
    // the hand goes to what's touched (in the work: with the scroll, or writing over the paper)
    vec3 aim(0); bool aimOn = false;
    if (workAt >= 0) { aim = g_iv.hand; aimOn = true; }
    else if (live && hover >= 0) {
      aimOn = true;
      if (hover < 4) aim = inscribeLiePos(w, at, hover);
      else { const bool bookCol = hover < 4 + (int)bookList.size(); aim = linePos(bookCol ? 0 : 1, hover - (bookCol ? 4 : 4 + (int)bookList.size())) - up * 0.05f + toYou * 0.06f; }
    }
    cinematicSetAimPoint(aimOn && scene().phase == 2, aim);
    if (!pad && !overUi && live && hover >= 0) {
      if (ImGui::IsMouseClicked(0)) act(a, hover, false);
      else if (ImGui::IsMouseClicked(1)) act(a, hover, true);
    }
    target = hover >= 0 ? hover : target;
    g_iv.at = at; g_iv.viewYaw = scene().viewYaw; g_iv.hover = live && hover >= 0 && hover < 4 ? hover : -1;
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_inscribe", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    static const char* PLACE[4] = {"the spellbook", "the scroll", "the paper", "the lapis"};
    ImGui::Text("In hand: %s   \xC2\xB7   %s", nameOf(p.held()).c_str(), hover >= 0 && hover < 4 ? (std::string("Here: ") + PLACE[hover] + (v[hover].empty() ? "" : " (" + nameOf(v[hover]) + ")")).c_str() : "");
    if (const int slots = v[0].empty() ? 0 : bookSlots(v[0].id)) ImGui::TextDisabled("%s: %d of %d slots written", nameOf(v[0]).c_str(), (int)bookList.size(), slots);
    if (workAt >= 0) ImGui::Text("%s", g_iv.mode == 0 ? "Inscribing..." : g_iv.mode == 1 ? "Taking it out..." : "Writing...");
    else if (hover >= 4 && hover < 4 + (int)bookList.size()) ImGui::Text("Take %s out of the book as a scroll", spellLabel(bookList[hover - 4].first, bookList[hover - 4].second).c_str());
    else if (hover >= 4 + (int)bookList.size()) ImGui::Text("Write out %s (a paper and a lapis)", spellLabel(writeList[hover - 4 - (int)bookList.size()].first, writeList[hover - 4 - (int)bookList.size()].second).c_str());
    else if (hover == 1 && !v[0].empty() && isScroll(v[1]) && handFree(p)) ImGui::Text("Press the scroll into the book");
    else ImGui::TextDisabled("%s", v[0].empty() && v[1].empty() && v[2].empty() && v[3].empty() ? "Lay a spellbook, a spell scroll, paper and lapis on its board"
                                   : v[2].empty() || v[3].empty() ? "Paper and lapis laid here: write out a spell you know" : writeList.empty() ? "You know no spells to write yet: read a spell tome" : " ");
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the table"
                      : pad ? "D-pad the board and the spells \xC2\xB7 A lay it down / do it \xC2\xB7 X take back \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : "Left click the board: lay down what's in hand \xC2\xB7 empty hand on the scroll: press it into the book \xC2\xB7 click a spell: take it out or write it \xC2\xB7 Right click: take back");
    if (ImGui::Button("The table's page")) {
      const ivec3 pos = at;
      uiDefer([pos](App& a2) { if (a2.session && a2.session->player) a2.events.emit("screenOpen", json::array({a2.session->player->name, "station", json{{"kind", "inscribe"}, {"pos", {pos.x, pos.y, pos.z}}, {"classic", true}}})); });
      a.popScreen(); ImGui::End(); return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; int target = -1, bookScroll = 0, writeScroll = 0; bool pad = false, showBag = true, made = false; double workAt = -1;
  int pick = -1; std::string spell;
  std::vector<std::pair<std::string, int>> bookList, writeList;
  BagPad bag;
  void act(App& a, int k, bool take) {
    GameSession* s = a.session.get();
    if (!s || workAt >= 0 || scene().phase != 2) return;
    Player& p = *s->player; World& w = s->world();
    const std::vector<ItemStack> v = inscribeWorkItems(w, at);
    if (k < 4) {
      if (take) { cinematicTap(); inscribeWorkTake(*s->level, p, w, at, k); return; }
      if (!handFree(p)) { cinematicTap(); inscribeWorkPut(*s->level, p, w, at, p.held()); return; }
      if (k == 1 && !v[0].empty() && isScroll(v[1])) { begin(a, 0, -1, ""); return; }   // the scroll pressed into the book
      cinematicTap(); inscribeWorkTake(*s->level, p, w, at, k);
      return;
    }
    if (take) return;
    if (k < 4 + (int)bookList.size()) begin(a, 1, k - 4, "");
    else if (k - 4 - (int)bookList.size() < (int)writeList.size()) begin(a, 2, -1, writeList[k - 4 - bookList.size()].first);
  }
  void begin(App& a, int mode, int slot, const std::string& k) {
    GameSession* s = a.session.get();
    const std::vector<ItemStack> v = inscribeWorkItems(s->world(), at);
    if (mode == 0) {   // (said now, not after the scroll's flown: it won't go in)
      ItemStack b = v[0], sc = v[1];
      if (const std::string why = inscribeScroll(b, sc); !why.empty()) { toast(*s->level, *s->player, why); return; }
    }
    if (mode == 2 && !s->player->creative() && (v[2].empty() || v[3].empty())) return;
    workAt = scene().clock; made = false; pick = slot; spell = k;
    g_iv.mode = mode; g_iv.t = 0; g_iv.before[0] = v[0]; g_iv.before[1] = v[1]; g_iv.made = ItemStack();
    if (mode == 1 && slot >= 0 && slot < (int)bookList.size()) g_iv.made = makeScroll(bookList[slot].first, bookList[slot].second);
    if (mode == 2) g_iv.made = makeScroll(k, spellRank(*s->player, k));
    cinematicTap();
    if (a.audio) a.audio->play("book_page");
  }
  void tick(App& a, GameSession& s, Player& p, World& w) {
    const double t = scene().clock - workAt;
    g_iv.t = t;
    // where the hand is: carrying the scroll over; at the book's spine; writing to and fro over the paper
    const vec3 L0 = inscribeLiePos(w, at, 0), L1 = inscribeLiePos(w, at, 1), L2 = inscribeLiePos(w, at, 2);
    const vec3 rgt(std::cos(scene().viewYaw), 0, -std::sin(scene().viewYaw)), toYou(std::sin(scene().viewYaw), 0, std::cos(scene().viewYaw));
    if (g_iv.mode == 0) g_iv.hand = inscribeScrollPos(g_iv, L0, L1);
    else if (g_iv.mode == 1) g_iv.hand = t < INSCRIBE_CONTACT ? L0 + vec3(0, 0.02f, 0) : inscribeRisePos(g_iv, L0);
    else g_iv.hand = t < INSCRIBE_CONTACT ? L2 + rgt * (0.07f * (float)std::sin(t * 22.0)) + toYou * (0.06f - 0.12f * (float)(t / INSCRIBE_CONTACT)) + vec3(0, 0.015f, 0)
                                          : inscribeRisePos(g_iv, L2);
    if (g_iv.mode == 2 && t < INSCRIBE_CONTACT && (int)(t * 6) != (int)((t - 1 / 60.0) * 6)) cinematicTap();   // (the pen's strokes)
    if (!made && t >= INSCRIBE_CONTACT) {
      made = true;
      const std::string why = g_iv.mode == 0 ? inscribeWorkInscribe(*s.level, p, w, at) : g_iv.mode == 1 ? inscribeWorkExtract(*s.level, p, w, at, pick) : inscribeWorkWrite(*s.level, p, w, at, spell);
      if (!why.empty()) { workAt = -1; g_iv.t = -1; return; }
      if (s.level->fx) s.level->fx->particles("mana_arcane", (g_iv.mode == 2 ? L2 : L0) + vec3(0, 0.1f, 0), 10);
      if (a.audio && g_iv.mode == 0) a.audio->play("levelup", 0.5f);
    }
    if (t >= INSCRIBE_LENGTH) { workAt = -1; g_iv.t = -1; }
  }
};
}  // namespace

vec3 inscribeScrollPos(const InscribeView& v, vec3 book, vec3 scroll) {
  const float u = (float)std::clamp(v.t / INSCRIBE_CONTACT, 0.0, 1.0), e = u * u * (3 - 2 * u);
  return scroll + (book + vec3(0, 0.02f, 0) - scroll) * e + vec3(0, 0.12f * std::sin(e * PI), 0);
}
vec3 inscribeRisePos(const InscribeView& v, vec3 from) {
  const float u = (float)std::clamp((v.t - INSCRIBE_CONTACT) / (INSCRIBE_LENGTH - INSCRIBE_CONTACT), 0.0, 1.0), e = u * u * (3 - 2 * u);
  return from + vec3(0, 0.03f + 0.22f * e, 0) + vec3(std::sin(v.viewYaw), 0, std::cos(v.viewYaw)) * (0.35f * e * e);
}
bool cinematicInscribeView(InscribeView& v) { if (!g_ivOn || scene().phase == 0) return false; v = g_iv; return true; }
std::unique_ptr<Screen> makeCinematicInscribeScreen(ivec3 at) { return std::make_unique<CinematicInscribeScreen>(at); }

}  // namespace dq
