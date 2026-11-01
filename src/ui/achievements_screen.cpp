// ui/achievements_screen.cpp — the Achievements tab (sim/achievements.h): every one, earned in colour with when, the
// rest faded with what to do. Controller: the d-pad / stick scrolls, LB/RB change tab, B or the inventory button closes.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/achievements.h"
#include "sim/player.h"
#include <imgui.h>
#include <ctime>

namespace dq {

bool inventoryClosePressed(App& a, bool tabScreen);   // ui/ui_main.cpp

namespace {
class AchievementsScreen : public Screen {
public:
  const char* id() const override { return "achievements"; }
  bool pausesGame() const override { return false; }
  void update(App& a, double) override {
    if (inventoryClosePressed(a, true) || a.input.pressed(Act::MenuBack)) { a.popScreen(); return; }
    if (a.input.pressed(Act::MenuDown)) scroll += 60;
    if (a.input.pressed(Act::MenuUp)) scroll -= 60;
  }
  void draw(App& a) override {
    GameSession* s = a.session && a.session->player ? a.session.get() : nullptr;
    if (!s) return;
    Player& p = *s->player;
    const json* got = p.data.is_object() && p.data.contains("ach") && p.data["ach"].is_object() ? &p.data["ach"] : nullptr;
    const auto& L = achievementList();
    int n = 0; for (auto& d : L) if (got && got->contains(d.id)) n++;
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(760.f, a.width - 30.f), std::min(540.f, a.height - 30.f)));
    ImGui::Begin("##achievements", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::Text("Achievements");
    ImGui::SameLine(ImGui::GetWindowWidth() - 180); ImGui::TextDisabled("%d of %d earned", n, (int)L.size());
    ImGui::Separator();
    ImGui::BeginChild("##ach_list", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing() * 1.4f), ImGuiChildFlags_Borders);
    ui::padScrollHere();
    if (scroll != 0) { ImGui::SetScrollY(std::max(0.f, ImGui::GetScrollY() + scroll)); scroll = 0; }
    const float W = ImGui::GetContentRegionAvail().x, cw = (W - 8) / 2, ch = 58;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < L.size(); i++) {
      const AchDef& d = L[i];
      const bool ok = got && got->contains(d.id);
      if (i % 2) ImGui::SameLine(0, 8);
      const ImVec2 at = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(cw, ch));
      dl->AddRectFilled(at, ImVec2(at.x + cw, at.y + ch), ok ? IM_COL32(70, 54, 30, 220) : IM_COL32(34, 30, 28, 200), 4);
      dl->AddRect(at, ImVec2(at.x + cw, at.y + ch), ok ? IM_COL32(230, 180, 80, 255) : IM_COL32(80, 72, 66, 255), 4, 0, ok ? 2 : 1);
      ui::drawItem(dl, ItemStack(d.icon, 1), ImVec2(at.x + 9, at.y + 9), 40, ok ? 1.f : 0.3f, false);
      dl->AddText(ImVec2(at.x + 58, at.y + 8), ok ? IM_COL32(255, 226, 150, 255) : IM_COL32(170, 160, 150, 255), d.title);
      std::string sub = d.desc;
      if (ok) {
        const long long when = (*got)[d.id].is_number() ? (*got)[d.id].get<long long>() : 0;
        if (when > 0) { std::time_t t = (std::time_t)when; char b[32]; std::strftime(b, sizeof b, "%Y-%m-%d", std::localtime(&t)); sub += std::string("  \xC2\xB7  ") + b; }
      }
      dl->AddText(ImVec2(at.x + 58, at.y + 30), ok ? IM_COL32(220, 210, 190, 255) : IM_COL32(130, 122, 114, 255), sub.c_str());
    }
    ImGui::EndChild();
    ImGui::TextDisabled("LB / RB or Q / E for the other tabs \xC2\xB7 Esc to close");
    ImGui::End();
  }
private:
  float scroll = 0;
};
} // namespace

std::unique_ptr<Screen> makeAchievementsScreen() { return std::make_unique<AchievementsScreen>(); }

} // namespace dq
