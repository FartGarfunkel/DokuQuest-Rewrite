// ui/skilltree_screen.cpp — the passive skill trees (sim/skilltree.h) and the six stats (sim/combat_rpg.h), a menu tab:
// one tree at a time (tabs: warrior, ranger, rogue, mage), its nodes and their links on a canvas you drag (or move with
// the left stick) and zoom (the wheel, or the triggers); point at a node for what it gives, click it (or A on the one
// picked with the d-pad, which steps to the nearest node that way) to learn it. The side panel: points, the six stats
// with a + each, and a reset. Mouse, keyboard and controller all work (the menu tabs: LB / RB).
#include "ui/magic_screens.h"
#include "ui/ui.h"
#include "ui/book.h"
#include "ui/theme.h"
#include "ui/glyphs.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/player.h"
#include "sim/skilltree.h"
#include "sim/balance.h"
#include "sim/skills.h"
#include "common/noise.h"
#include <imgui.h>
#include <cmath>

namespace dq {

namespace {
ImU32 hexCol(const std::string& h, int a = 255) {
  if (h.size() != 6) return IM_COL32(200, 200, 200, a);
  const u32 c = (u32)std::stoul(h, nullptr, 16);
  return IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, a);
}
// each node's picture (ui/glyphs.h): by what it gives, else what it does, else its subclass
ui::Glyph nodeGlyph(const TreeNode& n) {
  return ui::glyphForNode(n.bonus.empty() ? std::string() : n.bonus[0].attr, n.desc, n.tags.empty() ? std::string() : n.tags[0], n.size);
}
ui::Glyph treeGlyph(const std::string& key) {
  return key == "warrior" ? ui::Glyph::Sword : key == "ranger" ? ui::Glyph::Arrow : key == "rogue" ? ui::Glyph::Dagger : key == "mage" ? ui::Glyph::Star : ui::Glyph::Orb;
}
// a picture on the line, then the cursor on past it (ImGui text beside it)
void glyphInline(ui::Glyph g, ImU32 c, float size) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = std::max(size, ImGui::GetTextLineHeight());
  ui::drawGlyph(ImGui::GetWindowDrawList(), g, ImVec2(p.x + size * 0.5f, p.y + h * 0.5f), size * 0.5f, c);
  ImGui::Dummy(ImVec2(size, h)); ImGui::SameLine(0, 5 * ui::Z());
}

