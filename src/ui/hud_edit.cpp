// ui/hud_edit.cpp — moving the HUD's pieces (BTA's rearrangeable HUD): Options -> Interface -> Move HUD pieces.
// The world and the HUD stay drawn; each piece (hotbar, hearts and bars, boss bar, chat, toasts) has a frame.
// Mouse: drag a piece. Keyboard: Tab picks, arrows nudge, R puts it back, Esc done.
// Controller: LB/RB pick, left stick or d-pad move, X puts it back, Y puts every piece back, A or B done.
// Offsets live in settings extra "hudLayout" as fractions of the screen (ui/hud.cpp hudPieceEnd applies them).
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include <imgui.h>
#include <map>

namespace dq {
using namespace ui;

std::map<std::string, std::pair<ImVec2, ImVec2>>& hudPieceBoxes();   // ui/hud.cpp

namespace {
const char* PIECES[] = {"hotbar", "bars", "boss", "chat", "toasts"};
const char* NAMES[] = {"Hotbar", "Hearts and bars", "Boss bar", "Chat", "Messages"};

class HudEditScreen : public Screen {
public:
  const char* id() const override { return "hud_edit"; }
  bool pausesGame() const override { return true; }
  int sel = 0; bool dragging = false;
  json& layout(App& a) { json& L = a.settings.extra["hudLayout"]; if (!L.is_object()) L = json::object(); return L; }
  void nudge(App& a, const char* piece, float dx, float dy) {
    json& L = layout(a);
    float fx = 0, fy = 0;
    if (L.contains(piece) && L[piece].is_array() && L[piece].size() >= 2) { fx = L[piece][0]; fy = L[piece][1]; }
    fx = clampv(fx + dx / std::max(1, a.width), -0.95f, 0.95f); fy = clampv(fy + dy / std::max(1, a.height), -0.95f, 0.95f);
    L[piece] = json::array({fx, fy});
  }
  void update(App& a, double dt) override {
    Input& in = a.input;
    if (in.padButtonPressed(5) || (in.keyPressed(SDL_SCANCODE_TAB) && !in.shiftHeld())) sel = (sel + 1) % 5;
    if (in.padButtonPressed(4) || (in.keyPressed(SDL_SCANCODE_TAB) && in.shiftHeld())) sel = (sel + 4) % 5;
    if (in.padButtonPressed(2) || in.keyPressed(SDL_SCANCODE_R)) layout(a).erase(PIECES[sel]);
    if (in.padButtonPressed(3)) layout(a) = json::object();
    vec2 m = in.moveAxis();
    if (glm::length(m) > 0.2f) nudge(a, PIECES[sel], m.x * 420.f * (float)dt, -m.y * 420.f * (float)dt);
    ivec2 st = in.navStep();
    if (st != ivec2(0)) nudge(a, PIECES[sel], st.x * 4.f * ui::Z(), st.y * 4.f * ui::Z());
    if (in.padButtonPressed(0)) { done(a); return; }
  }
  void done(App& a) { a.settings.save(); uiDefer([this](App& app) { if (app.topScreen() == this) app.popScreen(); }); }
  bool onBack(App& a) override { a.settings.save(); return false; }
  void draw(App& a) override {
    float z = Z();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    auto& B = hudPieceBoxes();
    for (int i = 0; i < 5; i++) {
      auto it = B.find(PIECES[i]);
      if (it == B.end()) continue;
      ImVec2 lo = it->second.first - ImVec2(4, 4) * z, hi = it->second.second + ImVec2(4, 4) * z;
      bool s = i == sel;
      dl->AddRectFilled(lo, hi, s ? IM_COL32(255, 210, 90, 40) : IM_COL32(255, 255, 255, 18), 4 * z);
      dl->AddRect(lo, hi, s ? col::gold : IM_COL32(255, 255, 255, 120), 4 * z, 0, (s ? 3 : 1.5f) * z);
      ImVec2 ts = textSize(F_MID, NAMES[i]);
      ImVec2 tp(lo.x + 4 * z, lo.y - ts.y - 4 * z);
      if (tp.y < 2) tp.y = hi.y + 4 * z;
      dl->AddRectFilled(tp - ImVec2(3, 1) * z, tp + ts + ImVec2(3, 1) * z, IM_COL32(0, 0, 0, 170), 3 * z);
      text(dl, F_MID, tp, s ? col::gold : col::text, NAMES[i]);
      // the mouse: press on a frame to pick it and drag it
      if (ImGui::IsMouseClicked(0) && io.MousePos.x >= lo.x && io.MousePos.x <= hi.x && io.MousePos.y >= lo.y && io.MousePos.y <= hi.y) { sel = i; dragging = true; }
    }
    if (dragging) {
      if (ImGui::IsMouseDown(0)) { if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) nudge(a, PIECES[sel], io.MouseDelta.x, io.MouseDelta.y); }
      else dragging = false;
    }
    // the help strip
    const char* help = a.input.usingPad() ? "LB / RB pick  \xC2\xB7  stick or d-pad move  \xC2\xB7  X put back  \xC2\xB7  Y put all back  \xC2\xB7  A done"
                                          : "Drag a piece  \xC2\xB7  Tab picks  \xC2\xB7  arrows nudge  \xC2\xB7  R puts it back  \xC2\xB7  Esc done";
    ImVec2 hs = textSize(F_BODY, help);
    ImVec2 hp((a.width - hs.x) * 0.5f, 18 * z);
    dl->AddRectFilled(hp - ImVec2(12, 8) * z, hp + hs + ImVec2(12, 8) * z, IM_COL32(10, 8, 6, 210), 6 * z);
    text(dl, F_BODY, hp, col::text, help);
    ImGui::SetNextWindowPos(ImVec2(a.width * 0.5f - 60 * z, hp.y + hs.y + 14 * z));
    ImGui::Begin("##hudedit", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    if (button("Done", BTN_PRIMARY, true, 120 * z)) done(a);
    ImGui::End();
  }
};
} // namespace

std::unique_ptr<Screen> makeHudEditScreen() { return std::make_unique<HudEditScreen>(); }

} // namespace dq
