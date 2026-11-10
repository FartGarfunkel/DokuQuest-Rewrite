// ui/magic_screens.cpp — spellbook, skills, journal, enchanting table and brewing stand (plain ImGui; the
// wording, order and numbers are the original's — see ui/magic_screens.h).
#include "ui/magic_screens.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/icons.h"
#include "sim/magic.h"
#include "sim/enchanting.h"
#include "sim/brewing.h"
#include "sim/effects.h"
#include "sim/skills.h"
#include "sim/rpg.h"
#include "core/item_glint.h"
#include "ui/theme.h"
#include "ui/book.h"
#include "core/registry.h"
#include "world/world.h"
#include "common/noise.h"
#include <imgui.h>

namespace dq {
namespace ui { void padScrollHere(); }   // ui/theme.cpp: the right stick scrolls

static ImVec4 col4(const std::string& hex, float a = 1) { vec3 c = hexRGBf(hex); return ImVec4(c.r, c.g, c.b, a); }
static ImU32 col32(const std::string& hex, int a = 255) { u32 c = hexColor(hex); return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, a); }
static std::string thousands(double v) {
  std::string s = std::to_string((long long)std::floor(v)), o;
  for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) o += ','; o += s[i]; }
  return o;
}

// A dark panel in the middle of the screen; returns false if the window is collapsed/hidden.
// (the station screens were a dark panel of their own: they're written in the book now, below)

// (bookBegin / bookPageRight / bookEnd / bookTitle: ui/book.h, the pages every station screen is written on)
static void bar(float frac, ImU32 col, float w, float h = 6) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(0, 0, 0, 140), 3);
  dl->AddRectFilled(p, ImVec2(p.x + w * clampv(frac, 0.f, 1.f), p.y + h), col, 3);
  ImGui::Dummy(ImVec2(w, h));
}

std::string magicItemTooltip(const ItemStack& s0) {
  ItemStack s = s0;
  std::string out;
  json en = enchOf(s);
  if (en.is_object()) for (auto& [k, v] : en.items()) {
    const EnchDef* E = magicData().enchant(k);
    if (!E) continue;
    out += enchName(k, v.get<int>()) + (s.id == "enchanted_book" ? " \xC2\xB7 " + E->desc : "") + "\n";
  }
  std::string pt = potionTooltip(s.id);
  if (!pt.empty()) out += pt + "\n";
  const ItemDef* d = s.def();
  if (d && (d->hasTool || d->has("armor") || d->has("bow"))) {
    int L = gearLevel(s);
    out += "Level " + std::to_string(L) + (L >= GEAR_MAX ? " (max)" : "") + "\n";
  }
  if (d) { std::string desc = d->str("desc"); if (!desc.empty()) out += desc + "\n"; }
  return out;
}

// one item slot: an icon button with its count; returns true when clicked (A / click)
static bool slotButton(const char* id, const ItemStack& s, float size, const char* ghost = nullptr) {
  ImGui::PushID(id);
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::Button("##slot", ImVec2(size, size));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::string& key = !s.empty() ? s.id : ghost ? std::string(ghost) : std::string();
  if (!key.empty()) {
    IconRef ic = itemIcons().get(key);
    if (ic.valid()) dl->AddImage((ImTextureID)ic.tex, ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + size - 3, p.y + size - 3), ImVec2(ic.uv0.x, ic.uv0.y), ImVec2(ic.uv1.x, ic.uv1.y), s.empty() ? IM_COL32(255, 255, 255, 60) : IM_COL32_WHITE);
    else if (!s.empty()) dl->AddText(ImVec2(p.x + 4, p.y + 4), IM_COL32_WHITE, key.substr(0, 4).c_str());
  }
  if (!s.empty() && s.n > 1) { std::string n = std::to_string(s.n); ImVec2 ts = ImGui::CalcTextSize(n.c_str()); const ImVec2 q(p.x + size - ts.x - 3, p.y + size - ts.y - 1); dl->AddText(ImVec2(q.x + 1, q.y + 1), IM_COL32(20, 12, 6, 230), n.c_str()); dl->AddText(q, IM_COL32_WHITE, n.c_str()); }
  if (itemGlints(s)) ui::drawGlint(dl, s.id, ImVec2(p.x + 3, p.y + 3), size - 6);
  if (!s.empty() && ImGui::IsItemHovered()) {
    const ItemDef* d = s.def();
    std::string tip = magicItemTooltip(s);
    ImGui::BeginTooltip();
    ImGui::TextColored(itemGlints(s) ? ImVec4(0.56f, 0.91f, 1, 1) : ImVec4(1, 1, 1, 1), "%s", d ? d->name.c_str() : s.id.c_str());
    if (!tip.empty()) ImGui::TextDisabled("%s", tip.c_str());
    ImGui::EndTooltip();
  }
  ImGui::PopID();
  return clicked;
}

