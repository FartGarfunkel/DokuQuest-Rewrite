// ui/knapping_screen.cpp — knapping (sim/knapping.h, after TerraFirmaCraft's GuiKnapping): a 5x5 grid of the stone,
// flint or clay in your hands; chip cells away until what's left is a shape, then take what you've made. Stone and flint:
// the first chip spends one; clay: five, when you take the piece. Mouse: click a cell to chip it (or hold and drag across
// the grid to chip all it passes), click the result to take it. Controller: the d-pad / stick moves, A chips (or takes, on the result), X takes, B closes. Esc closes.
#include "ui/ui.h"
#include "ui/theme.h"
#include "ui/book.h"
#include "assets/icons.h"
#include "core/registry.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/knapping.h"
#include "sim/player.h"
#include "sim/gameplay.h"
#include "input/input.h"
#include "audio/audio.h"
#include <imgui.h>

namespace dq {
using namespace ui;

namespace {
struct KnapLook { IconRef icon; ImU32 tint = IM_COL32_WHITE; };
KnapLook knapLook(const std::string& material) {   // the block whose texture the slab wears, and its tint
  const bool clay = material == "clay" || material == "fireclay";
  KnapLook k;
  const BlockId b = reg().blockId(clay ? "clay" : "stone");
  if (b) k.icon = itemIcons().tile(reg().block(b).faces[2]);
  if (material == "flint") k.tint = IM_COL32(92, 94, 104, 255);          // flint: dark and cool
  else if (material == "fireclay") k.tint = IM_COL32(255, 238, 222, 255); // fire clay: paler, warmer
  if (!k.icon.valid()) k.tint = clay ? IM_COL32(160, 166, 178, 255) : IM_COL32(128, 128, 128, 255);
  return k;
}
class KnappingScreen : public Screen {
public:
  KnappingScreen(std::string material, std::string item) : material(std::move(material)), item(std::move(item)) {}
  std::string material, item;
  KnapGrid grid;
  int cur = 12;               // the cursor (0..24; 25 = the result slot)
  bool spent = false;          // the first chip has used up a stone
  bool dragging = false, toldEmpty = false;       // the mouse held down from the grid: every piece swept over is chipped
  const char* id() const override { return "knapping"; }
  bool pausesGame() const override { return false; }
  bool onBack(App&) override { return false; }

