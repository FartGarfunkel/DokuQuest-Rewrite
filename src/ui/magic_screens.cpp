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
static bool panel(App& a, const char* id, float wFrac, float hFrac) {
  ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(a.width * wFrac, a.height * hFrac), ImGuiCond_Always);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(18 / 255.f, 10 / 255.f, 30 / 255.f, 0.94f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.54f, 0.42f, 0.82f, 1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2);
  bool open = ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
  return open;
}
static void endPanel() { ImGui::End(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(2); }
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
  if (!s.empty() && s.n > 1) { std::string n = std::to_string(s.n); ImVec2 ts = ImGui::CalcTextSize(n.c_str()); dl->AddText(ImVec2(p.x + size - ts.x - 3, p.y + size - ts.y - 1), IM_COL32_WHITE, n.c_str()); }
  if (!s.empty() && s.tag.is_object() && s.tag.contains("en")) dl->AddRect(p, ImVec2(p.x + size, p.y + size), IM_COL32(200, 140, 255, 200), 3, 0, 2);
  if (!s.empty() && ImGui::IsItemHovered()) {
    const ItemDef* d = s.def();
    std::string tip = magicItemTooltip(s);
    ImGui::BeginTooltip();
    ImGui::TextColored(s.tag.is_object() && s.tag.contains("en") ? ImVec4(0.8f, 0.6f, 1, 1) : ImVec4(1, 1, 1, 1), "%s", d ? d->name.c_str() : s.id.c_str());
    if (!tip.empty()) ImGui::TextDisabled("%s", tip.c_str());
    ImGui::EndTooltip();
  }
  ImGui::PopID();
  return clicked;
}