class SkillTreeScreen : public Screen {
public:
  const char* id() const override { return "skilltree"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override {
    frames++;
    Player* p = a.session ? a.session->player : nullptr;
    if (want || !p || p->dead) { want = false; a.popScreen(); return; }
  }
  void draw(App& a) override {
    Player* p = a.session ? a.session->player : nullptr;
    if (!p) return;
    if (frames > 1 && a.input.pressed(Act::Skills)) want = true;
    const auto& trees = skillTrees();
    ImGuiIO& io = ImGui::GetIO();
    const float z = ui::Z();
    // in the book, as the spellbook and the journal are: the tree on the left page, what it gives on the right
    ui::bookPagesBegin(a, "##skilltree");
    if (trees.empty()) {
      ui::bookPageTitle(a, "Skill Trees");
      ImGui::TextWrapped("No skill trees here: none have been brought in.");
      ui::bookPagesEnd(); return;
    }
    cur = std::clamp(cur, 0, (int)trees.size() - 1);
    const SkillTree& T = trees[cur];
    ui::bookPageTitle(a, (T.name + " Tree").c_str());
    for (int i = 0; i < (int)trees.size(); i++) {   // the trees, as ink tabs
      if (i) ImGui::SameLine();
      const bool on = i == cur;
      if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
      const ImVec2 bp = ImGui::GetCursorScreenPos();
      if (ImGui::SmallButton(("     " + trees[i].name + "##t").c_str())) { cur = i; sel = -1; panX = panY = 0; }
      const float bh = ImGui::GetItemRectSize().y;
      ui::drawGlyph(ImGui::GetWindowDrawList(), treeGlyph(trees[i].key), ImVec2(bp.x + bh * 0.62f, bp.y + bh * 0.5f), bh * 0.36f, ImGui::GetColorU32(ImGuiCol_Text));
      if (on) ImGui::PopStyleColor();
    }
    if (a.input.pressed(Act::MenuX)) { cur = (cur + 1) % (int)trees.size(); sel = -1; panX = panY = 0; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // the canvas: the rest of the left page
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    const ImVec2 c0(wp.x, ImGui::GetCursorScreenPos().y + 4 * z), c1(wp.x + ws.x, wp.y + ws.y - 22 * z);
    dl->PushClipRect(c0, c1, true);
    ImGui::SetCursorScreenPos(c0);
    ImGui::InvisibleButton("##canvas", ImVec2(std::max(1.f, c1.x - c0.x), std::max(1.f, c1.y - c0.y)));
    const bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) { panX += io.MouseDelta.x / scale; panY += io.MouseDelta.y / scale; dragged = true; }
    if (hov && io.MouseWheel != 0) scale = std::clamp(scale * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), 0.2f * z, 3.f * z);
    if (scale <= 0) scale = 0.75f * z;
    { vec2 ls(0); for (auto& pd : a.input.pads()) if (glm::length(pd.lstick) > glm::length(ls)) ls = pd.lstick;   // (the left stick pans, the right zooms)
      if (glm::length(ls) > 0.2f) { panX -= ls.x * 600 * io.DeltaTime / scale; panY -= ls.y * 600 * io.DeltaTime / scale; }
      vec2 rs(0); for (auto& pd : a.input.pads()) if (std::fabs(pd.rstick.y) > std::fabs(rs.y)) rs = pd.rstick;
      if (std::fabs(rs.y) > 0.3f) scale = std::clamp(scale * (1 - rs.y * io.DeltaTime * 1.5f), 0.2f * z, 3.f * z); }
    const ImVec2 mid((c0.x + c1.x) / 2, (c0.y + c1.y) / 2);
    auto at = [&](const TreeNode& n) { return ImVec2(mid.x + (n.x + panX) * scale, mid.y + (n.y + panY) * scale); };
    // links, in ink: the learnt road gilded
    for (const TreeNode& n : T.nodes) {
      const bool hn = treeHas(*p, n.id);
      auto line = [&](const std::string& to, bool one) {
        const int j = T.find(to); if (j < 0) return;
        const bool both = hn && treeHas(*p, to);
        dl->AddLine(at(n), at(T.nodes[j]), both ? ui::ink::key : ui::withAlpha(ui::ink::muted, one ? 0.45f : 0.7f), (both ? 2.6f : 1.3f) * z);
      };
      for (auto& l : n.links) if (n.id < l) line(l, false);
      for (auto& l : n.oneWay) line(l, true);
    }
    // nodes: learnt in their colour (as ink), ready to learn in green, the rest faint; a keystone a hexagon
    int hot = -1;
    for (int i = 0; i < (int)T.nodes.size(); i++) {
      const TreeNode& n = T.nodes[i];
      const ImVec2 q = at(n);
      const float r = (n.size == 2 ? 13.f : n.size == 1 ? 9.5f : 7.f) * std::max(0.6f, scale / z) * z;
      if (q.x < c0.x - r || q.y < c0.y - r || q.x > c1.x + r || q.y > c1.y + r) continue;
      const bool has = treeHas(*p, n.id);
      const bool can = !has && treeWhyNot(*p, T, n).empty();
      const ImU32 base = has ? ui::inkOf(hexCol(n.col.empty() ? "ebd93b" : n.col)) : can ? ui::ink::good : ui::withAlpha(ui::ink::muted, 0.55f);
      if (n.size == 2) dl->AddNgonFilled(q, r, base, 6); else dl->AddCircleFilled(q, r, base, 20);
      if (n.size == 2) dl->AddNgon(q, r + 3 * z, has ? base : ui::withAlpha(ui::ink::muted, 0.6f), 6, 1.2f * z);   // (a keystone: a second rim)
      dl->AddCircle(q, r, i == sel ? ui::ink::head : ui::withAlpha(ui::ink::text, 0.6f), 20, (i == sel ? 2.5f : 1.f) * z);
      if (r >= 5.5f) ui::drawGlyph(dl, nodeGlyph(n), q, r * 0.6f, has || can ? ui::col::rgb(0xf8f1df) : ui::withAlpha(ui::col::rgb(0xf8f1df), 0.8f));
      const float dx = io.MousePos.x - q.x, dy = io.MousePos.y - q.y;
      if (hov && dx * dx + dy * dy <= r * r * 1.4f) hot = i;
    }
    dl->PopClipRect();
    auto step = [&](float ux, float uy) {   // the d-pad: the nearest node that way from the picked one (or the middle)
      const ImVec2 from = sel >= 0 ? at(T.nodes[sel]) : mid;
      int best = -1; float bd = 1e9f;
      for (int i = 0; i < (int)T.nodes.size(); i++) {
        if (i == sel) continue;
        const ImVec2 q = at(T.nodes[i]); const float dx = q.x - from.x, dy = q.y - from.y, along = dx * ux + dy * uy;
        if (along <= 1) continue;
        const float d = along + 2.5f * std::fabs(dx * uy - dy * ux);
        if (d < bd) { bd = d; best = i; }
      }
      if (best >= 0) { sel = best; const ImVec2 q = at(T.nodes[sel]); if (q.x < c0.x + 40 || q.x > c1.x - 40 || q.y < c0.y + 40 || q.y > c1.y - 40) { panX = -T.nodes[sel].x; panY = -T.nodes[sel].y; } }
    };
    if (a.input.pressed(Act::MenuLeft)) step(-1, 0);
    if (a.input.pressed(Act::MenuRight)) step(1, 0);
    if (a.input.pressed(Act::MenuUp)) step(0, -1);
    if (a.input.pressed(Act::MenuDown)) step(0, 1);
    const int show = hot >= 0 ? hot : sel;
    if (hot >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !dragged) treeLearn(*p, T.key, T.nodes[hot].id);
    if (sel >= 0 && a.input.pressed(Act::MenuOk)) treeLearn(*p, T.key, T.nodes[sel].id);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragged = false;
    ImGui::SetCursorScreenPos(ImVec2(wp.x, c1.y + 4 * z));
    ImGui::TextDisabled("Drag or stick: move \xC2\xB7 wheel: zoom \xC2\xB7 d-pad, A: learn \xC2\xB7 X: next tree");
    // the right page: the picked node, the points, the six stats, what the trees give
    ui::bookPagesRight("##skilltree-r");
    ui::bookPageTitle(a, "Your Path");
    if (show >= 0) {
      const TreeNode& n = T.nodes[show];
      const std::string why = treeWhyNot(*p, T, n);
      glyphInline(nodeGlyph(n), ui::inkOf(hexCol(n.col.empty() ? "ebd93b" : n.col)), 22 * z);
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::inkOf(hexCol(n.col.empty() ? "ebd93b" : n.col))));
      ImGui::TextWrapped("%s", n.title.empty() ? "A node" : n.title.c_str());
      ImGui::PopStyleColor();
      if (!n.desc.empty()) ImGui::TextWrapped("%s", n.desc.c_str());
      ImGui::TextDisabled("%s", treeHas(*p, n.id) ? "Learnt" : why.empty() ? "Click (or A) to learn it" : why.c_str());
    } else ImGui::TextDisabled("Point at a node (or pick one with the d-pad) to read it");
    ImGui::Separator();
    const int earned = treePointsEarned(*p), spent = treePointsSpent(*p);
    ImGui::Text("Tree points: %d of %d", earned - spent, earned);
    ImGui::TextDisabled("A point every 3 levels of your skills");
    if (ImGui::SmallButton("Reset the tree")) treeReset(*p);
    ImGui::Separator();
    ImGui::Text("Stat points: %d", p->statPoints);
    ImGui::TextDisabled("A point every %d levels of your skills, up to %d a stat", bal::STAT_EVERY, bal::STAT_CAP);
    struct S { const char* name; int* v; std::string what; ui::Glyph g; };
    auto pc = [](float f) { return std::to_string((int)std::lround(f * 100)) + "%"; };
    using namespace bal;
    S stats[] = {
        {"Constitution", &p->statCon, "Stamina and toughness. A point: +" + strFormat("%g", CON_STAMINA) + " stamina, +" + strFormat("%g", CON_HEALTH) + " health, " + pc(CON_REDUCE) + " less harm (at most " + pc(CON_REDUCE_CAP) + ").\nNow: +" + strFormat("%g", conStamina(*p)) + " stamina, +" + strFormat("%g", conHealth(*p)) + " health, " + pc(conReduction(*p)) + " less harm", ui::Glyph::Heart},
        {"Strength", &p->statStr, "Melee damage. A point: +" + pc(STR_DAMAGE) + ".\nNow: +" + pc(strDamageFactor(*p) - 1), ui::Glyph::Fist},
        {"Agility", &p->statAgi, "Quicker on your feet. A point: +" + pc(AGI_MOVE) + " speed, rolls " + pc(AGI_DODGE_COST) + " cheaper and " + pc(AGI_DODGE_TIME) + " longer, swings and bow draws " + pc(AGI_ATTACK) + " faster.\nNow: roll costs " + strFormat("%.0f", dodgeCost(*p)) + " stamina, swings x" + strFormat("%.2f", attackSpeedFactor(*p)), ui::Glyph::Wing},
        {"Intelligence", &p->statInt, "Spell power and mana. A point: +" + pc(INT_SPELL) + " spell power, +" + pc(INT_MANA) + " mana, +" + pc(INT_REGEN) + " mana regeneration.\nNow: spells x" + strFormat("%.2f", spellPowerFactor(*p)) + ", mana x" + strFormat("%.2f", manaFactor(*p)), ui::Glyph::Star},
        {"Luck", &p->statLck, "Critical hits and drops. A point: +" + strFormat("%.1f", LCK_CRIT * 100) + "% crit chance, +" + strFormat("%.1f", LCK_LOOT * 100) + "% loot luck.\nNow: " + pc(critChance(*p)) + " crit chance (at most " + pc(CRIT_CHANCE_CAP) + "), +" + pc(lootLuck(*p)) + " loot", ui::Glyph::Clover},
        {"Focus", &p->statFoc, "Posture damage: break a guard sooner for a finisher. A point: +" + pc(FOC_POSTURE) + ".\nNow: x" + strFormat("%.2f", postureFactor(*p)), ui::Glyph::Target}};
    const float pw = ImGui::GetWindowSize().x;
    for (auto& st : stats) {
      ImGui::PushID(st.name);
      glyphInline(st.g, ImGui::GetColorU32(ImGuiCol_Text), ImGui::GetTextLineHeight());
      ImGui::Text("%s", st.name); ImGui::SameLine(pw * 0.55f); ImGui::Text("%d", *st.v); ImGui::SameLine(pw * 0.7f);
      ImGui::BeginDisabled(p->statPoints <= 0 || *st.v >= bal::STAT_CAP);
      if (ImGui::SmallButton("+")) { (*st.v)++; p->statPoints--; }
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemFocused()) ui::setTooltip("%s", st.what.c_str());   // (focused: a pad's d-pad reads it too)
      ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::Text("From the trees and your gems:");
    static const char* SHOW[][2] = {{"health", "health"}, {"damage", "damage"}, {"armor", "armour"}, {"mana", "mana"}, {"crit_chance", "crit chance"},
                                    {"spell_power", "spell power"}, {"stamina", "stamina"}, {"mana_regen", "mana regeneration"}, {"spell_resist", "spell resistance"}, {"crit_damage", "crit damage"}};
    int any = 0;
    for (auto& sh : SHOW) {
      const float f = treeAttr(*p, sh[0]), pc = treeAttrPct(*p, sh[0]);
      if (f == 0 && pc == 0) continue;
      any++;
      glyphInline(ui::glyphForAttr(sh[0]), ImGui::GetColorU32(ImGuiCol_Text), ImGui::GetTextLineHeight());
      ImGui::Text("%s %s%s", sh[1], f ? ("+" + std::to_string((int)std::round(f))).c_str() : "", pc ? (" +" + std::to_string((int)std::round(pc * 100)) + "%").c_str() : "");
    }
    for (const bal::TalentRule& r : bal::TALENTS) {   // the rest, capped as they work in play (sim/balance.h)
      if (r.flatScale == 0) continue;
      bool shown = false; for (auto& sh : SHOW) shown |= r.attr == std::string(sh[0]);
      if (shown || (treeAttr(*p, r.attr) == 0 && treeAttrPct(*p, r.attr) == 0)) continue;
      any++;
      const float v = talent(*p, r.attr);
      ImGui::Text("%s %s%s", r.attr, std::string(r.attr) == "step" ? strFormat("+%.2f", v).c_str() : ((v >= 0 ? "+" : "") + std::to_string((int)std::lround(v * 100)) + "%").c_str(), v >= r.hi || v <= r.lo ? " (capped)" : "");
      if (ImGui::IsItemHovered()) ui::setTooltip("%s", r.what);
    }
    if (!any) ImGui::TextDisabled("Nothing yet");
    ui::bookPagesEnd();
  }
  bool want = false, dragged = false; int frames = 0, cur = 0, sel = -1;
  float panX = 0, panY = 0, scale = 0;
};
} // namespace

std::unique_ptr<Screen> makeSkillTreeScreen() { return std::make_unique<SkillTreeScreen>(); }

} // namespace dq
