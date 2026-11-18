// ui/magic_screens.cpp — spellbook, skills, journal, enchanting table and brewing stand (plain ImGui; the
// wording, order and numbers are the original's — see ui/magic_screens.h).
#include "sim/spell_items.h"
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
#include "sim/guidance.h"
#include "core/item_glint.h"
#include "ui/theme.h"
#include "ui/glyphs.h"
#include "ui/book.h"
#include "core/registry.h"
#include "world/world.h"
#include "common/noise.h"
#include <imgui.h>
#include "render/creature_portrait.h"
#include "sim/creature.h"
#include "common/fs.h"
#include <map>
#include <cstring>

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
  if (const std::string g = gemSocketsLine(s); !g.empty()) out += g + "\n";
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
    { const ImVec2 ia = ImGui::GetItemRectMin(), ib = ImGui::GetItemRectMax(); ui::tipNextAt(&ia, &ib); }   // (the pad: by the highlighted slot)
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
  bool l = ImGui::InvisibleButton("##row", ImVec2(std::max(1.f, w), std::max(1.f, h)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  if (hot) *hot = ImGui::IsItemHovered();
  if (!l) return 0;
  return ImGui::IsMouseReleased(ImGuiMouseButton_Right) ? 2 : ImGui::IsMouseReleased(ImGuiMouseButton_Middle) ? 3 : 1;
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
    const std::string activeAny = activeSpell(*p), active = rightSpellReady(*p) ? activeAny : std::string(), left = offSpell(*p), quick = quickSpell(*p);
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
    tabs.insert(tabs.begin(), ui::BookTab{"Tree of Magic", "enchanted_book"});   // (the first page: the whole tree)
    book.keysTab = false;   // X is the left hand here
    book.begin(a, "##spellbook", tabs);
    const int n = (int)flat.size();
    if (n) {
      Input& in = a.input;
      if (cur < 0) { cur = 0; for (int i = 0; i < n; i++) if (flat[i] == activeAny) cur = i; book.showSel(cur); }
      if (in.pressed(Act::MenuDown)) { cur = (cur + 1) % n; book.showSel(cur); }
      if (in.pressed(Act::MenuUp)) { cur = (cur + n - 1) % n; book.showSel(cur); }
      cur = std::clamp(cur, 0, n - 1);
      if (in.pressed(Act::MenuOk) && knows(flat[cur])) toggleRightSpell(*lv, *p, flat[cur]);
      if (in.pressed(Act::MenuX) && knows(flat[cur])) setOffSpell(*lv, *p, flat[cur]);
      if (in.pressed(Act::MenuY) && knows(flat[cur])) setQuickSpell(*lv, *p, flat[cur]);
    }
    const float z = ui::Z(), lh = ui::textSize(ui::F_BODY, "Ag").y + 2 * z, sh = ui::textSize(ui::F_SMALL, "Ag").y + 2 * z;
    std::vector<ui::BookRows> secs;
    int idx = 0;
    {   // the Tree of Magic: one page, the ten schools branching from the middle in their colours, each school's spells growing out
        // along its branch in the order they open (by Magic level); learnt ones lit, the next within reach outlined, the rest
        // faint; the right hand's ringed, the left's in gold. Click: the right hand; right-click: the left
      ui::BookRows tr;
      book.heading(tr, "The Tree of Magic");
      // (across the whole spread: the right page is left to it; the ten branches on an ellipse filling both pages)
      ui::BookRow row; const float headH = tr.empty() ? 0.f : tr.back().h; row.h = std::max(book.pageW(), book.pageH() - headH - 2); row.wide = true;
      const float treeH = row.h;
      std::vector<std::pair<std::string, std::vector<std::string>>> br;
      for (size_t i = 0; i < lists.size(); i++) br.push_back({(*sch[i])[2], lists[i]});
      std::vector<std::string> names, skeys; for (size_t i = 0; i < lists.size(); i++) { names.push_back((*sch[i])[1]); skeys.push_back((*sch[i])[0]); }
      row.draw = [this, lv, p, br, names, skeys, z, lvl, active, left, known, treeH](ImDrawList* dl, ImVec2 q, float w) {
        auto kn = [&](const std::string& k) { return std::find(known.begin(), known.end(), k) != known.end(); };
        const ImVec2 c(q.x + w * 0.5f, q.y + treeH * 0.5f);
        // laid out over the whole spread (rx across, ry down): the longest branches get the most level directions (the
        // spread is wide), each branch's spells spaced along it to fill the room it has to the edge
        const float rx = w * 0.5f - 6 * z, ry = treeH * 0.5f - 4 * z, rmin = std::min(rx, ry);
        const float nr = std::max(8.f * z, rmin * 0.05f);
        const float R0 = rmin * 0.12f, R1 = rmin * 0.27f;
        auto at = [&](float a, float r) { return ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r); };
        auto reach = [&](float a) { const float ca = std::cos(a) / rx, sa = std::sin(a) / ry; return 1.f / std::sqrt(ca * ca + sa * sa); };   // (centre to the ellipse's edge)
        dl->AddCircleFilled(c, R0, ui::withAlpha(ui::ink::key, 0.35f), 32); dl->AddCircle(c, R0, ui::ink::key, 32, 2 * z);
        ui::drawGlyph(dl, ui::Glyph::Orb, c, R0 * 0.62f, ui::ink::key);
        const int B = (int)br.size();
        std::vector<int> slotOf(B);
        {   // the slots round the ring, most level first; the branches, longest first
          std::vector<int> slots(B), order(B);
          for (int i = 0; i < B; i++) slots[i] = order[i] = i;
          std::stable_sort(slots.begin(), slots.end(), [&](int x, int y) { return std::fabs(std::cos(x * 6.2832f / B)) > std::fabs(std::cos(y * 6.2832f / B)); });
          std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return br[x].second.size() > br[y].second.size(); });
          for (int i = 0; i < B; i++) slotOf[order[i]] = slots[i];
        }
        for (int b = 0; b < B; b++) {
          const float ang = slotOf[b] * 6.2832f / B + 0.0001f;
          const ImVec2 dir(std::cos(ang), std::sin(ang));
          const ImU32 sc = inkOf(br[b].first);
          std::vector<std::string> o = br[b].second;
          std::stable_sort(o.begin(), o.end(), [](const std::string& x, const std::string& y) { return magicData().spells.at(x).lvl < magicData().spells.at(y).lvl; });
          const ImVec2 ls = ui::textSize(ui::F_SMALL, names[b].c_str());
          const float room = reach(ang) - R1 - nr - (std::fabs(std::cos(ang)) * ls.x * 0.6f + std::fabs(std::sin(ang)) * ls.y + 6 * z);
          const float step = std::clamp(room / ((float)o.size() + 0.4f), 2.3f * nr, 7.f * nr);
          const ImVec2 sn = at(ang, R1);
          int litN = 0; for (auto& k : o) if (kn(k)) litN++;
          dl->AddLine(at(ang, R0), sn, litN ? sc : ui::withAlpha(ui::ink::muted, 0.6f), (litN ? 2.5f : 1.3f) * z);
          dl->AddNgonFilled(sn, nr * 1.15f, ui::withAlpha(sc, litN ? 0.55f : 0.25f), 6); dl->AddNgon(sn, nr * 1.15f, sc, 6, 1.5f * z);
          ui::drawGlyph(dl, ui::glyphForSchool(skeys[b]), sn, nr * 0.72f, litN ? ui::ink::text : ui::withAlpha(ui::ink::text, 0.55f));   // (the school's mark on its hub)
          {   // the school's name at the tip of its branch, kept on the page
            const ImVec2 ts = ui::textSize(ui::F_SMALL, names[b].c_str());
            const ImVec2 tip = at(ang, R1 + step * ((float)o.size() + 0.55f));
            float lx = tip.x - ts.x * 0.5f + dir.x * (ts.x * 0.5f + nr), ly = tip.y - ts.y * 0.5f + dir.y * (ts.y * 0.5f + 2 * z);
            lx = std::clamp(lx, q.x, q.x + w - ts.x); ly = std::clamp(ly, q.y, q.y + treeH - ts.y);
            ui::text(dl, ui::F_SMALL, ImVec2(lx, ly), ui::withAlpha(sc, 0.95f), names[b].c_str());
          }
          ImVec2 prev = sn; bool prevLit = litN > 0;
          for (int i = 0; i < (int)o.size(); i++) {
            const std::string& k = o[i]; const SpellDef& S = magicData().spells.at(k);
            const bool k1 = kn(k), near = !k1 && lvl + 5 >= S.lvl;
            const float sway = ((i & 1) ? 1 : -1) * std::min(0.07f, 0.9f * nr / (R1 + step * (i + 1))) * (i > 0);
            const ImVec2 pt = at(ang + sway, R1 + step * (i + 1));
            dl->AddLine(prev, pt, k1 && prevLit ? sc : ui::withAlpha(ui::ink::muted, 0.55f), (k1 && prevLit ? 2.5f : 1.2f) * z);
            dl->AddCircleFilled(pt, nr, k1 ? ui::withAlpha(sc, 0.4f) : ui::withAlpha(ui::ink::muted, near ? 0.25f : 0.1f), 20);
            dl->AddCircle(pt, nr, k1 ? sc : ui::withAlpha(ui::ink::muted, near ? 0.9f : 0.4f), 20, (k == active ? 2.6f : 1.2f) * z);
            if (k == active) dl->AddCircle(pt, nr + 3.5f * z, sc, 20, 1.5f * z);
            if (k == left) dl->AddCircle(pt, nr + 3.5f * z, ui::ink::key, 20, 1.2f * z);
            IconRef ic = itemIcons().get(reg().item("tome_" + k) ? "tome_" + k : "book");
            if (ic.valid()) dl->AddImage((ImTextureID)ic.tex, ImVec2(pt.x - nr * 0.72f, pt.y - nr * 0.72f), ImVec2(pt.x + nr * 0.72f, pt.y + nr * 0.72f), ImVec2(ic.uv0.x, ic.uv0.y), ImVec2(ic.uv1.x, ic.uv1.y), k1 ? IM_COL32_WHITE : IM_COL32(255, 255, 255, near ? 110 : 50));
            ImGui::SetCursorScreenPos(ImVec2(pt.x - nr, pt.y - nr));
            ImGui::PushID(("tree" + k).c_str());
            const bool hit = ImGui::InvisibleButton("##n", ImVec2(2 * nr, 2 * nr), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            if (ImGui::IsItemHovered()) ui::setTooltip("%s \xC2\xB7 Magic %d \xC2\xB7 %d mana%s%s", S.name.c_str(), S.lvl, S.mana, S.cast != "instant" ? (" \xC2\xB7 " + S.cast + " cast").c_str() : "", k1 ? (" \xC2\xB7 " + std::string(spellRarityName(effectiveSpellRank(*p, k)))).c_str() : " \xC2\xB7 not learnt");
            if (hit && k1) { if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) setOffSpell(*lv, *p, k); else toggleRightSpell(*lv, *p, k); }
            ImGui::PopID();
            prev = pt; prevLit = k1;
          }
        }
      };
      tr.push_back(std::move(row));
      { ui::BookRow keep; keep.breakBefore = true; keep.h = 1; keep.draw = [](ImDrawList*, ImVec2, float) {}; tr.push_back(std::move(keep)); }   // (its right page: the tree's)
      secs.push_back(std::move(tr));
    }
    for (size_t s = 0; s < lists.size(); s++) {
      ui::BookRows r;
      book.heading(r, (*sch[s])[1], inkOf((*sch[s])[2]));
      book.para(r, "School level " + std::to_string(schoolLv(*p, (*sch[s])[0])) + " \xC2\xB7 Magic " + std::to_string(lvl) + " \xC2\xB7 " +
        std::to_string((int)std::floor(m.mana < 0 ? manaMax(*p) : m.mana)) + " / " + std::to_string((int)manaMax(*p)) + " mana", ui::ink::muted, ui::F_SMALL);
      for (auto& k : lists[s]) {
        const SpellDef& S = md.spells.at(k);
        const bool kn = knows(k), R = k == active, Lh = kn && k == left, Q = kn && k == quick && p->stats.contains("spellQuick");
        const std::string name = kn ? spellTitle(*p, k) : S.name;
        const std::string note = kn ? S.desc : std::string(lvl >= S.lvl ? "Find or craft its tome to learn it. " : "Needs Magic " + std::to_string(S.lvl) + ": cast spells to grow your Magic. ") + S.desc;
        auto lines = ui::Book::wrap(ui::F_SMALL, note, book.pageW() - 26 * z);
        const int me = idx++;
        ui::BookRow row; row.sel = me; row.h = lh + lines.size() * sh + 7 * z;
        row.draw = [this, a = &a, lv, p, k, kn, R, Lh, Q, name, lines, lh, sh, z, me, col = S.col, lvlNeed = S.lvl, mana = S.mana](ImDrawList* dl, ImVec2 q, float w) {
          bool hot = false;
          const int c = rowHit(q, w, lh + lines.size() * sh + 4 * z, &hot);
          if (c == 1) cur = me;
          if (kn && c == 1) toggleRightSpell(*lv, *p, k);
          if (kn && c == 2) { cur = me; setOffSpell(*lv, *p, k); }
          if (kn && c == 3) { cur = me; setQuickSpell(*lv, *p, k); }
          if (me == cur || hot) dl->AddRectFilled(ImVec2(q.x - 4 * z, q.y - 1 * z), ImVec2(q.x + w + 2 * z, q.y + lh + lines.size() * sh + 3 * z), me == cur ? ui::ink::hilite : ui::col::rgb(0xc8a050, 45), 3 * z);
          // which hand holds it: R, L, or both
          const std::string handS = std::string(R ? "R" : "") + (Lh ? "L" : "") + (Q ? "Q" : "");
          const char* hand = handS.c_str();
          if (*hand) { ImVec2 hs = ui::textSize(ui::F_SMALL, hand); dl->AddRectFilled(q, ImVec2(q.x + hs.x + 6 * z, q.y + lh - 2 * z), ui::ink::head, 2 * z); ui::text(dl, ui::F_SMALL, ImVec2(q.x + 3 * z, q.y + (lh - 2 * z - hs.y) * 0.5f), ui::col::rgb(0xf3ead6), hand); }
          const float nx = q.x + 26 * z;
          ui::text(dl, ui::F_BODY, ImVec2(nx, q.y), kn ? inkOf(col) : ui::ink::muted, name.c_str());
          const SpellDef* SD = magicData().spell(k);
          const int er = kn ? effectiveSpellRank(*p, k) : 1;
          std::string cost = (kn ? std::string(spellRarityName(er)) + " \xC2\xB7 " : std::string()) + (SD && SD->cast != "instant" ? (SD->cast == "long" ? "long cast \xC2\xB7 " : "continuous \xC2\xB7 ") : "") + "Magic " + std::to_string(lvlNeed) + " \xC2\xB7 " + std::to_string(mana) + " mana";
          ImVec2 cs = ui::textSize(ui::F_SMALL, cost.c_str());
          ui::text(dl, ui::F_SMALL, ImVec2(q.x + w - cs.x, q.y + (lh - cs.y) * 0.5f), ui::ink::muted, cost.c_str());
          for (size_t i = 0; i < lines.size(); i++) ui::text(dl, ui::F_SMALL, ImVec2(nx, q.y + lh + i * sh), kn ? ui::ink::text : ui::ink::muted, lines[i].c_str());
          if (kn && hot) ui::setTooltip("Rank %d of 10 (%s) \xC2\xB7 %d%% to the next%s", spellRank(*p, k), spellRarityName(spellRank(*p, k)), (int)(spellRankProgress(*p, k) * 100), bookRank(*p, k) > spellRank(*p, k) ? " \xC2\xB7 your spellbook holds it higher" : "");
          (void)a;
        };
        r.push_back(std::move(row));
      }
      secs.push_back(std::move(r));
    }
    book.draw(secs);
    // turning the pages with the arrows: the choice moves onto the open pages
    if (book.turned && !book.shownSel.empty() && std::find(book.shownSel.begin(), book.shownSel.end(), cur) == book.shownSel.end()) cur = book.shownSel.front();
    if (a.input.usingPad()) { ui::padHint(0, -1, "Ready: right hand"); ui::padHint(2, -1, "Ready: left hand"); ui::padHint(3, -1, "Quick spell"); ui::padHint(14, -1, "Turn pages"); }
    book.hint(a.input.usingPad() ? "\xE2\x86\x91 \xE2\x86\x93 choose \xC2\xB7 A: ready / empty the right hand (RT) \xC2\xB7 X: the left (LT) \xC2\xB7 Y: quick spell (hold B, press RB) \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 pages \xC2\xB7 B: close"
                                 : "Click: right hand (left click casts) \xC2\xB7 right-click: left hand (right click casts) \xC2\xB7 middle-click: quick spell (Z) \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 or the wheel: pages \xC2\xB7 Esc: close");
    book.end();
  }
  std::string lv0;
  ui::Book book;
  std::vector<std::string> flat;
  int cur = -1;
};