// the player's bag under a station panel: click (A) sends a stack to the station
template <class F> static void bagGrid(Player& p, float size, F onClick) {
  ImGui::TextDisabled("Inventory");
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

// ---- the spellbook (spellcraft.js) --------------------------------------------------------------------------
class SpellbookScreen : public MagicScreenBase {
public:
  const char* id() const override { return "spellbook"; }
  void onOpen(App& a) override { if (Player* p = player(a); p && level(a)) { lv0 = activeSpell(*p); } }
  void draw(App& a) override {
    Player* p = player(a); Level* lv = level(a);
    if (!p || !lv) return;
    closeOn(a, Act::Spellbook);
    const MagicData& md = magicData();
    if (!panel(a, "##spellbook", 0.84f, 0.88f)) { endPanel(); return; }
    MagicState& m = magicState(*lv, *p);
    auto known = knownSpells(*p);
    int lvl = skillLv(*p, "magic");
    ImGui::PushFont(a.fontUiBig);
    ImGui::TextColored(ImVec4(0.85f, 0.75f, 1, 1), "Spellbook \xC2\xB7 Magic %d \xC2\xB7 %d/%d mana", lvl, (int)std::floor(m.mana < 0 ? manaMax(*p) : m.mana), (int)manaMax(*p));
    ImGui::PopFont();
    ImGui::BeginChild("##sbl", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), ImGuiChildFlags_NavFlattened); ui::padScrollHere();
    std::string active = activeSpell(*p);
    for (auto& sc : md.schools) {
      std::vector<std::string> ks;
      for (auto& k : md.spellOrder) if (md.spells.at(k).school == sc[0]) ks.push_back(k);
      if (ks.empty()) continue;
      ImGui::Dummy(ImVec2(1, 4));
      ImGui::TextColored(col4(sc[2]), "%s \xC2\xB7 level %d", sc[1].c_str(), schoolLv(*p, sc[0]));
      for (auto& k : ks) {
        const SpellDef& S = md.spells.at(k);
        bool kn = std::find(known.begin(), known.end(), k) != known.end();
        ImGui::PushID(k.c_str());
        if (!kn) ImGui::BeginDisabled();
        bool sel = k == active;
        if (ImGui::Selectable("##row", sel, 0, ImVec2(0, ImGui::GetTextLineHeight())) && kn) pickSpell(*lv, *p, k);
        if (sel && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
        ImGui::SameLine(12); ImGui::TextColored(col4(S.col), "%s", S.name.c_str());
        ImGui::SameLine(190); ImGui::TextColored(ImVec4(0.6f, 0.82f, 1, 1), "Magic %d \xC2\xB7 %d mana", S.lvl, S.mana);
        ImGui::SameLine(330);
        ImGui::TextColored(ImVec4(0.73f, 0.73f, 0.73f, 1), "%s%s", kn ? "" : (lvl >= S.lvl ? "(find its tome) " : "(locked) "), S.desc.c_str());
        if (!kn) ImGui::EndDisabled();
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("\xE2\x86\x91/\xE2\x86\x93 or d-pad: choose \xC2\xB7 Enter / A / click: make active \xC2\xB7 Esc / B: close \xC2\xB7 Z casts it without a staff");
    endPanel();
  }
  std::string lv0;
};

// ---- skills and perks (skills.js + rpg.js) ------------------------------------------------------------------------
class SkillsScreen : public MagicScreenBase {
public:
  const char* id() const override { return "skills"; }
  void onOpen(App& a) override { (void)a; }
  void draw(App& a) override {
    Player* p = player(a);
    if (!p) return;
    closeOn(a, Act::Skills);
    if (a.input.pressed(Act::Journal) && frames > 1) { want = true; swapTo = 1; }
    const MagicData& md = magicData();
    if (!panel(a, "##skills", 0.62f, 0.86f)) { endPanel(); return; }
    ImGui::PushFont(a.fontUiBig); ImGui::Text("Skills"); ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 170); ImGui::TextDisabled("Total level %d", totalLevel(*p));
    float colW = (ImGui::GetContentRegionAvail().x - 16) / 2;
    if (ImGui::BeginTable("##sk", 2)) {
      for (auto& s : md.skills) {
        ImGui::TableNextColumn();
        double xp = skillXp(*p, s.key);
        int L = skillLevel(xp);
        double lo = skillXpFor(L), hi = skillXpFor(std::min(99, L + 1));
        float pr = L >= 99 ? 1.f : (float)((xp - lo) / std::max(1.0, hi - lo));
        ImGui::BeginGroup();
        ImGui::TextColored(col4(s.col), "%s", s.name.c_str()); ImGui::SameLine(colW - 40); ImGui::Text("%d", L);
        bar(pr, col32(s.col), colW - 12);
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s xp%s", thousands(xp).c_str(), L < 99 ? (" \xC2\xB7 " + thousands(std::ceil(hi - xp)) + " to level " + std::to_string(L + 1)).c_str() : "");
      }
      ImGui::EndTable();
    }
    // perks
    struct P { std::string k, name, col, pn, desc; int lv; };
    std::vector<P> have, next;
    for (auto& s : md.skills) {
      int L = skillLv(*p, s.key);
      auto it = md.perks.find(s.key);
      if (it == md.perks.end()) continue;
      for (auto& pk : it->second) (L >= pk.lvl ? have : next).push_back({s.key, s.name, s.col, pk.name, pk.desc, pk.lvl});
    }
    std::stable_sort(next.begin(), next.end(), [&](const P& x, const P& y) { return x.lv - skillLv(*p, x.k) < y.lv - skillLv(*p, y.k); });
    ImGui::Separator();
    if (have.empty()) ImGui::Text("Perks"); else ImGui::Text("Perks \xC2\xB7 %d of %d", (int)have.size(), (int)(have.size() + next.size()));
    ImGui::BeginChild("##pk", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), ImGuiChildFlags_NavFlattened); ui::padScrollHere();
    auto row = [&](const P& o, bool got) {
      ImGui::TextColored(col4(o.col, got ? 1.f : 0.55f), "%s", o.pn.c_str()); ImGui::SameLine(); ImGui::TextDisabled("%s %d", o.name.c_str(), o.lv);
      ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, got ? 1.f : 0.55f), "  %s", o.desc.c_str());
    };
    for (auto& o : have) row(o, true);
    for (size_t i = 0; i < next.size() && i < (have.empty() ? 6u : 4u); i++) row(next[i], false);
    ImGui::EndChild();
    ImGui::TextDisabled("Skills grow by doing. Hover for experience \xC2\xB7 K to close \xC2\xB7 J for your journal");
    endPanel();
  }
  void update(App& a, double dt) override {
    if (want && swapTo == 1) { want = false; swapTo = 0; a.popScreen(); a.pushScreen(makeJournalScreen()); return; }
    MagicScreenBase::update(a, dt);
  }
  int swapTo = 0;
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
    if (!panel(a, "##journal", 0.8f, 0.88f)) { endPanel(); return; }
    Story s = storyOf(*lv, *p);
    ImGui::PushFont(a.fontUiBig); ImGui::Text("Journal"); ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 200); ImGui::TextDisabled("%s", p->name.c_str());
    if (ImGui::BeginTable("##jn", 2, ImGuiTableFlags_SizingStretchProp, ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4))) {
      ImGui::TableSetupColumn("story", 0, 2.f); ImGui::TableSetupColumn("records", 0, 1.f);
      ImGui::TableNextColumn();
      ImGui::BeginChild("##story"); ui::padScrollHere();
      for (int i = 0; i < (int)s.ch.size(); i++) {
        auto& c = s.ch[i];
        bool cur = i == s.cur;
        ImVec4 tc = c.done ? ImVec4(0.55f, 0.85f, 0.55f, 1) : cur ? ImVec4(1, 0.85f, 0.45f, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1);
        ImGui::TextColored(tc, "%s %s", c.done ? "\xE2\x9C\x93" : cur ? "\xE2\x9E\xA4" : "\xC2\xB7", c.t.c_str());
        if (c.side || c.epi) { ImGui::SameLine(); ImGui::TextDisabled("%s", c.side ? "(the long way round)" : "(after)"); }
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, c.done ? 0.6f : 1), "   %s", c.goal.c_str());
        if (cur || (!c.done && (c.side || c.epi))) ImGui::TextColored(ImVec4(0.65f, 0.7f, 0.8f, 1), "   %s", c.hint.c_str());
        ImGui::PopTextWrapPos();
        if (c.prog >= 0 && !c.done) { ImGui::Indent(14); bar(c.prog, IM_COL32(220, 180, 90, 255), 220); ImGui::Unindent(14); }
        ImGui::Dummy(ImVec2(1, 3));
      }
      ImGui::EndChild();
      ImGui::TableNextColumn();
      ImGui::Text("Records");
      for (auto& [k, v] : journalRecords(*lv, *p)) { ImGui::TextDisabled("%s", k.c_str()); ImGui::SameLine(ImGui::GetColumnWidth() - ImGui::CalcTextSize(v.c_str()).x - 6); ImGui::Text("%s", v.c_str()); }
      auto foes = greatFoesBeaten(*lv);
      if (!foes.empty()) {
        ImGui::Dummy(ImVec2(1, 8)); ImGui::Text("Great foes beaten");
        std::string f; for (auto& x : foes) f += (f.empty() ? "" : " \xC2\xB7 ") + x;
        ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", f.c_str()); ImGui::PopTextWrapPos();
      }
      ImGui::EndTable();
    }
    ImGui::TextDisabled("J to close \xC2\xB7 K for your skills and perks");
    endPanel();
  }
  void update(App& a, double dt) override {
    if (want && swapTo == 1) { want = false; swapTo = 0; a.popScreen(); a.pushScreen(makeSkillsScreen()); return; }
    MagicScreenBase::update(a, dt);
  }
  int swapTo = 0;
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
    if (!panel(a, "##enchant", 0.5f, 0.82f)) { endPanel(); return; }
    float sz = std::min(44.f, ImGui::GetContentRegionAvail().x / 10.5f);
    ImGui::PushFont(a.fontUiBig); ImGui::Text("Enchant"); ImGui::PopFont();
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
        dl->AddText(ImVec2(p0.x + 6, p0.y + 4), can ? IM_COL32(90, 200, 255, 255) : IM_COL32(120, 120, 140, 255), dots.c_str());
        drawRunes(dl, ImVec2(p0.x + 50, p0.y + 5), enchRunes((u32)(p->stats.value("enchSeed", 0u) + i * 131 + o.level), 9 + i * 3), rh * 0.4f, can ? IM_COL32(220, 210, 255, 220) : IM_COL32(140, 130, 160, 160));
        std::string hint = o.order.empty() ? "" : enchName(o.order[0], o.en[o.order[0]].get<int>()) + (o.order.size() > 1 ? " \xE2\x80\xA6" : "");
        dl->AddText(ImVec2(p0.x + 50, p0.y + rh * 0.55f), can ? IM_COL32(200, 200, 200, 255) : IM_COL32(130, 130, 130, 255), hint.c_str());
        std::string L = std::to_string(o.level);
        ImVec2 ts = ImGui::CalcTextSize(L.c_str());
        dl->AddText(ImVec2(p0.x + rw - ts.x - 8, p0.y + (rh - ts.y) / 2), p->level >= o.level || cr ? IM_COL32(128, 255, 32, 255) : IM_COL32(255, 80, 80, 255), L.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndGroup();
    std::string status = !item.empty() && !has ? (enchKind(item.id).empty() || !(item.tag.is_object() && item.tag.contains("en")) ? "That can't be enchanted" : "Already enchanted \xE2\x80\x94 use books at an anvil to add more")
      : item.empty() ? "Put in something to enchant, and lapis \xC2\xB7 " + std::to_string(shelves) + " bookshel" + (shelves == 1 ? "f" : "ves") + " around (15 for the strongest)"
      : "Costs the levels shown on the right, and 1-3 lapis \xC2\xB7 you have " + std::to_string(p->level) + " level" + (p->level == 1 ? "" : "s");
    if (!item.empty() && !has && !enchKind(item.id).empty() && item.tag.is_object() && item.tag.contains("en")) status = "Already enchanted \xE2\x80\x94 use books at an anvil to add more";
    ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", status.c_str()); ImGui::PopTextWrapPos();
    ImGui::Separator();
    bagGrid(*p, sz, [&](int i) {
      ItemStack& s = p->inv[i];
      if (s.empty()) return;
      if (s.id == "lapis_lazuli") {
        if (lapis.empty()) { lapis = s; s.clear(); }
        else { int mv = std::min(64 - lapis.n, s.n); lapis.n += mv; s.n -= mv; if (s.n <= 0) s.clear(); }
      } else if (item.empty() && !enchKind(s.id).empty() && enchKind(s.id) != "ebook") { item = s; item.n = 1; if (--s.n <= 0) s.clear(); }
    });
    endPanel();
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
    if (!panel(a, "##brew", 0.5f, 0.82f)) { endPanel(); return; }
    float sz = std::min(44.f, ImGui::GetContentRegionAvail().x / 10.5f);
    ImGui::PushFont(a.fontUiBig); ImGui::Text("Brewing Stand"); ImGui::PopFont();
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
    dl->AddRectFilled(tube, ImVec2(tube.x + sz * 0.2f, tube.y + th), IM_COL32(0, 0, 0, 120), 2);
    dl->AddRectFilled(tube, ImVec2(tube.x + sz * 0.2f, tube.y + th * brewProgress(e)), IM_COL32(230, 240, 255, 220), 2);
    ImGui::SetCursorPos(ImVec2(cx - sz * 1.2f, y0 + sz + th + 6));
    for (int i = 0; i < 3; i++) { if (i) ImGui::SameLine(0, sz * 0.2f); if (slotButton(("b" + std::to_string(i)).c_str(), at(i), sz, ghosts[i])) take(i); }
    std::string st = brewStatus(e);
    ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", st.empty() ? " " : st.c_str()); ImGui::PopTextWrapPos();
    ImGui::Separator();
    bagGrid(*p, sz, [&](int i) {
      ItemStack& s = p->inv[i];
      if (s.empty()) return;
      if (potionInfo(s.id)) { for (int k = 0; k < 3; k++) if (at(k).empty()) { e["items"][k] = s.toJson(); s.clear(); changed = true; break; } return; }
      for (int k : {3, 4}) {
        if (!brewAccepts(k, s.id)) continue;
        ItemStack b = at(k);
        if (b.empty()) { e["items"][k] = s.toJson(); s.clear(); changed = true; break; }
        if (b.id == s.id) { int mv = std::min(s.maxStack() - b.n, s.n); b.n += mv; s.n -= mv; if (s.n <= 0) s.clear(); e["items"][k] = b.toJson(); changed = true; break; }
      }
    });
    if (changed) w.blockEntityChanged(pos.x, pos.y, pos.z);
    endPanel();
  }
};

} // namespace

std::unique_ptr<Screen> makeSpellbookScreen() { return std::make_unique<SpellbookScreen>(); }
std::unique_ptr<Screen> makeSkillsScreen() { return std::make_unique<SkillsScreen>(); }
std::unique_ptr<Screen> makeJournalScreen() { return std::make_unique<JournalScreen>(); }
std::unique_ptr<Screen> makeEnchantScreen(ivec3 at) { return std::make_unique<EnchantScreen>(at); }
std::unique_ptr<Screen> makeBrewScreen(ivec3 at) { return std::make_unique<BrewScreen>(at); }

} // namespace dq