// the player's bag under a station panel: click (A) sends a stack to the station
template <class F> static void bagGrid(Player& p, float size, F onClick) {
  ImGui::TextDisabled("Inventory");
  ImGui::Dummy(ImVec2(1, 2));
  for (int row = 0; row < 4; row++) {
    for (int c = 0; c < 9; c++) {
      int i = row == 3 ? c : 9 + row * 9 + c;   // backpack rows, then the hotbar
      if (c) ImGui::SameLine(0, 3);
      if (slotButton(("bag" + std::to_string(i)).c_str(), p.inv[i], size)) onClick(i);
    }
    if (row == 2) ImGui::Dummy(ImVec2(1, 4));
  }
}

namespace {

class MagicScreenBase : public Screen {
public:
  bool pausesGame() const override { return false; }
  Player* player(App& a) { return a.session ? a.session->player : nullptr; }
  Level* level(App& a) { return a.session ? a.session->level.get() : nullptr; }
  // the key that opened a screen closes it again (not in the frame it opened)
  void closeOn(App& a, Act act) { if (a.input.pressed(act) && frames > 1) want = true; }
  void update(App& a, double) override { frames++; if (want || !a.session || !player(a) || player(a)->dead) { want = false; a.popScreen(); } }
  bool want = false;
  int frames = 0;
};

// ---- the books: spellbook, skills, journal (ui/book.h) ----------------------------------------------------------------
// a colour picked for the dark panels, deepened to read as ink on the paper
static ImU32 inkOf(const std::string& hex, float a = 1) { vec3 c = hexRGBf(hex); return ui::withAlpha(ui::inkOf(IM_COL32((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), 255)), a); }
// a whole row that takes clicks: returns 1 left, 2 right; hot when the pointer is on it
static int rowHit(ImVec2 p, float w, float h, bool* hot = nullptr) {
  ImGui::SetCursorScreenPos(p);
  bool l = ImGui::InvisibleButton("##row", ImVec2(std::max(1.f, w), std::max(1.f, h)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  if (hot) *hot = ImGui::IsItemHovered();
  if (!l) return 0;
  return ImGui::IsMouseReleased(ImGuiMouseButton_Right) ? 2 : 1;
}
// a line of the record: name on the left, value on the right
static void recordRow(ui::Book& b, ui::BookRows& r, const std::string& k, const std::string& v) {
  ui::BookRow row; row.h = ui::textSize(ui::F_BODY, "Ag").y + 4 * ui::Z();
  row.draw = [k, v](ImDrawList* dl, ImVec2 p, float w) {
    ui::text(dl, ui::F_BODY, p, ui::ink::text, k.c_str());
    ImVec2 s = ui::textSize(ui::F_BODY, v.c_str());
    float x0 = p.x + ui::textSize(ui::F_BODY, k.c_str()).x + 6 * ui::Z(), x1 = p.x + w - s.x - 6 * ui::Z(), y = p.y + s.y * 0.8f;
    for (float x = x0; x < x1; x += 4 * ui::Z()) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 1.5f * ui::Z(), y + 1.5f * ui::Z()), ui::ink::faint);   // the dotted leader
    ui::text(dl, ui::F_BODY, ImVec2(p.x + w - s.x, p.y), ui::ink::key, v.c_str());
  };
  (void)b; r.push_back(std::move(row));
}

class SpellbookScreen : public MagicScreenBase {
public:
  const char* id() const override { return "spellbook"; }
  void onOpen(App& a) override { if (Player* p = player(a); p && level(a)) { lv0 = activeSpell(*p); } }
  void draw(App& a) override {
    Player* p = player(a); Level* lv = level(a);
    if (!p || !lv) return;
    closeOn(a, Act::Spellbook);
    const MagicData& md = magicData();
    MagicState& m = magicState(*lv, *p);
    auto known = knownSpells(*p);
    auto knows = [&](const std::string& k) { return std::find(known.begin(), known.end(), k) != known.end(); };
    const int lvl = skillLv(*p, "magic");
    const std::string active = activeSpell(*p), left = offSpell(*p);
    // a bookmark per school (its first tome on the ribbon)
    std::vector<ui::BookTab> tabs; std::vector<std::vector<std::string>> lists; std::vector<const std::array<std::string, 3>*> sch;
    flat.clear();
    for (auto& sc : md.schools) {
      std::vector<std::string> ks;
      for (auto& k : md.spellOrder) if (md.spells.at(k).school == sc[0]) ks.push_back(k);
      if (ks.empty()) continue;
      std::string icon = "book";
      for (auto& k : ks) if (reg().item("tome_" + k)) { icon = "tome_" + k; break; }
      tabs.push_back({sc[1], icon}); lists.push_back(ks); sch.push_back(&sc);
      for (auto& k : ks) flat.push_back(k);
    }
    book.keysTab = false;   // X is the left hand here
    book.begin(a, "##spellbook", tabs);
    const int n = (int)flat.size();
    if (n) {
      Input& in = a.input;
      if (cur < 0) { cur = 0; for (int i = 0; i < n; i++) if (flat[i] == active) cur = i; book.showSel(cur); }
      if (in.pressed(Act::MenuDown)) { cur = (cur + 1) % n; book.showSel(cur); }
      if (in.pressed(Act::MenuUp)) { cur = (cur + n - 1) % n; book.showSel(cur); }
      cur = std::clamp(cur, 0, n - 1);
      if (in.pressed(Act::MenuOk) && knows(flat[cur])) pickSpell(*lv, *p, flat[cur]);
      if (in.pressed(Act::MenuX) && knows(flat[cur])) setOffSpell(*lv, *p, flat[cur]);
    }
    const float z = ui::Z(), lh = ui::textSize(ui::F_BODY, "Ag").y + 2 * z, sh = ui::textSize(ui::F_SMALL, "Ag").y + 2 * z;
    std::vector<ui::BookRows> secs;
    int idx = 0;
    for (size_t s = 0; s < lists.size(); s++) {
      ui::BookRows r;
      book.heading(r, (*sch[s])[1], inkOf((*sch[s])[2]));
      book.para(r, "School level " + std::to_string(schoolLv(*p, (*sch[s])[0])) + " \xC2\xB7 Magic " + std::to_string(lvl) + " \xC2\xB7 " +
        std::to_string((int)std::floor(m.mana < 0 ? manaMax(*p) : m.mana)) + " / " + std::to_string((int)manaMax(*p)) + " mana", ui::ink::muted, ui::F_SMALL);
      for (auto& k : lists[s]) {
        const SpellDef& S = md.spells.at(k);
        const bool kn = knows(k), R = k == active, Lh = kn && k == left;
        const std::string name = kn ? spellTitle(*p, k) : S.name;
        const std::string note = kn ? S.desc : std::string(lvl >= S.lvl ? "Find or craft its tome to learn it. " : "Needs Magic " + std::to_string(S.lvl) + ": cast spells to grow your Magic. ") + S.desc;
        auto lines = ui::Book::wrap(ui::F_SMALL, note, book.pageW() - 26 * z);
        const int me = idx++;
        ui::BookRow row; row.sel = me; row.h = lh + lines.size() * sh + 7 * z;
        row.draw = [this, a = &a, lv, p, k, kn, R, Lh, name, lines, lh, sh, z, me, col = S.col, lvlNeed = S.lvl, mana = S.mana](ImDrawList* dl, ImVec2 q, float w) {
          bool hot = false;
          const int c = rowHit(q, w, lh + lines.size() * sh + 4 * z, &hot);
          if (c == 1) cur = me;
          if (kn && c == 1) pickSpell(*lv, *p, k);
          if (kn && c == 2) { cur = me; setOffSpell(*lv, *p, k); }
          if (me == cur || hot) dl->AddRectFilled(ImVec2(q.x - 4 * z, q.y - 1 * z), ImVec2(q.x + w + 2 * z, q.y + lh + lines.size() * sh + 3 * z), me == cur ? ui::ink::hilite : ui::col::rgb(0xc8a050, 45), 3 * z);
          // which hand holds it: R, L, or both
          const char* hand = R && Lh ? "RL" : R ? "R" : Lh ? "L" : "";
          if (*hand) { ImVec2 hs = ui::textSize(ui::F_SMALL, hand); dl->AddRectFilled(q, ImVec2(q.x + hs.x + 6 * z, q.y + lh - 2 * z), ui::ink::head, 2 * z); ui::text(dl, ui::F_SMALL, ImVec2(q.x + 3 * z, q.y + (lh - 2 * z - hs.y) * 0.5f), ui::col::rgb(0xf3ead6), hand); }
          const float nx = q.x + 26 * z;
          ui::text(dl, ui::F_BODY, ImVec2(nx, q.y), kn ? inkOf(col) : ui::ink::muted, name.c_str());
          std::string cost = "Magic " + std::to_string(lvlNeed) + " \xC2\xB7 " + std::to_string(mana) + " mana";
          ImVec2 cs = ui::textSize(ui::F_SMALL, cost.c_str());
          ui::text(dl, ui::F_SMALL, ImVec2(q.x + w - cs.x, q.y + (lh - cs.y) * 0.5f), ui::ink::muted, cost.c_str());
          for (size_t i = 0; i < lines.size(); i++) ui::text(dl, ui::F_SMALL, ImVec2(nx, q.y + lh + i * sh), kn ? ui::ink::text : ui::ink::muted, lines[i].c_str());
          if (kn && hot) ImGui::SetTooltip("Rank %d of 10 \xC2\xB7 %d%% to the next", spellRank(*p, k), (int)(spellRankProgress(*p, k) * 100));
          (void)a;
        };
        r.push_back(std::move(row));
      }
      secs.push_back(std::move(r));
    }
    book.draw(secs);
    // turning the pages with the arrows: the choice moves onto the open pages
    if (book.turned && !book.shownSel.empty() && std::find(book.shownSel.begin(), book.shownSel.end(), cur) == book.shownSel.end()) cur = book.shownSel.front();
    book.hint(a.input.usingPad() ? "\xE2\x86\x91 \xE2\x86\x93 choose \xC2\xB7 A: right hand \xC2\xB7 X: left hand \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 turn the pages \xC2\xB7 B: close"
                                 : "Click: right hand \xC2\xB7 right-click: left hand \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 or the wheel: pages \xC2\xB7 B / Esc: close \xC2\xB7 Z / N cast (a staff makes it stronger)");
    book.end();
  }
  std::string lv0;
  ui::Book book;
  std::vector<std::string> flat;
  int cur = -1;
};

// ---- skills and perks (skills.js + rpg.js) ------------------------------------------------------------------------
class SkillsScreen : public MagicScreenBase {
public:
  const char* id() const override { return "skills"; }
  void draw(App& a) override {
    Player* p = player(a);
    if (!p) return;
    closeOn(a, Act::Skills);
    if (a.input.pressed(Act::Journal) && frames > 1) { want = true; swapTo = 1; }
    const MagicData& md = magicData();
    book.begin(a, "##skills", {{"Skills", "iron_pickaxe"}, {"Perks", "experience_bottle"}});
    const float z = ui::Z(), lh = ui::textSize(ui::F_BODY, "Ag").y + 2 * z;
    ui::BookRows sk, pk;
    book.heading(sk, "Skills");
    book.para(sk, "Total level " + std::to_string(totalLevel(*p)) + ". Skills grow by doing (Magic: casting spells, beating foes with them, learning tomes); each one's perks come as it levels.", ui::ink::muted, ui::F_SMALL);
    for (auto& s : md.skills) {
      const double xp = skillXp(*p, s.key);
      const int L = skillLevel(xp);
      const double lo = skillXpFor(L), hi = skillXpFor(std::min(99, L + 1));
      const float pr = L >= 99 ? 1.f : (float)((xp - lo) / std::max(1.0, hi - lo));
      std::string tip = thousands(xp) + " xp" + (L < 99 ? " \xC2\xB7 " + thousands(std::ceil(hi - xp)) + " to level " + std::to_string(L + 1) : std::string());
      ui::BookRow row; row.h = lh + 10 * z;
      row.draw = [name = s.name, col = s.col, L, pr, tip, lh, z](ImDrawList* dl, ImVec2 q, float w) {
        bool hot = false; rowHit(q, w, lh + 8 * z, &hot);
        ui::text(dl, ui::F_BODY, q, inkOf(col), name.c_str());
        std::string lv = std::to_string(L); ImVec2 ls = ui::textSize(ui::F_BODY, lv.c_str());
        ui::text(dl, ui::F_BODY, ImVec2(q.x + w - ls.x, q.y), ui::ink::text, lv.c_str());
        ImVec2 b0(q.x, q.y + lh + 1 * z), b1(q.x + w, q.y + lh + 6 * z);
        dl->AddRectFilled(b0, b1, ui::ink::faint);
        dl->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * std::clamp(pr, 0.f, 1.f), b1.y), inkOf(col));
        dl->AddRect(b0, b1, ui::withAlpha(ui::ink::text, 0.5f), 0, 0, std::max(1.f, z));
        if (hot) ImGui::SetTooltip("%s", tip.c_str());
      };
      sk.push_back(std::move(row));
    }
    // perks: the ones you have, then the nearest few to come
    struct P { std::string k, name, col, pn, desc; int lv; };
    std::vector<P> have, next;
    for (auto& s : md.skills) {
      int L = skillLv(*p, s.key);
      auto it = md.perks.find(s.key);
      if (it == md.perks.end()) continue;
      for (auto& q : it->second) (L >= q.lvl ? have : next).push_back({s.key, s.name, s.col, q.name, q.desc, q.lvl});
    }
    std::stable_sort(next.begin(), next.end(), [&](const P& x, const P& y) { return x.lv - skillLv(*p, x.k) < y.lv - skillLv(*p, y.k); });
    book.heading(pk, "Perks \xC2\xB7 " + std::to_string(have.size()) + " of " + std::to_string(have.size() + next.size()));
    auto perk = [&](const P& o, bool got) {
      ui::BookRow row; row.h = lh; row.keep = true;
      row.draw = [o, got](ImDrawList* dl, ImVec2 q, float) {
        ui::text(dl, ui::F_BODY, q, inkOf(o.col, got ? 1.f : 0.6f), o.pn.c_str());
        std::string t = "  " + o.name + " " + std::to_string(o.lv);
        ui::text(dl, ui::F_SMALL, ImVec2(q.x + ui::textSize(ui::F_BODY, o.pn.c_str()).x, q.y + 2 * ui::Z()), ui::ink::muted, t.c_str());
      };
      pk.push_back(std::move(row));
      book.para(pk, o.desc, got ? ui::ink::text : ui::ink::muted, ui::F_SMALL, 10 * z);
    };
    if (have.empty()) book.para(pk, "None yet. Each skill earns its perks as it levels up.", ui::ink::muted);
    for (auto& o : have) perk(o, true);
    if (!next.empty()) { book.gap(pk, 4 * z); book.sub(pk, "Coming next"); }
    for (size_t i = 0; i < next.size() && i < 8u; i++) perk(next[i], false);
    book.draw({sk, pk});
    book.hint("K: close \xC2\xB7 J: your journal \xC2\xB7 point at a skill for its experience");
    book.end();
  }
  void update(App& a, double dt) override {
    if (want && swapTo == 1) { want = false; swapTo = 0; a.popScreen(); a.pushScreen(makeJournalScreen()); return; }
    MagicScreenBase::update(a, dt);
  }
  int swapTo = 0;
  ui::Book book;
};