// ---- skills and perks (skills.js + rpg.js) ------------------------------------------------------------------------
// each skill's picture: the tool or thing it's about (an item of the game's own, drawn as its icon)
static std::string skillIcon(const std::string& k) {
  static const std::pair<const char*, const char*> M[] = {
    {"mining", "iron_pickaxe"}, {"woodcutting", "iron_axe"}, {"excavation", "iron_shovel"}, {"farming", "wheat"}, {"fishing", "fishing_rod"},
    {"melee", "iron_sword"}, {"archery", "bow"}, {"defence", "shield"}, {"smithing", "anvil"}, {"cooking", "cooked_beef"}, {"taming", "lead"},
    {"magic", "enchanted_book"}, {"exploring", "compass"}, {"alchemy", "brewing_stand"},
  };
  for (auto& [s, i] : M) if (k == s && reg().item(i)) return i;
  return reg().item("book") ? "book" : std::string();
}
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
      row.draw = [name = s.name, col = s.col, icon = skillIcon(s.key), L, pr, tip, lh, z](ImDrawList* dl, ImVec2 q0, float w0) {
        bool hot = false; rowHit(q0, w0, lh + 8 * z, &hot);
        const float is = lh + 6 * z;   // the skill's picture on the left, its name and bar beside it
        if (!icon.empty()) ui::drawIcon(dl, icon, ImVec2(q0.x, q0.y), is, L > 1 ? 1.f : 0.7f);
        const ImVec2 q(q0.x + is + 6 * z, q0.y); const float w = w0 - is - 6 * z;
        ui::text(dl, ui::F_BODY, q, inkOf(col), name.c_str());
        std::string lv = std::to_string(L); ImVec2 ls = ui::textSize(ui::F_BODY, lv.c_str());
        ui::text(dl, ui::F_BODY, ImVec2(q.x + w - ls.x, q.y), ui::ink::text, lv.c_str());
        ImVec2 b0(q.x, q.y + lh + 1 * z), b1(q.x + w, q.y + lh + 6 * z);
        dl->AddRectFilled(b0, b1, ui::ink::faint);
        dl->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * std::clamp(pr, 0.f, 1.f), b1.y), inkOf(col));
        dl->AddRect(b0, b1, ui::withAlpha(ui::ink::text, 0.5f), 0, 0, std::max(1.f, z));
        if (hot) ui::setTooltip("%s", tip.c_str());
      };
      sk.push_back(std::move(row));
    }
    // perks: the ones you have, then the nearest few to come
    struct P { std::string k, name, col, pn, desc; int lv; };   // (k: the skill)
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
      row.draw = [o, got, icon = skillIcon(o.k), lh](ImDrawList* dl, ImVec2 q0, float) {
        const float is = lh - 2 * ui::Z();
        if (!icon.empty()) ui::drawIcon(dl, icon, ImVec2(q0.x, q0.y), is, got ? 1.f : 0.45f);
        const ImVec2 q(q0.x + is + 5 * ui::Z(), q0.y);
        ui::text(dl, ui::F_BODY, q, inkOf(o.col, got ? 1.f : 0.6f), o.pn.c_str());
        std::string t = "  " + o.name + " " + std::to_string(o.lv);
        ui::text(dl, ui::F_SMALL, ImVec2(q.x + ui::textSize(ui::F_BODY, o.pn.c_str()).x, q.y + 2 * ui::Z()), ui::ink::muted, t.c_str());
      };
      pk.push_back(std::move(row));
      book.para(pk, o.desc, got ? ui::ink::text : ui::ink::muted, ui::F_SMALL, lh + 3 * z);
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
// each chapter's picture: a thing of the game's own that the chapter is about
static std::string chapterIcon(const std::string& id) {
  static const std::pair<const char*, const char*> M[] = {
    {"village", "bell"}, {"iron", "iron_pickaxe"}, {"tear", "rift_crystal"}, {"keepers", "realm_fragment"}, {"depths", "cinder_core"},
    {"skylands", "feather"}, {"gateway", "rift_core"}, {"realm", "rift_staff"}, {"storm", "wither_skeleton_skull"}, {"seal", "nether_star"},
    {"end", "ender_eye"}, {"deepdark", "echo_shard"}, {"dragons", "dragon_scale"},
  };
  for (auto& [c, i] : M) if (id == c && reg().item(i)) return i;
  return reg().item("writable_book") ? "writable_book" : std::string();
}
// a leaf on the journal's tree, pointing along `a` (radians)
static void journalLeaf(ImDrawList* dl, ImVec2 p, float a, float len, ImU32 c) {
  const ImVec2 d(std::cos(a), std::sin(a)), n(-d.y, d.x);
  const ImVec2 tip(p.x + d.x * len, p.y + d.y * len), m(p.x + d.x * len * 0.5f, p.y + d.y * len * 0.5f);
  dl->AddTriangleFilled(p, ImVec2(m.x + n.x * len * 0.32f, m.y + n.y * len * 0.32f), tip, c);
  dl->AddTriangleFilled(p, tip, ImVec2(m.x - n.x * len * 0.32f, m.y - n.y * len * 0.32f), c);
}
class JournalScreen : public MagicScreenBase {
public:
  const char* id() const override { return "journal"; }
  // ---- the story as a tree: the trunk the main story (from the root at the bottom up), branches off either side; only
  // what you've come to shows, the next on the trunk a faint bud. The chosen chapter on the right page: what came of it,
  // or the journal's clue, and "Read closer" for the plain way
  void storyRows(App& a, Story& s, ui::BookRows& st, float z) {
    book.heading(st, playerName + "'s journal");
    if (sel.empty() || std::none_of(s.ch.begin(), s.ch.end(), [&](auto& c) { return c.id == sel && c.shown; })) sel = s.cur >= 0 ? s.ch[s.cur].id : s.ch[0].id;
    // the layout: trunk chapters in order up the middle; a branch beside its parent, a little higher
    std::vector<int> trunk; for (int i = 0; i < (int)s.ch.size(); i++) if (s.ch[i].branch == 0) trunk.push_back(i);
    const float H = std::max(200 * z, book.pageH() - 60 * z);
    ui::BookRow row; row.h = H;
    row.draw = [this, &a, s, trunk, z, H](ImDrawList* dl, ImVec2 q, float w) {
      std::map<std::string, ImVec2> at;
      const int n = (int)trunk.size();
      const float step = (H - 40 * z) / std::max(1, n - 1), cx = q.x + w * 0.5f, by = q.y + H - 20 * z;
      for (int k = 0; k < n; k++) at[s.ch[trunk[k]].id] = ImVec2(cx, by - k * step);
      for (auto& c : s.ch) if (c.branch != 0 && at.count(c.parent)) { ImVec2 pp = at[c.parent]; at[c.id] = ImVec2(cx + c.branch * w * 0.32f, pp.y - step * 0.45f); }
      const ImU32 bark = ui::withAlpha(ui::ink::text, 0.75f), faint = ui::ink::faint;
      // the trunk: drawn up to the highest shown chapter, a dotted shoot on to the next (the bud)
      int top = -1; for (int k = 0; k < n; k++) if (s.ch[trunk[k]].shown) top = k;
      if (top > 0) dl->AddLine(at[s.ch[trunk[0]].id], at[s.ch[trunk[top]].id], bark, 5 * z);
      if (top + 1 < n) { ImVec2 a0 = at[s.ch[trunk[top]].id], a1 = at[s.ch[trunk[top + 1]].id];
        for (int d = 0; d < 6; d++) { float t0 = d / 6.f, t1 = t0 + 0.08f; dl->AddLine(ImVec2(a0.x, a0.y + (a1.y - a0.y) * t0), ImVec2(a0.x, a0.y + (a1.y - a0.y) * t1), faint, 3 * z); }
        ui::text(dl, ui::F_BODY, ImVec2(a1.x - 4 * z, a1.y - 8 * z), ui::ink::muted, "?"); }
      // the roots under it all
      for (int r = -2; r <= 2; r++) dl->AddBezierCubic(ImVec2(cx, by), ImVec2(cx + r * 6 * z, by + 8 * z), ImVec2(cx + r * 14 * z, by + 10 * z), ImVec2(cx + r * 22 * z, by + 16 * z), bark, 2 * z);
      // branches
      const ImU32 leafOn = ui::withAlpha(ui::ink::good, 0.8f), leafOff = ui::withAlpha(ui::ink::good, 0.3f);
      for (auto& c : s.ch) if (c.branch != 0 && c.shown && at.count(c.parent)) {
        ImVec2 p0 = at[c.parent], p1 = at[c.id];
        const ImVec2 k1(p0.x + (p1.x - p0.x) * 0.4f, p0.y), k2(p1.x - (p1.x - p0.x) * 0.3f, p1.y + 6 * z);
        dl->AddBezierCubic(p0, k1, k2, p1, bark, 3 * z);
        for (int l = 1; l <= 2; l++) {   // a pair of leaves along it, green once its chapter's done
          const float t = l / 3.f, u = 1 - t;
          const ImVec2 b(u * u * u * p0.x + 3 * u * u * t * k1.x + 3 * u * t * t * k2.x + t * t * t * p1.x, u * u * u * p0.y + 3 * u * u * t * k1.y + 3 * u * t * t * k2.y + t * t * t * p1.y);
          journalLeaf(dl, b, (l & 1 ? -1.9f : -1.2f) + (c.branch < 0 ? -0.4f : 0.4f), 9 * z, c.done ? leafOn : leafOff);
        }
      }
      for (int k = 0; k < top; k++) {   // and a leaf each side of the trunk between chapters, for the ones done
        const ImVec2 a0 = at[s.ch[trunk[k]].id], a1 = at[s.ch[trunk[k + 1]].id], m((a0.x + a1.x) * 0.5f, (a0.y + a1.y) * 0.5f);
        const ImU32 lc = s.ch[trunk[k]].done ? leafOn : leafOff;
        journalLeaf(dl, ImVec2(m.x + 2 * z, m.y), -0.6f, 10 * z, lc); journalLeaf(dl, ImVec2(m.x - 2 * z, m.y + 5 * z), -2.5f, 10 * z, lc);
      }
      // the chapters: leaves (done: green), the one you're on (gold, a ring), the rest found (an open bud)
      const ImVec2 mp = ImGui::GetIO().MousePos;
      for (int i = 0; i < (int)s.ch.size(); i++) {
        const auto& c = s.ch[i];
        if (!c.shown || !at.count(c.id)) continue;
        const ImVec2 p = at[c.id];
        const float r = 12 * z;
        const bool cur = i == s.cur, chosen = c.id == sel;
        const bool hot = std::hypot(mp.x - p.x, mp.y - p.y) < r * 1.5f;
        if (hot && ImGui::IsMouseClicked(0)) { sel = c.id; closer = false; }
        // a medallion with the chapter's picture: green-rimmed when done (a tick on it), gold when it's the one you're on
        dl->AddCircleFilled(p, r, ui::withAlpha(c.done ? ui::ink::good : cur ? ui::ink::key : ui::ink::faint, c.done || cur ? 0.28f : 0.35f), 20);
        dl->AddCircle(p, r, c.done ? ui::ink::good : cur ? ui::ink::key : ui::ink::text, 20, (c.done || cur ? 2.5f : 1.5f) * z);
        if (const std::string ic = chapterIcon(c.id); !ic.empty()) ui::drawIcon(dl, ic, ImVec2(p.x - r * 0.72f, p.y - r * 0.72f), r * 1.44f, c.done || cur ? 1.f : 0.75f);
        if (c.done) { const ImVec2 bc(p.x + r * 0.72f, p.y + r * 0.72f); const float br = r * 0.42f;
          dl->AddCircleFilled(bc, br, ui::ink::good, 14); ui::drawTick(dl, ImVec2(bc.x - br * 0.62f, bc.y - br * 0.62f), br * 1.24f, ui::col::rgb(0xf4ecd8)); }
        if (chosen || hot) dl->AddCircle(p, r + 4 * z, ui::ink::head, 20, 2 * z);
        // its name beside it (the trunk's to the left of the trunk, a branch's out past its leaf)
        const std::string t = c.t;
        const ImVec2 ts = ui::textSize(ui::F_SMALL, t.c_str());
        const float tx = c.branch < 0 ? p.x - r - 4 * z - ts.x : c.branch > 0 ? p.x + r + 4 * z : p.x - r - 6 * z - ts.x;
        float tx2 = std::max(q.x, std::min(tx, q.x + w - ts.x));
        ui::text(dl, ui::F_SMALL, ImVec2(tx2, p.y - ts.y * 0.5f), c.done ? ui::ink::good : cur ? ui::ink::head : ui::ink::text, t.c_str());
      }
    };
    st.push_back(std::move(row));
    // the right page: the chosen chapter
    const StoryChapter* c = nullptr; for (auto& x : s.ch) if (x.id == sel) c = &x;
    if (!c) return;
    ui::BookRow brk; brk.breakBefore = true; brk.h = 1; brk.draw = [](ImDrawList*, ImVec2, float) {}; st.push_back(brk);
    book.iconRow(st, chapterIcon(c->id), c->t, c->done ? "Done" : "Under way", c->done ? ui::ink::good : ui::ink::head);
    if (c->done) {
      book.para(st, c->note, ui::ink::text);
      book.gap(st, 4 * z);
      book.para(st, c->goal, ui::ink::muted, ui::F_SMALL);
    } else {
      book.para(st, "\"" + c->clue + "\"", ui::ink::key);
      if (c->prog >= 0) { book.gap(st, 4 * z); book.bar(st, c->prog, ui::ink::key); }
      book.gap(st, 8 * z);
      if (!closer) {
        ui::BookRow btn; btn.h = ui::textSize(ui::F_BODY, "Ag").y + 10 * z;
        btn.draw = [this, z](ImDrawList* dl, ImVec2 p, float) {
          const char* t = "Read closer...";
          const ImVec2 ts = ui::textSize(ui::F_BODY, t), b0 = p, b1(p.x + ts.x + 16 * z, p.y + ts.y + 8 * z);
          const ImVec2 mp = ImGui::GetIO().MousePos; const bool hot = mp.x >= b0.x && mp.x <= b1.x && mp.y >= b0.y && mp.y <= b1.y;
          dl->AddRect(b0, b1, hot ? ui::ink::head : ui::ink::muted, 3 * z, 0, 1.5f * z);
          ui::text(dl, ui::F_BODY, ImVec2(p.x + 8 * z, p.y + 4 * z), hot ? ui::ink::head : ui::ink::text, t);
          if (hot && ImGui::IsMouseClicked(0)) closer = true;
        };
        st.push_back(std::move(btn));
        book.para(st, "(the plain way, if you're stuck: A on a controller)", ui::ink::muted, ui::F_SMALL);
      } else {
        book.para(st, c->goal, ui::ink::text);
        book.gap(st, 3 * z);
        book.para(st, c->hint, ui::ink::muted, ui::F_SMALL);
      }
    }
    // the others found but not done, so a side road isn't forgotten
    int open = 0; for (auto& x : s.ch) if (x.shown && !x.done && x.id != sel) open++;
    if (open) { book.gap(st, 10 * z); book.para(st, std::to_string(open) + (open == 1 ? " other road still open on the tree." : " other roads still open on the tree."), ui::ink::muted, ui::F_SMALL); }
  }
  // ---- the Compendium: an index, then a spread to each creature met (its picture and what it is on the left; what it
  // does and drops on the right). What it does shows once you've beaten one, what it drops once you've beaten three
  void dexRows(App& a, Player& p, ui::BookRows& idx, float z) {
    std::vector<std::pair<std::string, std::string>> met;   // name, type
    const json& D = p.stats.contains("dex") && p.stats["dex"].is_object() ? p.stats["dex"] : json::object();
    int total = 0;
    for (auto& [k, m] : reg().mobs.items()) {
      if (!m.is_object() || m.value("npc", false) || !creatureType(k)) continue;
      total++;
      if (D.contains(k)) met.push_back({nameOfMob(k), k});
    }
    std::sort(met.begin(), met.end());
    book.heading(idx, "Compendium");
    book.para(idx, std::to_string(met.size()) + " of " + std::to_string(total) + " creatures met.", ui::ink::muted, ui::F_SMALL);
    book.gap(idx, 4 * z);
    if (met.empty()) book.para(idx, "Nothing yet. Get a good look at a creature and it goes in the book.", ui::ink::muted);
    for (size_t i = 0; i < met.size(); i++) {
      const std::string type = met[i].second, name = met[i].first;
      const int kills = D[type].value("k", 0);
      ui::BookRow row; row.h = ui::textSize(ui::F_BODY, "Ag").y + 3 * z; row.sel = 1000 + (int)i;
      row.draw = [this, type, name, kills, i, z](ImDrawList* dl, ImVec2 q, float w) {
        const ImVec2 mp = ImGui::GetIO().MousePos; const float h = ui::textSize(ui::F_BODY, "Ag").y + 3 * z;
        const bool hot = mp.x >= q.x && mp.x <= q.x + w && mp.y >= q.y && mp.y < q.y + h;
        if (hot || dexCur == (int)i) dl->AddRectFilled(ImVec2(q.x - 3 * z, q.y), ImVec2(q.x + w, q.y + h), ui::ink::hilite, 3 * z);
        static std::map<std::string, std::string> eggs;   // (its spawn egg's picture, if it has one)
        auto e = eggs.find(type);
        if (e == eggs.end()) e = eggs.emplace(type, reg().item(type + "_spawn_egg") ? type + "_spawn_egg" : std::string()).first;
        if (!e->second.empty()) ui::drawIcon(dl, e->second, ImVec2(q.x, q.y), h - 2 * z, kills ? 1.f : 0.6f);
        else ui::drawGlyph(dl, kills ? ui::Glyph::Skull : ui::Glyph::Eye, ImVec2(q.x + (h - 2 * z) * 0.5f, q.y + h * 0.5f - z), (h - 2 * z) * 0.4f, ui::ink::muted);
        q.x += h + 2 * z; w -= h + 2 * z;
        ui::text(dl, ui::F_BODY, q, ui::ink::text, name.c_str());
        const std::string kk = kills ? std::to_string(kills) + " beaten" : "seen";
        ui::text(dl, ui::F_SMALL, ImVec2(q.x + w - ui::textSize(ui::F_SMALL, kk.c_str()).x, q.y + 2 * z), ui::ink::muted, kk.c_str());
        if (hot && ImGui::IsMouseClicked(0)) dexGo = (int)i;
      };
      idx.push_back(std::move(row));
    }
    dexTypes.clear();
    ui::BookRows& R = idx;   // (each creature a page of its own after the index, two to a spread)
    for (auto& [name, type] : met) {
      dexTypes.push_back(type);
      const CreatureType* T = creatureType(type);
      const json& M = reg().mobs[type];
      const int kills = D[type].value("k", 0);
      book.heading(R, name, ui::ink::head, true);
      R.back().sel = 2000 + (int)dexTypes.size() - 1;
      // the picture: the creature itself, turning
      ui::BookRow pic; pic.h = 120 * z;
      pic.draw = [this, type, z](ImDrawList* dl, ImVec2 q, float w) {
        const int px = (int)(std::min(w, 120 * z) * 1.5f);
        CreaturePortrait& pp = portraits[type];   // (each its own picture: two pages open at once)
        const GLuint tex = pp.render(type, px, px, (float)ImGui::GetTime() * 0.6f, (float)ImGui::GetTime());
        const float s = std::min(w, 120 * z);
        if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(q.x + (w - s) * 0.5f, q.y), ImVec2(q.x + (w + s) * 0.5f, q.y + s), ImVec2(0, 1), ImVec2(1, 0));
      };
      R.push_back(std::move(pic));
      const std::string blurb = dexBlurb(type);
      if (!blurb.empty()) book.para(R, blurb, ui::ink::text, ui::F_SMALL);
      const auto homes = creatureHomes(type);
      std::string where;
      for (size_t i = 0; i < homes.size(); i++) where += (i ? ", " : "") + homes[i];
      if (where.empty()) where = T && T->hostile ? (M.value("boss", false) ? "its own lair" : "the dark, at night or underground") : "about the land";
      book.gap(R, 3 * z);
      book.para(R, "Found in: " + where, ui::ink::muted, ui::F_SMALL);
      book.gap(R, 4 * z);
      book.sub(R, kills ? "What it is" : "What it is (beat one to learn more)");
      if (kills >= 1) {
        recordRow(book, R, "Health", std::to_string((int)M.value("hp", 0.0)));
        if (M.contains("damage")) recordRow(book, R, "Its blows", strFormat("%.1f", M.value("damage", 0.0)));
        recordRow(book, R, "Speed", M.value("speed", 0.0) < 1.5 ? "slow" : M.value("speed", 0.0) < 2.6 ? "steady" : "fast");
        recordRow(book, R, "Temper", M.value("boss", false) ? "a great foe" : M.value("hostile", false) ? "hostile" : M.value("retaliates", false) || M.value("angersWhenHurt", false) ? "fights back" : M.value("skittish", 0.0) > 0 ? "shy" : "peaceful");
        book.gap(R, 4 * z);
        book.sub(R, kills >= 3 ? "How it fights" : "How it fights (beat three)");
        if (kills >= 3) for (auto& l : dexHabits(M)) book.bullet(R, l);
        book.gap(R, 4 * z);
        book.sub(R, kills >= 3 ? "What it leaves" : "What it leaves (beat three)");
        if (kills >= 3) {
          if (!M.contains("drops") || M["drops"].empty()) book.para(R, "Nothing.", ui::ink::muted);
          else for (auto& d : M["drops"]) if (d.is_array() && !d.empty() && d[0].is_string()) {
            const std::string k = d[0].get<std::string>(); const ItemDef* id = reg().item(k);
            std::string t = id ? id->name : k;
            if (d.size() >= 4 && d[3].is_number() && d[3].get<double>() < 1) t += " (now and then)";
            book.bullet(R, t);
          }
        }
      } else book.para(R, "Its strength and its ways are a mystery yet.", ui::ink::muted);
      book.gap(R, 6 * z);
      book.para(R, kills ? std::to_string(kills) + " beaten" : "Met, not yet beaten", ui::ink::muted, ui::F_SMALL);
    }
  }
  static std::string nameOfMob(const std::string& k) {
    const json& M = reg().mobs[k];
    std::string n = M.value("name", std::string());
    if (n.empty()) { n = M.value("label", k); for (const char* a : {"a ", "an ", "the "}) if (n.rfind(a, 0) == 0) n = n.substr(strlen(a)); }
    for (char& c : n) if (c == '_') c = ' ';
    if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
    return n;
  }
  static std::vector<std::string> dexHabits(const json& M) {
    std::vector<std::string> o;
    auto f = [&](const char* k) { return M.contains(k) && !(M[k].is_boolean() && !M[k].get<bool>()) && !(M[k].is_number() && M[k].get<double>() == 0); };
    if (f("ranged")) o.push_back("Strikes from afar: close in, or keep cover between you.");
    if (f("onHit")) o.push_back("Its blows carry something nasty with them.");
    if (f("stings")) o.push_back("It stings.");
    if (f("pack")) o.push_back("Hunts in packs: where there's one, look for more.");
    if (f("flyer") || f("fly")) o.push_back("Flies: it comes at you from above.");
    if (f("swims")) o.push_back("At home in the water.");
    if (f("climber")) o.push_back("Climbs walls.");
    if (f("undead")) o.push_back("Undead: Smite bites deep, and healing hurts it.");
    if (f("fleesSun")) o.push_back("Burns in the sun.");
    if (f("fireImmune")) o.push_back("Fire doesn't touch it.");
    if (M.contains("element") && M["element"].is_string()) o.push_back("Its element: " + M["element"].get<std::string>() + ".");
    if (f("knockResist")) o.push_back("Hard to knock back.");
    if (f("retaliates") || f("angersWhenHurt")) o.push_back("Leave it be and it leaves you be. Hit it and it fights.");
    if (f("skittish")) o.push_back("Flees when you come near.");
    if (f("boss")) o.push_back("A great foe: come ready.");
    if (o.empty()) o.push_back(M.value("hostile", false) ? "Comes straight at you." : "It means you no harm.");
    return o;
  }
  static std::string dexBlurb(const std::string& type) {
    static json B = [] { auto j = readJson(dataDir() / "compendium.json"); return j ? *j : json::object(); }();
    return B.is_object() && B.contains(type) && B[type].is_string() ? B[type].get<std::string>() : std::string();
  }

  void draw(App& a) override {
    Player* p = player(a); Level* lv = level(a);
    if (!p || !lv) return;
    closeOn(a, Act::Journal);
    if (a.input.pressed(Act::Skills) && frames > 1) { want = true; swapTo = 1; }
    Story s = storyOf(*lv, *p);
    playerName = p->name;
    book.begin(a, "##journal", {{"Your story", "book"}, {"What next", "compass"}, {"Compendium", "spawn_egg"}, {"Records", "clock"}});
    const float z = ui::Z();
    // the controller: on the story, up / down walks the chapters found and A reads closer; on What next up / down picks a
    // goal and A follows it (points to spend: your skills); in the Compendium's index up / down walks it and A opens that
    // creature's spread
    Input& in = a.input;
    if (book.tab() == 0) {
      std::vector<std::string> ids; for (auto& c : s.ch) if (c.shown) ids.push_back(c.id);
      int k = 0; for (int i = 0; i < (int)ids.size(); i++) if (ids[i] == sel) k = i;
      if (!ids.empty() && in.pressed(Act::MenuDown)) { sel = ids[(k + 1) % ids.size()]; closer = false; }
      if (!ids.empty() && in.pressed(Act::MenuUp)) { sel = ids[(k + ids.size() - 1) % ids.size()]; closer = false; }
      if (in.pressed(Act::MenuOk)) closer = !closer;
    }
    ui::BookRows st, rec;
    storyRows(a, s, st, z);
    std::vector<ui::BookRows> secs; secs.push_back(std::move(st));
    ui::BookRows nx; nextRows(a, *lv, *p, nx, z);
    secs.push_back(std::move(nx));
    if (book.tab() == 1 && !goals.empty()) {
      if (in.pressed(Act::MenuDown)) nextCur = (nextCur + 1) % (int)goals.size();
      if (in.pressed(Act::MenuUp)) nextCur = (nextCur + (int)goals.size() - 1) % (int)goals.size();
      if (in.pressed(Act::MenuOk) && nextCur >= 0) nextGo = nextCur;
    }
    if (nextGo >= 0 && nextGo < (int)goals.size()) {   // following a goal: the points' and perks' ones open your skills
      const std::string k = goals[nextGo].key; nextGo = -1;
      if (k == "stat" || k == "talent" || k == "perk") { want = true; swapTo = 1; }
    }
    if (portraits.size() > 8) portraits.clear();   // (the pictures of the pages open: a few at most)
    ui::BookRows dex; dexRows(a, *p, dex, z);
    secs.push_back(std::move(dex));
    if (book.tab() == 2 && !dexTypes.empty()) {
      if (in.pressed(Act::MenuDown)) dexCur = (dexCur + 1) % (int)dexTypes.size();
      if (in.pressed(Act::MenuUp)) dexCur = (dexCur + (int)dexTypes.size() - 1) % (int)dexTypes.size();
      if (in.pressed(Act::MenuOk) && dexCur >= 0) dexGo = dexCur;
    }
    book.heading(rec, "Records");
    for (auto& [k, v] : journalRecords(*lv, *p)) recordRow(book, rec, k, v);
    auto foes = greatFoesBeaten(*lv);
    book.gap(rec, 6 * z); book.sub(rec, "Great foes beaten");
    if (foes.empty()) book.para(rec, "None yet.", ui::ink::muted);
    for (auto& f : foes) book.bullet(rec, f);
    secs.push_back(std::move(rec));
    // (the bookmarks: the Compendium's index and each creature's spread are sections; the ribbon goes to the index)
    book.draw(secs);
    if (dexGo >= 0) { book.showSel(2000 + dexGo); dexCur = dexGo; dexGo = -1; }
    if (a.input.usingPad()) ui::padHint(14, -1, "Turn pages");
    book.hint(a.input.usingPad() ? "D-pad \xE2\x86\x90 \xE2\x86\x92: turn the pages \xC2\xB7 \xE2\x86\x91 \xE2\x86\x93: choose \xC2\xB7 A: read closer, follow a goal, open a creature \xC2\xB7 X: next bookmark \xC2\xB7 B: close"
                                 : "J: close \xC2\xB7 K: your skills and perks \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 or the wheel: turn the pages \xC2\xB7 click a leaf on the tree, or a goal to follow it");
    book.end();
  }
  // ---- What next: a few goals from where you are (sim/guidance.h), each with why; one with points to spend opens your
  // skills when followed
  void nextRows(App& a, Level& lv, Player& p, ui::BookRows& R, float z) {
    goals = whatNext(lv, p);
    if (nextCur >= (int)goals.size()) nextCur = goals.empty() ? -1 : 0;
    book.heading(R, "What next");
    book.para(R, "A few things you could do now, the most pressing first.", ui::ink::muted, ui::F_SMALL);
    book.gap(R, 6 * z);
    for (size_t i = 0; i < goals.size(); i++) {
      const NextGoal g = goals[i];
      const bool opens = g.key == "stat" || g.key == "talent" || g.key == "perk";
      const float h = std::max(ui::textSize(ui::F_BODY, "Ag").y + 6 * z, 26 * z);
      ui::BookRow row; row.h = h; row.sel = 3000 + (int)i;
      row.draw = [this, g, i, h, z, opens](ImDrawList* dl, ImVec2 q, float w) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool hot = mp.x >= q.x && mp.x <= q.x + w && mp.y >= q.y && mp.y < q.y + h;
        if (hot || nextCur == (int)i) dl->AddRectFilled(ImVec2(q.x - 3 * z, q.y), ImVec2(q.x + w, q.y + h), ui::ink::hilite, 3 * z);
        ui::drawIcon(dl, g.icon, ImVec2(q.x, q.y + 1 * z), h - 2 * z, 1.f);
        ui::text(dl, ui::F_BODY, ImVec2(q.x + h + 4 * z, q.y + (h - ui::textSize(ui::F_BODY, "Ag").y) * 0.5f), ui::ink::head, g.title.c_str(), w - h - 6 * z);
        if (opens) { const char* t = "\xE2\x86\x92"; ui::text(dl, ui::F_BODY, ImVec2(q.x + w - ui::textSize(ui::F_BODY, t).x - 2 * z, q.y + 3 * z), ui::ink::muted, t); }
        if (hot && ImGui::IsMouseClicked(0)) { nextCur = (int)i; nextGo = (int)i; }
      };
      R.push_back(std::move(row));
      book.para(R, g.why, ui::ink::text, ui::F_SMALL, 8 * z);
      book.gap(R, 7 * z);
    }
    (void)a;
  }
  std::vector<NextGoal> goals;
  int nextCur = 0, nextGo = -1;
  void update(App& a, double dt) override {
    if (want && swapTo == 1) { want = false; swapTo = 0; a.popScreen(); a.pushScreen(makeSkillsScreen()); return; }
    MagicScreenBase::update(a, dt);
  }
  int swapTo = 0;
  ui::Book book;
  std::string sel, playerName; bool closer = false;
  int dexCur = -1, dexGo = -1;
  std::vector<std::string> dexTypes;
  std::map<std::string, CreaturePortrait> portraits;
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

