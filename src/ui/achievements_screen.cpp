// ui/achievements_screen.cpp — the Achievements tab (sim/achievements.h), written in the guidebook like the other menu
// tabs (ui/book.h): an Overview bookmark (how many you've earned, each world's share, the latest few) and a bookmark for
// each world BTA keeps an achievement page for (the Overworld, the Nether) and the End. Each world's achievements run in
// the order you'd earn them (each after the one that leads to it), each in the pack's own advancement frame — task,
// goal or challenge, gold once earned — with its icon, what it asks, and when you earned it or what comes first.
// Controls: as every book (arrows / wheel / d-pad turn the pages, a bookmark or X / Tab jumps); LB / RB (Q / E) change
// menu tab; B, Esc or the inventory button close.
#include "ui/ui.h"
#include "ui/theme.h"
#include "ui/book.h"
#include "assets/gui_textures.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/achievements.h"
#include "sim/player.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <map>

namespace dq {

bool inventoryClosePressed(App& a, bool tabScreen);   // ui/ui_main.cpp

namespace {
using namespace ui;

enum Frame { TASK, GOAL, CHALLENGE };
struct Place { const char* id; int page; int x, y; const char* parent; Frame f; };
// where each one sits (grid cells) and what leads to it
const std::vector<Place>& places() {
  static const std::vector<Place> P = {
    {"wood", 0, 0, 3, nullptr, TASK}, {"bench", 0, 2, 3, "wood", TASK}, {"pick", 0, 4, 3, "bench", TASK},
    {"furnace", 0, 6, 3, "pick", TASK}, {"iron", 0, 8, 3, "furnace", TASK}, {"diamond", 0, 10, 3, "iron", GOAL},
    {"forge", 0, 10, 5, "iron", TASK}, {"sword", 0, 4, 1, "bench", TASK}, {"monster", 0, 6, 1, "sword", TASK},
    {"tame", 0, 8, 1, "monster", TASK}, {"leather", 0, 6, 0, "sword", TASK}, {"bookshelf", 0, 8, 0, "leather", TASK},
    {"enchant", 0, 10, 0, "bookshelf", GOAL}, {"spell", 0, 12, 0, "enchant", TASK}, {"hoe", 0, 4, 5, "bench", TASK},
    {"bread", 0, 6, 5, "hoe", TASK}, {"fish", 0, 2, 5, "bench", TASK}, {"sleep", 0, 0, 5, "wood", TASK},
    {"paint", 0, 4, 6, "bench", TASK}, {"village", 0, 0, 0, nullptr, TASK}, {"sky", 0, 0, 1, nullptr, GOAL},
    {"nether", 1, 0, 1, nullptr, GOAL}, {"blaze", 1, 2, 1, "nether", TASK}, {"wither", 1, 4, 1, "blaze", CHALLENGE},
    {"storm", 1, 6, 1, "wither", CHALLENGE},
    {"end", 2, 0, 1, nullptr, GOAL}, {"dragon", 2, 2, 1, "end", CHALLENGE}, {"glide", 2, 4, 1, "dragon", TASK},
  };
  return P;
}
struct PageDef { const char* name; const char* icon; const char* bg; };
const PageDef PAGES[] = {{"Overworld", "crafting_table", "stone"}, {"Nether", "netherrack", "nether"}, {"The End", "end_stone", "end"}};
constexpr int NPAGES = 3;

GuiSprite pic(const std::string& n) {
  GuiSprite s = guiTexture("gui/" + n + ".png");
  if (!s.valid()) s = guiTexture("gui/sprites/advancements/" + n + ".png");
  if (!s.valid()) s = guiTexture("gui/advancements/backgrounds/" + n + ".png");
  return s;
}
void img(ImDrawList* dl, const GuiSprite& s, ImVec2 a, ImVec2 b, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1), ImU32 tint = IM_COL32_WHITE) {
  dl->AddImage((ImTextureID)(intptr_t)s.tex, a, b, uv0, uv1, tint);
}
class AchievementsScreen : public Screen {
public:
  const char* id() const override { return "achievements"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override {
    if (inventoryClosePressed(a, true)) a.popScreen();   // (Esc / B: the app closes the top screen)
  }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player;
    const json* got = p.data.is_object() && p.data.contains("ach") && p.data["ach"].is_object() ? &p.data["ach"] : nullptr;
    auto earned = [&](const char* id) { return got && got->contains(id); };
    auto when = [&](const char* id) -> long long { return earned(id) && (*got)[id].is_number() ? (*got)[id].get<long long>() : 0; };
    const auto& L = achievementList();
    std::map<std::string, const AchDef*> byId; for (auto& d : L) byId[d.id] = &d;
    // where each one goes (any the table doesn't know: the Overworld, at the end), and in what order: each after the
    // one that leads to it, the roots in the table's order
    std::vector<Place> pl;
    for (auto& q : places()) if (byId.count(q.id)) pl.push_back(q);
    for (auto& d : L) { bool known = false; for (auto& q : pl) if (std::string(q.id) == d.id) known = true; if (!known) pl.push_back({d.id, 0, 0, 99, nullptr, TASK}); }
    std::vector<std::vector<const Place*>> order(NPAGES);
    std::function<void(const Place&)> visit = [&](const Place& q) {
      order[q.page].push_back(&q);
      for (auto& c : pl) if (c.parent && std::string(c.parent) == q.id) visit(c);
    };
    for (auto& q : pl) if (!q.parent || !byId.count(q.parent)) visit(q);
    int n = 0; for (auto& d : L) if (earned(d.id)) n++;

    std::vector<ui::BookTab> tabs = {{"Overview", "nether_star"}};
    for (int i = 0; i < NPAGES; i++) tabs.push_back({PAGES[i].name, PAGES[i].icon});
    book.begin(a, "##achievements", tabs);
    const float z = ui::Z(), lh = ui::textSize(ui::F_BODY, "Ag").y + 2 * z, sh = ui::textSize(ui::F_SMALL, "Ag").y + 2 * z;
    std::vector<ui::BookRows> secs(1 + NPAGES);
    // ---- the overview ----
    {
      ui::BookRows& r = secs[0];
      book.heading(r, "Achievements");
      book.para(r, std::to_string(n) + " of " + std::to_string(L.size()) + " earned. Each world's are on its own bookmark, in the order you'd come to them.", ui::ink::muted);
      book.bar(r, L.empty() ? 0.f : (float)n / L.size(), ui::ink::key);
      book.gap(r, 6 * z);
      for (int i = 0; i < NPAGES; i++) {
        int have = 0; for (auto* q : order[i]) if (earned(q->id)) have++;
        const std::string nm = PAGES[i].name, cnt = std::to_string(have) + " of " + std::to_string(order[i].size());
        ui::BookRow row; row.h = lh + 2 * z; row.keep = true;
        row.draw = [nm, cnt, icon = std::string(PAGES[i].icon), lh, z](ImDrawList* dl, ImVec2 q, float w) {
          ui::drawIcon(dl, icon, ImVec2(q.x, q.y + 1 * z), lh - 2 * z);
          ui::text(dl, ui::F_BODY, ImVec2(q.x + lh + 4 * z, q.y), ui::ink::text, nm.c_str());
          ImVec2 cs = ui::textSize(ui::F_BODY, cnt.c_str()); ui::text(dl, ui::F_BODY, ImVec2(q.x + w - cs.x, q.y), ui::ink::key, cnt.c_str());
        };
        r.push_back(std::move(row));
        book.bar(r, order[i].empty() ? 0.f : (float)have / order[i].size(), ui::ink::good, lh + 4 * z);
      }
      // the latest few
      std::vector<std::pair<long long, const AchDef*>> recent;
      for (auto& d : L) if (earned(d.id)) recent.push_back({when(d.id), &d});
      std::sort(recent.begin(), recent.end(), [](auto& x, auto& y) { return x.first > y.first; });
      book.gap(r, 8 * z); book.sub(r, "Lately");
      if (recent.empty()) book.para(r, "Nothing yet. Punch a tree!", ui::ink::muted);
      for (size_t i = 0; i < recent.size() && i < 6; i++) entry(r, *recent[i].second, framOf(pl, recent[i].second->id), true, recent[i].first, nullptr, lh, sh);
    }
    // ---- each world ----
    for (int i = 0; i < NPAGES; i++) {
      ui::BookRows& r = secs[1 + i];
      int have = 0; for (auto* q : order[i]) if (earned(q->id)) have++;
      book.heading(r, PAGES[i].name);
      book.para(r, std::to_string(have) + " of " + std::to_string(order[i].size()) + " earned", ui::ink::muted, ui::F_SMALL);
      for (auto* q : order[i]) {
        const AchDef* d = byId[q->id];
        const AchDef* par = q->parent && byId.count(q->parent) ? byId[q->parent] : nullptr;
        entry(r, *d, q->f, earned(q->id), when(q->id), par && !earned(par->id) ? par : nullptr, lh, sh);
      }
    }
    book.draw(secs);
    book.hint(a.input.usingPad() ? "D-pad: turn the pages \xC2\xB7 X: next bookmark \xC2\xB7 LB / RB: menu tabs \xC2\xB7 B: close"
                                 : "\xE2\x86\x90 \xE2\x86\x92 or the wheel: turn the pages \xC2\xB7 Tab or a bookmark: next world \xC2\xB7 Q / E: menu tabs \xC2\xB7 Esc: close");
    book.end();
  }
private:
  ui::Book book;
  static Frame framOf(const std::vector<Place>& pl, const char* id) { for (auto& q : pl) if (std::string(q.id) == id) return q.f; return TASK; }
  // one achievement: its frame (the pack's, gold when earned) with the icon in it, the title, what it asks, and when
  // it was earned — or, not yet, what comes before it
  void entry(ui::BookRows& r, const AchDef& d, Frame f, bool ok, long long t, const AchDef* needs, float lh, float sh) {
    const float z = ui::Z(), fs = std::round(lh * 2.1f), tx = fs + 8 * z;
    auto lines = ui::Book::wrap(ui::F_SMALL, d.desc, book.pageW() - tx);
    std::string foot;
    if (ok && t > 0) { std::time_t tt = (std::time_t)t; char b[40]; std::strftime(b, sizeof b, "Earned %d %b %Y", std::localtime(&tt)); foot = b; }
    else if (ok) foot = "Earned";
    else if (needs) foot = std::string("After: ") + needs->title;
    ui::BookRow row; row.h = std::max(fs, lh + lines.size() * sh + (foot.empty() ? 0 : sh)) + 7 * z;
    row.draw = [title = std::string(d.title), icon = std::string(d.icon), lines, foot, f, ok, fs, tx, lh, sh, z](ImDrawList* dl, ImVec2 q, float) {
      const char* fn = f == GOAL ? (ok ? "goal_frame_obtained" : "goal_frame_unobtained") : f == CHALLENGE ? (ok ? "challenge_frame_obtained" : "challenge_frame_unobtained")
                                 : (ok ? "task_frame_obtained" : "task_frame_unobtained");
      GuiSprite fr = pic(fn);
      if (fr.valid()) img(dl, fr, q, ImVec2(q.x + fs, q.y + fs));
      else { dl->AddRectFilled(q, ImVec2(q.x + fs, q.y + fs), ui::col::rgb(0x2b2017), 4 * z); dl->AddRect(q, ImVec2(q.x + fs, q.y + fs), ok ? ui::col::rgb(0xe2b84f) : ui::col::rgb(0x8a8078), 4 * z, 0, 2 * z); }
      const float is = fs * 16.f / 26.f;
      ui::drawIcon(dl, icon, ImVec2(std::round(q.x + (fs - is) * 0.5f), std::round(q.y + (fs - is) * 0.5f)), is, ok ? 1.f : 0.55f);
      ui::text(dl, ui::F_BODY, ImVec2(q.x + tx, q.y), ok ? ui::ink::head : ui::ink::muted, title.c_str());
      float y = q.y + lh;
      for (auto& ln : lines) { ui::text(dl, ui::F_SMALL, ImVec2(q.x + tx, y), ok ? ui::ink::text : ui::ink::muted, ln.c_str()); y += sh; }
      if (!foot.empty()) ui::text(dl, ui::F_SMALL, ImVec2(q.x + tx, y), ok ? ui::ink::good : ui::ink::key, foot.c_str());
    };
    r.push_back(std::move(row));
  }
};
} // namespace

std::unique_ptr<Screen> makeAchievementsScreen() { return std::make_unique<AchievementsScreen>(); }

} // namespace dq