// ---- the journal (rpg.js) ------------------------------------------------------------------------------------------
class JournalScreen : public MagicScreenBase {
public:
  const char* id() const override { return "journal"; }
  void draw(App& a) override {
    Player* p = player(a); Level* lv = level(a);
    if (!p || !lv) return;
    closeOn(a, Act::Journal);
    if (a.input.pressed(Act::Skills) && frames > 1) { want = true; swapTo = 1; }
    Story s = storyOf(*lv, *p);
    book.begin(a, "##journal", {{"Your story", "book"}, {"Records", "clock"}});
    const float z = ui::Z(), lh = ui::textSize(ui::F_BODY, "Ag").y + 2 * z;
    ui::BookRows st, rec;
    book.heading(st, p->name + "'s journal");
    for (int i = 0; i < (int)s.ch.size(); i++) {
      auto& c = s.ch[i];
      const bool cur = i == s.cur;
      ui::BookRow row; row.h = lh + 2 * z; row.keep = true;
      const ImU32 tc = c.done ? ui::ink::good : cur ? ui::ink::head : ui::ink::text;
      const int mark = c.done ? 2 : cur ? 1 : 0;   // a tick, a pointer, a dot (drawn: the pixel font has no tick)
      std::string tag = c.side ? "  (the long way round)" : c.epi ? "  (after)" : "";
      row.draw = [mark, t = c.t, tag, tc, cur, z, lh](ImDrawList* dl, ImVec2 q, float w) {
        if (cur) dl->AddRectFilled(ImVec2(q.x - 4 * z, q.y - 1 * z), ImVec2(q.x + w + 2 * z, q.y + lh + 1 * z), ui::ink::hilite, 3 * z);
        const float s = lh * 0.55f; const ImVec2 m(q.x + 1 * z, q.y + (lh - s) * 0.5f - 1 * z);
        if (mark == 2) ui::drawTick(dl, m, s, tc);
        else if (mark == 1) dl->AddTriangleFilled(m, ImVec2(m.x + s * 0.8f, m.y + s * 0.5f), ImVec2(m.x, m.y + s), tc);
        else dl->AddRectFilled(ImVec2(m.x + s * 0.3f, m.y + s * 0.4f), ImVec2(m.x + s * 0.5f, m.y + s * 0.6f), tc);
        ui::text(dl, ui::F_BODY, ImVec2(q.x + 16 * z, q.y), tc, t.c_str());
        if (!tag.empty()) ui::text(dl, ui::F_SMALL, ImVec2(q.x + 16 * z + ui::textSize(ui::F_BODY, t.c_str()).x, q.y + 2 * z), ui::ink::muted, tag.c_str());
      };
      st.push_back(std::move(row));
      book.para(st, c.goal, c.done ? ui::ink::muted : ui::ink::text, ui::F_SMALL, 16 * z);
      if (cur || (!c.done && (c.side || c.epi))) book.para(st, c.hint, ui::ink::key, ui::F_SMALL, 16 * z);
      if (c.prog >= 0 && !c.done) book.bar(st, c.prog, ui::ink::key, 16 * z);
      book.gap(st, 4 * z);
    }
    book.heading(rec, "Records");
    for (auto& [k, v] : journalRecords(*lv, *p)) recordRow(book, rec, k, v);
    auto foes = greatFoesBeaten(*lv);
    book.gap(rec, 6 * z); book.sub(rec, "Great foes beaten");
    if (foes.empty()) book.para(rec, "None yet.", ui::ink::muted);
    for (auto& f : foes) book.bullet(rec, f);
    book.draw({st, rec});
    book.hint("J: close \xC2\xB7 K: your skills and perks \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 or the wheel: turn the pages");
    book.end();
  }
  void update(App& a, double dt) override {
    if (want && swapTo == 1) { want = false; swapTo = 0; a.popScreen(); a.pushScreen(makeSkillsScreen()); return; }
    MagicScreenBase::update(a, dt);
  }
  int swapTo = 0;
  ui::Book book;
};