// ---- the inscription table (the spell magic, ours: docs/MAGIC_MERGE.md) ----------------------------------------------
class InscriptionScreen : public MagicScreenBase {
public:
  explicit InscriptionScreen(ivec3 at) : pos(at) {}
  const char* id() const override { return "inscribe"; }
  ivec3 pos;
  ItemStack book, scroll, paper, lapis;
  std::string status;
  void giveBack(App& a) {
    Player* p = player(a); Level* lv = level(a);
    if (!p || !lv) return;
    for (ItemStack* s : {&book, &scroll, &paper, &lapis}) if (!s->empty()) { giveOrDrop(*lv, *p, *s); s->clear(); }
  }
  void onClose(App& a) override { giveBack(a); }
  void draw(App& a) override {
    Player* p = player(a); Level* lv = level(a);
    if (!p || !lv) return;
    if (reg().block(lv->world(p->realm).getBlock(pos.x, pos.y, pos.z)).key != "inscription_table") { want = true; return; }
    const float sz = ui::bookPagesBegin(a, "##inscribe");
    ui::bookPageTitle(a, "Inscription Table");
    auto back = [&](ItemStack& s) { if (!s.empty()) { giveOrDrop(*lv, *p, s); s.clear(); } };
    ImGui::BeginGroup();
    if (slotButton("book", book, sz, "apprentice_spellbook")) back(book);
    ImGui::SameLine(0, 6); if (slotButton("scroll", scroll, sz, "spell_scroll")) back(scroll);
    ImGui::SameLine(0, 14); if (slotButton("paper", paper, sz, "paper")) back(paper);
    ImGui::SameLine(0, 6); if (slotButton("lapis", lapis, sz, "lapis_lazuli")) back(lapis);
    ImGui::EndGroup();
    // the book's slots
    const int slots = bookSlots(book.id);
    if (slots) {
      const auto sp = bookSpells(book);
      ImGui::TextDisabled("%s: %d of %d slots written", reg().item(book.id) ? reg().item(book.id)->name.c_str() : "Spellbook", (int)sp.size(), slots);
      for (int i = 0; i < slots; i++) {
        ImGui::PushID(1000 + i);
        if (i < (int)sp.size()) {
          const u32 c = spellRarityColor(sp[i].second);
          ImGui::TextColored(ImVec4(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1), "%d. %s", i + 1, spellLabel(sp[i].first, sp[i].second).c_str());
          ImGui::SameLine();
          if (ImGui::SmallButton("Take out")) { ItemStack out; status = extractSpell(book, i, out); if (status.empty()) { giveOrDrop(*lv, *p, out); status = "Taken out as a scroll"; lv->fx->soundUi(p->id, "book_page"); } }
        } else ImGui::TextDisabled("%d. (empty)", i + 1);
        ImGui::PopID();
      }
      const bool can = isScroll(scroll);
      if (!can) ImGui::BeginDisabled();
      if (ImGui::Button("Inscribe the scroll")) {
        status = inscribeScroll(book, scroll);
        if (status.empty()) { status = "Inscribed"; lv->fx->soundUi(p->id, "levelup"); }
      }
      if (!can) ImGui::EndDisabled();
    } else ImGui::TextDisabled("Put in a spellbook to inscribe scrolls into it");
    // write a scroll of a spell you know
    ImGui::Dummy(ImVec2(1, 6));
    ImGui::TextDisabled("Write a scroll (a paper and a lapis): a spell you know, at your rank");
    const auto& ks = knownSpells(*p);
    int shown = 0;
    for (const std::string& k : ks) {
      if (!learntSpell(*p, k)) continue;   // (one only carried in a book can't be written out)
      ImGui::PushID(k.c_str());
      const int r = spellRank(*p, k); const u32 c = spellRarityColor(r);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1));
      const bool go = ImGui::SmallButton(spellLabel(k, r).c_str());
      ImGui::PopStyleColor();
      if (go) { ItemStack out; status = writeScroll(*p, k, paper, lapis, out); if (status.empty()) { giveOrDrop(*lv, *p, out); status = "Written"; lv->fx->soundUi(p->id, "book_page"); } }
      if (++shown % 2) ImGui::SameLine();
      ImGui::PopID();
    }
    if (ks.empty()) ImGui::TextDisabled("You know no spells yet: read a spell tome");
    ImGui::NewLine();
    if (!status.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", status.c_str()); ImGui::PopTextWrapPos(); }
    ui::bookPagesRight("##inscribe-bag");
    bagGrid(*p, sz, [&](int i) {
      ItemStack& s = p->inv[i];
      if (s.empty()) return;
      auto take = [&](ItemStack& slot, int most) {
        if (slot.empty()) { slot = s; slot.n = std::min(s.n, most); s.n -= slot.n; if (s.n <= 0) s.clear(); return; }
        if (slot.id != s.id || slot.tag != s.tag) return;
        const int mv = std::min(most - slot.n, s.n); slot.n += mv; s.n -= mv; if (s.n <= 0) s.clear();
      };
      if (bookSlots(s.id) && book.empty()) take(book, 1);
      else if (isScroll(s) && scroll.empty()) take(scroll, 1);
      else if (s.id == "paper") take(paper, 64);
      else if (s.id == "lapis_lazuli") take(lapis, 64);
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
std::unique_ptr<Screen> makeInscriptionScreen(ivec3 at) { return std::make_unique<InscriptionScreen>(at); }
std::unique_ptr<Screen> makeBrewScreen(ivec3 at) { return std::make_unique<BrewScreen>(at); }

} // namespace dq