  int have(Player& p) const { int n = 0; for (auto& s : p.inv) if (s.id == item) n += s.n; for (auto& s : p.offhand) if (s.id == item) n += s.n; return n; }
  bool spend(Player& p, int n) {
    if (have(p) < n) return false;
    auto take = [&](ItemStack& s) { if (n > 0 && s.id == item) { const int k = std::min(n, s.n); s.n -= k; n -= k; if (s.n <= 0) s = ItemStack(); } };
    take(p.held()); for (auto& s : p.inv) take(s); for (auto& s : p.offhand) take(s);
    return true;
  }
  void onOpen(App& a) override {
    if (!a.session || !a.session->player || have(*a.session->player) < knapNeeds(material)) a.popScreen();
  }
  void chip(App& a, int i) {
    Player& p = *a.session->player;
    if (i < 0 || i >= 25 || !grid.kept[i]) return;
    if (!spent && material != "clay" && material != "fireclay") { if (!spend(p, knapCost(material))) { if (!toldEmpty) hudToast("You've nothing left to knap", "warn"); toldEmpty = true; return; } spent = true; }
    grid.kept[i] = false;
    if (a.audio) a.audio->material(material == "clay" ? "gravel" : "stone", "hit");
  }
  void takeResult(App& a) {
    if (closing) return;   // (once: A on the result and X in the same frame would give it twice)
    const KnapPattern* m = knapMatch(material, grid);
    if (!m || !a.session) return;
    Player& p = *a.session->player;
    if ((material == "clay" || material == "fireclay") && !spend(p, knapCost(material))) { hudToast("You need five clay", "warn"); return; }
    ItemStack out(m->out, m->n);
    if (int left = giveItem(*a.session->level, p, out); left > 0) { out.n = left; dropItem(*a.session->level, p.realm, out, p.pos + vec3(0, 1, 0), true); }
    closing = true;   // popped in update(): this can run from draw(), and popping frees the screen under it
  }
  bool closing = false;
  void update(App& a, double) override {
    if (closing || !a.session || !a.session->player) { a.popScreen(); return; }
    Input& in = a.input;
    if (in.pressed(Act::MenuLeft) && cur < 25 && cur % 5 > 0) cur--;
    if (in.pressed(Act::MenuRight)) { if (cur < 25 && cur % 5 < 4) cur++; else if (cur < 25) cur = 25; }
    if (in.pressed(Act::MenuUp) && cur < 25 && cur >= 5) cur -= 5;
    if (in.pressed(Act::MenuDown) && cur < 20) cur += 5;
    if (cur == 25 && in.pressed(Act::MenuLeft)) cur = 14;
    if (in.pressed(Act::MenuOk)) { if (cur == 25) takeResult(a); else chip(a, cur); }
    if (in.pressed(Act::MenuX)) takeResult(a);
  }
  void draw(App& a) override {
    if (!a.session) return;
    // written in the open book, like the inventory and the other stations (ui/book.h)
    const float z = Z();
    const char* title = material == "clay" || material == "fireclay" ? "Shape the clay" : material == "flint" ? "Knap the flint" : "Knap the stone";
    ui::bookPagesBegin(a, "##knapping");
    ui::bookPageTitle(a, title);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 avail = ImGui::GetContentRegionAvail(), g0 = ImGui::GetCursorScreenPos();
    const float gap = 2 * z, cs = std::floor(std::min((avail.x - 4 * gap) / 5, (avail.y - 34 * z - 4 * gap) / 5));
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool click = ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered();
    // click and drag: holding the button down and sweeping over the grid chips every piece it passes (a drag starts on the grid)
    const ImVec2 gEnd = g0 + ImVec2(5 * cs + 4 * gap, 5 * cs + 4 * gap);
    const bool overGrid = mouse.x >= g0.x && mouse.x < gEnd.x && mouse.y >= g0.y && mouse.y < gEnd.y;
    if (click && overGrid) dragging = true;
    if (!ImGui::IsMouseDown(0)) { dragging = false; toldEmpty = false; }   // (told once per drag, not per piece swept)
    // the material laid flat, a block of its own texture (stone, clay; flint a dark stone), each piece a square of it
    const KnapLook look = knapLook(material);
    for (int i = 0; i < 25; i++) {
      const ImVec2 c0 = g0 + ImVec2((i % 5) * (cs + gap), (i / 5) * (cs + gap)), c1 = c0 + ImVec2(cs, cs);
      const bool hov = mouse.x >= c0.x && mouse.x < c1.x && mouse.y >= c0.y && mouse.y < c1.y;
      if (hov && !a.input.usingPad()) cur = i;   // the pad keeps its own cursor (the hidden pointer must not steal it)
      if (grid.kept[i]) {
        if (look.icon.valid()) dl->AddImage((ImTextureID)(intptr_t)look.icon.tex, c0, c1, ImVec2(look.icon.uv0.x, look.icon.uv0.y), ImVec2(look.icon.uv1.x, look.icon.uv1.y), look.tint);
        else dl->AddRectFilled(c0, c1, look.tint);
        dl->AddRect(c0, c1, IM_COL32(0, 0, 0, 70), 0, 0, std::max(1.f, z));   // a fine seam between the pieces
      } else dl->AddRectFilled(c0, c1, ink::faint);   // chipped away
      if (i == cur) dl->AddRect(c0 - ImVec2(1, 1) * z, c1 + ImVec2(1, 1) * z, ink::key, 0, 0, 2 * z);
      if (hov && (click || dragging) && grid.kept[i]) chip(a, i);
    }
    ImGui::Dummy(ImVec2(5 * cs + 4 * gap, 5 * cs + 4 * gap));
    ImGui::Dummy(ImVec2(1, 4 * z));
    ImGui::TextDisabled("%s", a.input.usingPad() ? "A chip \xC2\xB7 X take \xC2\xB7 B close" : "Click a piece to chip it away, or drag across them");
    ui::bookPagesRight("##knapping-r");
    ui::bookPageTitle(a, "What you'll make");
    const KnapPattern* m = knapMatch(material, grid);
    const float ss = 34 * z;
    const ImVec2 r0 = ImGui::GetCursorScreenPos();
    SlotLook lk; lk.out = true; lk.ready = m != nullptr; lk.focus = cur == 25;
    drawSlot(ImGui::GetWindowDrawList(), r0, ss, lk);
    if (m) drawItem(ImGui::GetWindowDrawList(), ItemStack(m->out, m->n), r0 + ImVec2(ss * 0.1f, ss * 0.1f), ss * 0.8f);
    const bool rhov = mouse.x >= r0.x && mouse.x < r0.x + ss && mouse.y >= r0.y && mouse.y < r0.y + ss;
    if (rhov && !a.input.usingPad()) cur = 25;
    ImGui::Dummy(ImVec2(ss, ss));
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (m) { const ItemDef* d = reg().item(m->out); ImGui::TextUnformatted(d ? d->name.c_str() : m->out.c_str()); }
    else ImGui::TextDisabled("Nothing yet");
    ImGui::BeginDisabled(!m);
    if ((ImGui::Button("Take it") || (rhov && click && m)) && !closing) takeResult(a);
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, 6 * z));
    ImGui::TextDisabled("Shapes:");
    ImGui::PushTextWrapPos(0);
    for (auto& p : knapPatterns(material)) {
      const ItemDef* d = reg().item(p.out);
      const std::string n = (d ? d->name : p.out) + (p.n > 1 ? " x" + std::to_string(p.n) : "");
      if (m == &p) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ink::good), "%s", n.c_str()); else ImGui::TextUnformatted(n.c_str());
    }
    ImGui::PopTextWrapPos();
    ui::bookPagesEnd();
  }
};
}

std::unique_ptr<Screen> makeKnappingScreen(const std::string& material, const std::string& item) { return std::make_unique<KnappingScreen>(material, item); }

} // namespace dq