// rune glyphs drawn as strokes (no font has the runic block): each index is a little stave
static void drawRunes(ImDrawList* dl, ImVec2 p, const std::vector<int>& runes, float h, ImU32 c) {
  float x = p.x;
  for (int r : runes) {
    if (r < 0) { x += h * 0.4f; continue; }
    u32 k = hash32(r, 7, 13, 0);
    float w = h * 0.45f;
    dl->AddLine(ImVec2(x + w * 0.5f, p.y), ImVec2(x + w * 0.5f, p.y + h), c, 1.5f);
    for (int s = 0; s < 2; s++) {
      u32 b = (k >> (s * 8)) & 255;
      float y0 = p.y + h * (0.1f + (b & 3) * 0.2f), y1 = y0 + h * (((b >> 2) & 1) ? 0.3f : -0.2f);
      float xa = x + w * 0.5f, xb = ((b >> 3) & 1) ? x + w : x;
      if ((b >> 4) & 1) dl->AddLine(ImVec2(xa, y0), ImVec2(xb, y1), c, 1.5f);
    }
    x += w + 3;
  }
}

// ---- the enchanting table (enchanting.js) ----------------------------------------------------------------------------
class EnchantScreen : public MagicScreenBase {
public:
  explicit EnchantScreen(ivec3 at) : pos(at) {}
  const char* id() const override { return "enchant"; }
  ivec3 pos;
  ItemStack item, lapis;
  void giveBack(App& a) {
    Player* p = player(a); Level* lv = level(a);
    if (!p || !lv) return;
    if (!item.empty()) giveOrDrop(*lv, *p, item);
    if (!lapis.empty()) giveOrDrop(*lv, *p, lapis);
    item.clear(); lapis.clear();
  }
  void onClose(App& a) override { giveBack(a); }
  void draw(App& a) override {
    Player* p = player(a); Level* lv = level(a);
    if (!p || !lv) return;
    World& w = lv->world(p->realm);
    if (reg().block(w.getBlock(pos.x, pos.y, pos.z)).key != "enchanting_table") { want = true; return; }
    float sz = ui::bookPagesBegin(a, "##enchant");
    ui::bookPageTitle(a, "Enchanting Table");
    int shelves = enchShelves(w, pos.x, pos.y, pos.z);
    std::array<EnchOffer, 3> offers;
    bool has = !item.empty() && enchOffers(item, shelves, enchSeed(*p), offers);
    int lap = lapis.id == "lapis_lazuli" ? lapis.n : 0;
    bool cr = p->creative();
    ImGui::BeginGroup();
    if (slotButton("item", item, sz, "iron_sword") && !item.empty()) { giveOrDrop(*lv, *p, item); item.clear(); }
    if (slotButton("lapis", lapis, sz, "lapis_lazuli") && !lapis.empty()) { giveOrDrop(*lv, *p, lapis); lapis.clear(); }
    ImGui::EndGroup();
    ImGui::SameLine(0, 12);
    ImGui::BeginGroup();
    for (int i = 0; i < 3; i++) {
      ImGui::PushID(i);
      float rw = ImGui::GetContentRegionAvail().x, rh = sz * 0.8f;
      ImVec2 p0 = ImGui::GetCursorScreenPos();
      bool can = has && (cr || (p->level >= offers[i].level && lap >= offers[i].cost));
      if (!has) ImGui::BeginDisabled();
      if (ImGui::Button("##offer", ImVec2(rw, rh)) && has) {
        ItemStack left;
        std::string why = enchApplyOffer(*lv, *p, item, lapis, i, shelves, vec3(pos), &left);
        if (!why.empty()) a.toast(why, "warn");
        if (!left.empty()) giveOrDrop(*lv, *p, left);
      }
      if (!has) ImGui::EndDisabled();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (has) {
        const EnchOffer& o = offers[i];
        std::string dots; for (int c = 0; c < o.cost; c++) dots += "\xE2\x97\x8F";
        dl->AddText(ImVec2(p0.x + 6, p0.y + 4), can ? ui::inkOf(IM_COL32(90, 200, 255, 255)) : ui::ink::muted, dots.c_str());
        drawRunes(dl, ImVec2(p0.x + 50, p0.y + 5), enchRunes((u32)(p->stats.value("enchSeed", 0u) + i * 131 + o.level), 9 + i * 3), rh * 0.4f, can ? ui::inkOf(IM_COL32(220, 210, 255, 220)) : ui::ink::faint);
        std::string hint = o.order.empty() ? "" : enchName(o.order[0], o.en[o.order[0]].get<int>()) + (o.order.size() > 1 ? " \xE2\x80\xA6" : "");
        dl->AddText(ImVec2(p0.x + 50, p0.y + rh * 0.55f), can ? ui::ink::text : ui::ink::muted, hint.c_str());
        std::string L = std::to_string(o.level);
        ImVec2 ts = ImGui::CalcTextSize(L.c_str());
        dl->AddText(ImVec2(p0.x + rw - ts.x - 8, p0.y + (rh - ts.y) / 2), p->level >= o.level || cr ? ui::ink::good : ui::ink::bad, L.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndGroup();
    std::string status = !item.empty() && !has ? (enchKind(item.id).empty() || !(item.tag.is_object() && item.tag.contains("en")) ? "That can't be enchanted" : "Already enchanted \xE2\x80\x94 use books at an anvil to add more")
      : item.empty() ? "Put in something to enchant, and lapis \xC2\xB7 " + std::to_string(shelves) + " bookshel" + (shelves == 1 ? "f" : "ves") + " around (15 for the strongest)"
      : "Costs the levels shown on the right, and 1-3 lapis \xC2\xB7 you have " + std::to_string(p->level) + " level" + (p->level == 1 ? "" : "s");
    if (!item.empty() && !has && !enchKind(item.id).empty() && item.tag.is_object() && item.tag.contains("en")) status = "Already enchanted \xE2\x80\x94 use books at an anvil to add more";
    ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", status.c_str()); ImGui::PopTextWrapPos();
    ui::bookPagesRight("##enchant-bag");
    bagGrid(*p, sz, [&](int i) {
      ItemStack& s = p->inv[i];
      if (s.empty()) return;
      if (s.id == "lapis_lazuli") {
        if (lapis.empty()) { lapis = s; s.clear(); }
        else { int mv = std::min(64 - lapis.n, s.n); lapis.n += mv; s.n -= mv; if (s.n <= 0) s.clear(); }
      } else if (item.empty() && !enchKind(s.id).empty() && enchKind(s.id) != "ebook") { item = s; item.n = 1; if (--s.n <= 0) s.clear(); }
    });
    ui::bookPagesEnd();
  }
};

// ---- the brewing stand (brewing.js) ------------------------------------------------------------------------------------
class BrewScreen : public MagicScreenBase {
public:
  explicit BrewScreen(ivec3 at) : pos(at) {}
  const char* id() const override { return "brew"; }
  ivec3 pos;
  void draw(App& a) override {
    Player* p = player(a); Level* lv = level(a);
    if (!p || !lv) return;
    World& w = lv->world(p->realm);
    if (reg().block(w.getBlock(pos.x, pos.y, pos.z)).key != "brewing_stand") { want = true; return; }
    json& e = brewEntity(w, pos.x, pos.y, pos.z);
    float sz = ui::bookPagesBegin(a, "##brew");
    ui::bookPageTitle(a, "Brewing Stand");
    auto at = [&](int i) { return ItemStack::fromJson(e["items"][i]); };
    bool changed = false;
    auto take = [&](int i) { ItemStack s = at(i); if (s.empty()) return; giveOrDrop(*lv, *p, s); e["items"][i] = nullptr; changed = true; };
    static const char* ghosts[5] = {"potion_water", "potion_water", "potion_water", "nether_wart_item", "cinder_powder"};
    // fuel on the left, the ingredient over the tube, three bottles below
    float x0 = ImGui::GetCursorPosX(), y0 = ImGui::GetCursorPosY();
    ImGui::SetCursorPos(ImVec2(x0, y0 + sz * 0.2f));
    if (slotButton("fuel", at(4), sz, ghosts[4])) take(4);
    ImGui::SetCursorPosX(x0); bar(brewFuel(e), IM_COL32(255, 140, 40, 255), sz, 6);
    float cx = x0 + sz * 3.2f;
    ImGui::SetCursorPos(ImVec2(cx, y0));
    if (slotButton("ing", at(3), sz, ghosts[3])) take(3);
    ImVec2 tube = ImGui::GetCursorScreenPos(); tube.x += sz * 0.4f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float th = sz * 0.9f;
    dl->AddRectFilled(tube, ImVec2(tube.x + sz * 0.2f, tube.y + th), IM_COL32(60, 40, 20, 90), 2);
    dl->AddRectFilled(tube, ImVec2(tube.x + sz * 0.2f, tube.y + th * brewProgress(e)), IM_COL32(90, 140, 210, 230), 2);
    ImGui::SetCursorPos(ImVec2(cx - sz * 1.2f, y0 + sz + th + 6));
    for (int i = 0; i < 3; i++) { if (i) ImGui::SameLine(0, sz * 0.2f); if (slotButton(("b" + std::to_string(i)).c_str(), at(i), sz, ghosts[i])) take(i); }
    std::string st = brewStatus(e);
    ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", st.empty() ? " " : st.c_str()); ImGui::PopTextWrapPos();
    ui::bookPagesRight("##brew-bag");
    bagGrid(*p, sz, [&](int i) {
      ItemStack& s = p->inv[i];
      if (s.empty()) return;
      if (potionInfo(s.id)) {   // one bottle to a stand slot (potions stack to 16): as many empty slots as the stack fills
        for (int k = 0; k < 3 && !s.empty(); k++) if (at(k).empty()) { ItemStack one = s; one.n = 1; e["items"][k] = one.toJson(); if (--s.n <= 0) s.clear(); changed = true; }
        return;
      }
      for (int k : {3, 4}) {
        if (!brewAccepts(k, s.id)) continue;
        ItemStack b = at(k);
        if (b.empty()) { e["items"][k] = s.toJson(); s.clear(); changed = true; break; }
        if (b.id == s.id) { int mv = std::min(s.maxStack() - b.n, s.n); b.n += mv; s.n -= mv; if (s.n <= 0) s.clear(); e["items"][k] = b.toJson(); changed = true; break; }
      }
    });
    if (changed) w.blockEntityChanged(pos.x, pos.y, pos.z);
    ui::bookPagesEnd();
  }
};

} // namespace

std::unique_ptr<Screen> makeSpellbookScreen() { return std::make_unique<SpellbookScreen>(); }
std::unique_ptr<Screen> makeSkillsScreen() { return std::make_unique<SkillsScreen>(); }
std::unique_ptr<Screen> makeJournalScreen() { return std::make_unique<JournalScreen>(); }
std::unique_ptr<Screen> makeEnchantScreen(ivec3 at) { return std::make_unique<EnchantScreen>(at); }
std::unique_ptr<Screen> makeBrewScreen(ivec3 at) { return std::make_unique<BrewScreen>(at); }

} // namespace dq
