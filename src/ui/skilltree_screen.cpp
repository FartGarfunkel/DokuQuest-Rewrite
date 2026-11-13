// ui/skilltree_screen.cpp — the passive skill trees (sim/skilltree.h) and the six stats (sim/combat_rpg.h), a menu tab:
// one tree at a time (tabs: warrior, ranger, rogue, mage), its nodes and their links on a canvas you drag (or move with
// the left stick) and zoom (the wheel, or the triggers); point at a node for what it gives, click it (or A on the one
// picked with the d-pad, which steps to the nearest node that way) to learn it. The side panel: points, the six stats
// with a + each, and a reset. Mouse, keyboard and controller all work (the menu tabs: LB / RB).
#include "ui/magic_screens.h"
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/player.h"
#include "sim/skilltree.h"
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
    const ImVec2 ds = io.DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ds);
    ImGui::Begin("##skilltree", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(10, 10, 16, 235));
    const float z = ui::Z();
    if (trees.empty()) {
      ImGui::SetCursorPos(ImVec2(40 * z, 80 * z));
      ImGui::TextWrapped("No skill trees here: they come from Soulrend's (tools/port/soulrend_tree.py).");
      ImGui::End(); return;
    }
    cur = std::clamp(cur, 0, (int)trees.size() - 1);
    const SkillTree& T = trees[cur];
    // tabs
    ImGui::SetCursorPos(ImVec2(40 * z, 120 * z));   // (inside the book's page: its edges are cropped)
    for (int i = 0; i < (int)trees.size(); i++) {
      if (i) ImGui::SameLine();
      const bool on = i == cur;
      if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
      if (ImGui::Button(trees[i].name.c_str())) { cur = i; sel = -1; panX = panY = 0; }
      if (on) ImGui::PopStyleColor();
    }
    if (a.input.pressed(Act::MenuX)) { cur = (cur + 1) % (int)trees.size(); sel = -1; panX = panY = 0; }
    // the side panel
    const float side = 300 * z;
    const int earned = treePointsEarned(*p), spent = treePointsSpent(*p);
    ImGui::SetCursorPos(ImVec2(ds.x - side - 30 * z, 120 * z));
    ImGui::BeginChild("##treeside", ImVec2(side - 16 * z, ds.y - 250 * z), true);
    ImGui::Text("Tree points: %d of %d", earned - spent, earned);
    ImGui::TextDisabled("A point every 3 levels of your skills");
    if (ImGui::Button("Reset the tree")) treeReset(*p);
    ImGui::Separator();
    ImGui::Text("Stat points: %d", p->statPoints);
    struct S { const char* name; int* v; const char* what; };
    S stats[] = {{"Constitution", &p->statCon, "stamina and toughness"}, {"Strength", &p->statStr, "+1% damage a point"}, {"Agility", &p->statAgi, "quicker on your feet"},
                 {"Intelligence", &p->statInt, "spell power"}, {"Luck", &p->statLck, "critical hits"}, {"Focus", &p->statFoc, "posture damage"}};
    for (auto& s : stats) {
      ImGui::PushID(s.name);
      ImGui::Text("%-13s %2d", s.name, *s.v);
      ImGui::SameLine(side - 70 * z);
      ImGui::BeginDisabled(p->statPoints <= 0 || *s.v >= 15);
      if (ImGui::SmallButton("+")) { (*s.v)++; p->statPoints--; }
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", s.what);
      ImGui::PopID();
    }
    ImGui::Separator();
    // what the tree gives you, added up
    ImGui::Text("From the trees:");
    static const char* SHOW[][2] = {{"health", "health"}, {"damage", "damage"}, {"armor", "armour"}, {"mana", "mana"}, {"crit_chance", "crit chance"},
                                    {"spell_power", "spell power"}, {"stamina", "stamina"}};
    for (auto& sh : SHOW) {
      const float f = treeAttr(*p, sh[0]), pc = treeAttrPct(*p, sh[0]);
      if (f == 0 && pc == 0) continue;
      ImGui::BulletText("%s %s%s", sh[1], f ? ("+" + std::to_string((int)std::round(f))).c_str() : "", pc ? (" +" + std::to_string((int)std::round(pc * 100)) + "%").c_str() : "");
    }
    ImGui::EndChild();
    // the canvas
    const ImVec2 c0(40 * z, 160 * z), c1(ds.x - side - 40 * z, ds.y - 130 * z);
    dl->PushClipRect(c0, c1, true);
    ImGui::SetCursorScreenPos(c0);
    ImGui::InvisibleButton("##canvas", ImVec2(c1.x - c0.x, c1.y - c0.y));
    const bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) { panX += io.MouseDelta.x / scale; panY += io.MouseDelta.y / scale; dragged = true; }
    if (hov && io.MouseWheel != 0) scale = std::clamp(scale * (io.MouseWheel > 0 ? 1.15f : 1 / 1.15f), 0.25f * z, 3.f * z);
    if (scale <= 0) scale = 0.8f * z;
    // the controller: the left stick pans, the shoulders zoom, the d-pad steps between nodes
    { vec2 ls(0); for (auto& pd : a.input.pads()) if (glm::length(pd.lstick) > glm::length(ls)) ls = pd.lstick;   // (the left stick pans, the right zooms)
      if (glm::length(ls) > 0.2f) { panX -= ls.x * 600 * io.DeltaTime / scale; panY -= ls.y * 600 * io.DeltaTime / scale; }
      vec2 rs(0); for (auto& pd : a.input.pads()) if (std::fabs(pd.rstick.y) > std::fabs(rs.y)) rs = pd.rstick;
      if (std::fabs(rs.y) > 0.3f) scale = std::clamp(scale * (1 - rs.y * io.DeltaTime * 1.5f), 0.25f * z, 3.f * z); }
    const ImVec2 mid((c0.x + c1.x) / 2, (c0.y + c1.y) / 2);
    auto at = [&](const TreeNode& n) { return ImVec2(mid.x + (n.x + panX) * scale, mid.y + (n.y + panY) * scale); };
    // links
    for (const TreeNode& n : T.nodes) {
      const bool hn = treeHas(*p, n.id);
      auto line = [&](const std::string& to, bool one) {
        const int j = T.find(to); if (j < 0) return;
        const bool both = hn && treeHas(*p, to);
        dl->AddLine(at(n), at(T.nodes[j]), both ? IM_COL32(235, 200, 90, 230) : IM_COL32(120, 120, 140, one ? 110 : 160), (both ? 3.f : 1.6f) * z);
      };
      for (auto& l : n.links) if (n.id < l) line(l, false);
      for (auto& l : n.oneWay) line(l, true);
    }
    // nodes
    int hot = -1;
    for (int i = 0; i < (int)T.nodes.size(); i++) {
      const TreeNode& n = T.nodes[i];
      const ImVec2 q = at(n);
      const float r = (n.size == 2 ? 13.f : n.size == 1 ? 9.f : 6.f) * std::max(0.6f, scale / z) * z;
      if (q.x < c0.x - r || q.y < c0.y - r || q.x > c1.x + r || q.y > c1.y + r) continue;
      const bool has = treeHas(*p, n.id);
      const bool can = !has && treeWhyNot(*p, T, n).empty();
      const ImU32 base = has ? hexCol(n.col.empty() ? "ebd93b" : n.col) : can ? IM_COL32(150, 210, 150, 255) : IM_COL32(70, 70, 86, 255);
      if (n.size == 2) dl->AddNgonFilled(q, r, base, 6); else dl->AddCircleFilled(q, r, base, 20);
      dl->AddCircle(q, r, i == sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 26, 255), 20, (i == sel ? 2.5f : 1.2f) * z);
      const float dx = io.MousePos.x - q.x, dy = io.MousePos.y - q.y;
      if (hov && dx * dx + dy * dy <= r * r * 1.4f) hot = i;
    }
    dl->PopClipRect();
    // the d-pad: the nearest node that way from the picked one (or the middle)
    auto step = [&](float ux, float uy) {
      const ImVec2 from = sel >= 0 ? at(T.nodes[sel]) : mid;
      int best = -1; float bd = 1e9f;
      for (int i = 0; i < (int)T.nodes.size(); i++) {
        if (i == sel) continue;
        const ImVec2 q = at(T.nodes[i]); const float dx = q.x - from.x, dy = q.y - from.y, along = dx * ux + dy * uy;
        if (along <= 1) continue;
        const float d = along + 2.5f * std::fabs(dx * uy - dy * ux);
        if (d < bd) { bd = d; best = i; }
      }
      if (best >= 0) { sel = best; const ImVec2 q = at(T.nodes[sel]); if (q.x < c0.x + 60 || q.x > c1.x - 60 || q.y < c0.y + 60 || q.y > c1.y - 60) { panX = -T.nodes[sel].x; panY = -T.nodes[sel].y; } }
    };
    if (a.input.pressed(Act::MenuLeft)) step(-1, 0);
    if (a.input.pressed(Act::MenuRight)) step(1, 0);
    if (a.input.pressed(Act::MenuUp)) step(0, -1);
    if (a.input.pressed(Act::MenuDown)) step(0, 1);
    const int show = hot >= 0 ? hot : sel;
    if (show >= 0) {
      const TreeNode& n = T.nodes[show];
      const std::string why = treeWhyNot(*p, T, n);
      ImGui::BeginTooltip();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(hexCol(n.col.empty() ? "ebd93b" : n.col)), "%s", n.title.empty() ? "A node" : n.title.c_str());
      if (!n.desc.empty()) ImGui::TextWrapped("%s", n.desc.c_str());
      ImGui::TextDisabled("%s", why.empty() ? "Click (or A) to learn it" : why.c_str());
      ImGui::EndTooltip();
    }
    if (hot >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !dragged) treeLearn(*p, T.key, T.nodes[hot].id);
    if (sel >= 0 && a.input.pressed(Act::MenuOk)) treeLearn(*p, T.key, T.nodes[sel].id);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragged = false;
    ImGui::SetCursorPos(ImVec2(40 * z, ds.y - 120 * z));
    ImGui::TextDisabled("Drag or left stick: move \xC2\xB7 wheel: zoom \xC2\xB7 d-pad: pick a node, A: learn \xC2\xB7 X: next tree \xC2\xB7 K: close");
    ImGui::End();
  }
  bool want = false, dragged = false; int frames = 0, cur = 0, sel = -1;
  float panX = 0, panY = 0, scale = 0;
};
} // namespace

std::unique_ptr<Screen> makeSkillTreeScreen() { return std::make_unique<SkillTreeScreen>(); }

} // namespace dq
