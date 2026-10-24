// ui/bootstrap_screens.cpp — minimal title / pause screens used until the full port of the
// original menus (ui/title.js, ui.js) replaces makeTitleScreen/makePauseScreen.
#include "ui/screen.h"
#include "client/app.h"
#include "client/session.h"
#include <imgui.h>
#include <imgui_stdlib.h>

namespace dq {

namespace {
class BootTitle : public Screen {
public:
  const char* id() const override { return "title"; }
  bool showsWorld() const override { return false; }
  std::string name = "New World", seed;
  void draw(App& a) override {
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("##title", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
    ImGui::PushFont(a.fontTitle); ImGui::TextColored(ImVec4(1, 0.54f, 0.24f, 1), "DokuQuest"); ImGui::PopFont();
    ImGui::TextDisabled("%s", DQ_VERSION);
    ImGui::Separator();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(a.savesDir(), ec)) {
      if (!e.is_directory() || !fs::exists(e.path() / "level.json")) continue;
      auto j = readJson(e.path() / "level.json");
      std::string label = (j ? j->value("name", e.path().filename().string()) : e.path().filename().string()) + "##" + e.path().filename().string();
      if (ImGui::Button(label.c_str(), ImVec2(320, 0))) { a.openWorld(e.path()); ImGui::End(); return; }
    }
    ImGui::Separator();
    ImGui::InputText("Name", &name); ImGui::InputText("Seed", &seed);
    if (ImGui::Button("Create New World", ImVec2(320, 0))) { LevelInfo li; li.name = name; li.seedText = seed; a.createWorld(li); ImGui::End(); return; }
    if (ImGui::Button("Quit", ImVec2(320, 0))) a.quit = true;
    ImGui::End();
  }
};
class BootPause : public Screen {
public:
  const char* id() const override { return "pause"; }
  void draw(App& a) override {
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f, a.height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Game Menu", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
    if (ImGui::Button("Back to Game", ImVec2(260, 0))) { a.popScreen(); ImGui::End(); return; }
    if (ImGui::Button("Save and Quit to Title", ImVec2(260, 0))) { ImGui::End(); a.leaveWorld(true); return; }
    ImGui::End();
  }
};
}

std::unique_ptr<Screen> makeTitleScreen() { return std::make_unique<BootTitle>(); }
std::unique_ptr<Screen> makePauseScreen() { return std::make_unique<BootPause>(); }

} // namespace dq
