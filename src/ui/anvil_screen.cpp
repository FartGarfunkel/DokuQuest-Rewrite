// ui/anvil_screen.cpp — smithing on an anvil (sim/anvil.h, TFC-lite): the hot piece in your other hand, the hammer in your
// main hand. Pick what to make; a bar from 0 to 150 shows where the work is (the pointer) and where it must end (the
// target, with its ±3 band); eight moves push the pointer; the plan's last three steps are shown, met ones lit. When the
// work is within the band and on plan, "Finish" takes the piece and gives what it made. Each move wears the hammer; the
// piece cools while you work (heat it again if it gets too cold). Mouse: click. Controller: the d-pad moves between the
// buttons, A presses, B closes.
#include "ui/ui.h"
#include "ui/theme.h"
#include "ui/book.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/anvil.h"
#include "sim/heat.h"
#include "sim/player.h"
#include "sim/gameplay.h"
#include "sim/combat.h"
#include "core/tool_parts.h"
#include "input/input.h"
#include "audio/audio.h"
#include <imgui.h>

namespace dq {
using namespace ui;

namespace {
const char* stepName(AnvilStep s) { static const char* N[] = {"Hit", "Draw", "Punch", "Bend", "Upset", "Shrink"}; return N[(int)s]; }
const char* whereName(AnvilRule::Where w) { static const char* N[] = {"last", "second last", "third last", "not last", "any of the last three"}; return N[(int)w]; }

class AnvilScreen : public Screen {
public:
  AnvilScreen(ivec3 at, int tier) : at(at), tier(tier) {}
  ivec3 at; int tier;
  std::vector<const AnvilRecipe*> choices;
  int pick = -1;                   // the recipe chosen (index into choices), -1: choosing
  AnvilWork work;
  int cur = 0;                     // the focused button
  std::string msg;
  bool closing = false;
  const char* id() const override { return "anvil"; }
  bool pausesGame() const override { return false; }
  bool onBack(App&) override { if (pick >= 0) { pick = -1; cur = 0; return true; } return false; }
  Player* me(App& a) { return a.session ? a.session->player : nullptr; }
  void onOpen(App& a) override { refresh(a); }
  void refresh(App& a) {
    choices.clear();
    Player* p = me(a); if (!p) return;
    const std::string piece = anvilPieceOf(p->offhand[0]);
    for (auto& r : anvilRecipes()) if (r.in == piece) choices.push_back(&r);
    if (choices.empty()) msg = "Hold the hot metal in your other hand";
  }
  bool hasFlux(Player& p) { for (auto& s : p.inv) if (s.id == "flux" && s.n > 0) return true; return false; }
  void useFlux(Player& p) { for (auto& s : p.inv) if (s.id == "flux" && s.n > 0) { if (--s.n <= 0) s = ItemStack(); return; } }
  void choose(App& a, int i) {
    if (i < 0 || i >= (int)choices.size() || !a.session) return;
    pick = i; cur = 0; msg.clear();
    work = AnvilWork{}; work.recipe = choices[i]->key;
    work.target = anvilTarget((u64)(u32)a.session->level->info.seed, work.recipe);
  }
  void move(App& a, AnvilMove m) {
    Player* p = me(a); if (!p || pick < 0) return;
    const AnvilRecipe& r = *choices[pick];
    if (!anvilCanWork(p->offhand[0], r, tier, &msg)) return;
    if (!p->held().def() || !p->held().def()->has("hammer")) { msg = "You need a hammer in your hand"; return; }
    anvilApply(work, m);
    wearHeld(*a.session->level, *p, 1);
    if (a.audio) a.audio->material("metal", "hit");
    msg.clear();
  }
  void finish(App& a) {
    Player* p = me(a); if (!p || pick < 0 || !anvilDone(work)) return;
    const AnvilRecipe& r = *choices[pick];
    if (!anvilCanWork(p->offhand[0], r, tier, &msg)) return;
    if (r.weld && !hasFlux(*p)) { msg = "Welding needs flux"; return; }
    ItemStack out = anvilOutput(r, p->offhand[0], anvilQuality(work));
    if (out.empty()) { msg = "That can't be made of this"; return; }
    if (r.weld) useFlux(*p);
    p->offhand[0].n -= r.n; if (p->offhand[0].n <= 0) p->offhand[0] = ItemStack();
    if (int left = giveItem(*a.session->level, *p, out); left > 0) { out.n = left; dropItem(*a.session->level, p->realm, out, p->pos + vec3(0, 1, 0), true); }
    if (a.audio) a.audio->material("anvil", "place");
    pick = -1; cur = 0; refresh(a);
    if (choices.empty()) closing = true;   // popped in update(): finish() can run from draw()
  }
  int buttons() const { return pick < 0 ? (int)choices.size() : (int)AnvilMove::Count + 1; }
  void update(App& a, double) override {
    if (closing || !a.session || !me(a)) { a.popScreen(); return; }
    Input& in = a.input; const int n = buttons();
    if (n <= 0) { if (in.pressed(Act::MenuOk)) a.popScreen(); return; }
    const int cols = 2;
    if (in.pressed(Act::MenuRight)) cur = std::min(n - 1, cur + 1);
    if (in.pressed(Act::MenuLeft)) cur = std::max(0, cur - 1);
    if (in.pressed(Act::MenuDown)) cur = std::min(n - 1, cur + cols);
    if (in.pressed(Act::MenuUp)) cur = std::max(0, cur - cols);
    if (in.pressed(Act::MenuOk)) press(a, cur);
  }
  void press(App& a, int i) {
    if (pick < 0) { choose(a, i); return; }
    if (i < (int)AnvilMove::Count) move(a, (AnvilMove)i); else finish(a);
  }
  void draw(App& a) override {
    Player* p = me(a); if (!p) return;
    // written in the open book, like the inventory and the other stations (ui/book.h)
    const float z = Z();
    const ItemStack& piece = p->offhand[0];
    std::string title = piece.empty() ? std::string("The anvil") : stackName(piece);
    ui::bookPagesBegin(a, "##anvil");
    ui::bookPageTitle(a, title.c_str());
    if (!piece.empty()) {
      const float h = itemHeat(piece);
      const std::string hn = heatName(h);
      ImGui::TextDisabled("%s", h > 25 ? (std::to_string((int)h) + "\xC2\xB0" "C" + (hn.empty() ? "" : " \xC2\xB7 " + hn)).c_str() : "Cold");
    }
    auto inkButton = [&](int i, const char* label, ImVec2 size, bool enabled) {   // a button in ink; the controller's one lit
      if (cur == i) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.38f, 0.16f, 0.45f));
      ImGui::BeginDisabled(!enabled);
      const bool hit = ImGui::Button(label, size);
      ImGui::EndDisabled();
      if (cur == i) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered() && !a.input.usingPad()) cur = i;   // the pad keeps its own cursor
      if (hit && !closing) press(a, i);
    };
    const float pageW = ImGui::GetContentRegionAvail().x;
    if (pick < 0) {
      ImGui::Dummy(ImVec2(1, 4 * z));
      ImGui::PushTextWrapPos(0);
      ImGui::TextUnformatted(choices.empty() ? msg.c_str() : "What will you make?");
      ImGui::PopTextWrapPos();
      const float bw = (pageW - 4 * z) / 2, bh = 18 * z;
      for (int i = 0; i < (int)choices.size(); i++) {
        if (i % 2) ImGui::SameLine(0, 4 * z);
        ImGui::PushID(i); inkButton(i, choices[i]->name.c_str(), ImVec2(bw, bh), true); ImGui::PopID();
      }
      ui::bookPagesRight("##anvil-r");
      ui::bookPageTitle(a, "Smithing");
      ImGui::PushTextWrapPos(0);
      ImGui::TextUnformatted("Hold the hot metal in your other hand and the hammer in this one. Each piece has a mark to work it to, and a plan for its last three blows.");
      ImGui::Dummy(ImVec2(1, 4 * z));
      ImGui::TextDisabled("Too cold? Heat it again in the forge.");
      ImGui::PopTextWrapPos();
      ui::bookPagesEnd();
      return;
    }
    const AnvilRecipe& r = *choices[pick];
    ImGui::Dummy(ImVec2(1, 2 * z));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ink::head), "%s", r.name.c_str());
    // the bar: 0..150, the mark with its band, the work
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b0 = ImGui::GetCursorScreenPos() + ImVec2(0, 6 * z), b1 = b0 + ImVec2(pageW, 14 * z);
    auto xOf = [&](float v) { return b0.x + (b1.x - b0.x) * v / 150.f; };
    dl->AddRectFilled(b0, b1, ink::faint);
    dl->AddRectFilled(ImVec2(xOf((float)work.target - 3), b0.y), ImVec2(xOf((float)work.target + 3), b1.y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.42f, 0.15f, 0.45f)));
    dl->AddLine(ImVec2(xOf((float)work.target), b0.y - 3 * z), ImVec2(xOf((float)work.target), b1.y + 3 * z), ink::good, 2 * z);
    dl->AddTriangleFilled(ImVec2(xOf((float)work.value), b1.y + 1 * z), ImVec2(xOf((float)work.value) - 6 * z, b1.y + 11 * z), ImVec2(xOf((float)work.value) + 6 * z, b1.y + 11 * z), ink::key);
    dl->AddRect(b0, b1, ink::text, 0, 0, std::max(1.f, z));
    ImGui::Dummy(ImVec2(pageW, 30 * z));
    ImGui::TextDisabled("The plan:");
    for (const AnvilRule& rule : r.plan) {
      AnvilRecipe one = r; one.plan = {rule};
      const bool met = anvilPlanMet(one, work.steps);
      const std::string line = std::string(stepName(rule.step)) + " " + whereName(rule.where);
      if (met) { const ImVec2 t = ImGui::GetCursorScreenPos(); drawTick(dl, t + ImVec2(0, 2 * z), 10 * z, ink::good); ImGui::Dummy(ImVec2(12 * z, 1)); ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ink::good), "%s", line.c_str()); }
      else { ImGui::Dummy(ImVec2(12 * z, 1)); ImGui::SameLine(); ImGui::TextUnformatted(line.c_str()); }
    }
    ui::bookPagesRight("##anvil-r");
    ui::bookPageTitle(a, "Work it");
    const float pw2 = ImGui::GetContentRegionAvail().x, bw = (pw2 - 4 * z) / 2, bh = 20 * z;
    for (int i = 0; i < (int)AnvilMove::Count; i++) {
      if (i % 2) ImGui::SameLine(0, 4 * z);
      char lab[40]; std::snprintf(lab, sizeof lab, "%s %+d", anvilMoveName((AnvilMove)i), anvilMoveValue((AnvilMove)i));
      ImGui::PushID(100 + i); inkButton(i, lab, ImVec2(bw, bh), true); ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(1, 4 * z));
    const bool done = anvilDone(work);
    inkButton((int)AnvilMove::Count, done ? (work.value == work.target ? "Finish (perfect work)" : "Finish") : "Finish (not yet)", ImVec2(pw2, bh), done);
    if (!msg.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ink::bad), "%s", msg.c_str()); ImGui::PopTextWrapPos(); }
    ImGui::TextDisabled("%s", a.input.usingPad() ? "A strike \xC2\xB7 B back" : "Esc: back");
    ui::bookPagesEnd();
  }
};
}

std::unique_ptr<Screen> makeAnvilScreen(ivec3 at, int tier) { return std::make_unique<AnvilScreen>(at, tier); }

} // namespace dq
