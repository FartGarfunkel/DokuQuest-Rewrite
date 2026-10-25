// ui/sign_screen.cpp — writing on a sign (ui/hud.js signEditor, .sign-edit / .sign-board): four lines of up to
// 16 characters on a planks board. Enter moves to the next line (on the last: done), Esc / B saves and closes.
// Controller: d-pad up/down picks the line or the Done button, A presses Done.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <array>

namespace dq {
using namespace ui;

namespace {
class SignScreen : public Screen {
public:
  explicit SignScreen(ivec3 at) : at(at) {}
  ivec3 at;
  std::array<std::string, 4> lines;
  int focusLine = 0;            // the box that gets keyboard focus next frame (-1 = none)
  bool saved = false;
  const char* id() const override { return "sign"; }
  bool pausesGame() const override { return false; }
  bool padOpen = false;         // opened with a controller: focus starts on Done (no on-screen keyboard yet)
  void onOpen(App& a) override {
    padOpen = a.input.usingPad();
    if (padOpen) focusLine = -1;
    if (!a.session) return;
    if (json* e = a.session->world().blockEntity(at.x, at.y, at.z)) if (e->contains("lines") && (*e)["lines"].is_array())
      for (size_t i = 0; i < 4 && i < (*e)["lines"].size(); i++) if ((*e)["lines"][i].is_string()) lines[i] = (*e)["lines"][i].get<std::string>();
  }
  void save(App& a) {
    if (saved || !a.session) return;
    saved = true;
    World& w = a.session->world();
    if (!reg().block(w.getBlock(at.x, at.y, at.z)).has("sign")) return;   // broken while we wrote
    json* e = w.blockEntity(at.x, at.y, at.z, true);
    if (!e->is_object()) *e = json::object();
    (*e)["type"] = "sign";
    json arr = json::array();
    for (auto& l : lines) arr.push_back(l.substr(0, 16));
    (*e)["lines"] = arr;
    w.blockEntityChanged(at.x, at.y, at.z);
  }
  void onClose(App& a) override { save(a); }
  bool onBack(App& a) override { return false; }   // Esc / B: close (onClose saves)
  void update(App& a, double) override {
    if (!a.session) { a.popScreen(); return; }
    if (!reg().block(a.session->world().getBlock(at.x, at.y, at.z)).has("sign")) { saved = true; a.popScreen(); }
  }
  void draw(App& a) override {
    float z = Z(), W = (float)a.width, H = (float)a.height;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 3, 2, 140));
    float bw = std::min(360.f * z, W - 32 * z), ih = 34 * z, gap = 4 * z;
    float titleH = textSize(F_H3, "Ag", 0, 22.f / fontPx(F_H3)).y;
    float btnH = textSize(F_BODY, "Ag").y + 12 * z;
    float bh = 14 * z + titleH + 4 * z + 4 * (ih + gap) + 6 * z + btnH + 12 * z;
    ImVec2 p0((W - bw) * 0.5f, (H - bh) * 0.5f), p1 = p0 + ImVec2(bw, bh);
    ImGui::SetNextWindowPos(p0); ImGui::SetNextWindowSize(ImVec2(bw, bh));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * z, 14 * z));
    ImGui::Begin("##sign", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // .sign-board: planks, light top-left / dark bottom-right border, black rim
    dl->AddRectFilled(p0 - ImVec2(2, 2) * z, p1 + ImVec2(2, 2) * z, col::black);
    dl->AddRectFilled(p0, p1, col::rgb(0x8a6237));
    drawTiled(dl, "planks", p0, p1, 64 * z, col::white);
    bevel(dl, p0, p1, col::rgb(0xb78b55), col::rgb(0x3a2512), 3 * z);
    float sc = 22.f / fontPx(F_H3);
    ImVec2 ts = textSize(F_H3, "Write on the sign", 0, sc);
    ImVec2 tp(p0.x + (bw - ts.x) * 0.5f, p0.y + 14 * z);
    text(dl, F_H3, tp + ImVec2(2, 2) * z, col::rgb(0x2a1a0a), "Write on the sign", 0, sc);
    text(dl, F_H3, tp, col::rgb(0xfff3d6), "Write on the sign", 0, sc);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 16 * z, tp.y + titleH + 4 * z));
    ImGui::PushFont(font(F_BTN));
    ImGui::PushStyleColor(ImGuiCol_Text, col::rgb(0x1e140c));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col::rgb(0xfff0d2, 140)); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col::rgb(0xfff4dc, 170)); ImGui::PushStyleColor(ImGuiCol_FrameBgActive, col::rgb(0xfff4dc, 204));
    ImGui::PushStyleColor(ImGuiCol_Border, col::rgb(0x3a2512, 153));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2 * z); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2 * z);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * z, std::max(2.f, (ih - ImGui::GetFontSize()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    int submitted = -1;
    for (int i = 0; i < 4; i++) {
      ImGui::PushID(i);
      if (focusLine == i) { ImGui::SetKeyboardFocusHere(); focusLine = -1; }
      ImGui::SetNextItemWidth(bw - 32 * z);
      // centred text like the original's text-align: center
      if (ImGui::InputText("##l", &lines[i], ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
                           [](ImGuiInputTextCallbackData* d) { return d->EventChar < 32 ? 1 : 0; })) submitted = i;
      if (lines[i].size() > 16) { lines[i].resize(16); while (!lines[i].empty() && ((u8)lines[i].back() & 0xC0) == 0x80) lines[i].pop_back(); }
      if (ImGui::IsItemFocused() && navVisible()) { ImVec2 a0 = ImGui::GetItemRectMin(), a1 = ImGui::GetItemRectMax(); dl->AddRect(a0 - ImVec2(2, 2) * z, a1 + ImVec2(2, 2) * z, col::gold, 0, 0, 2 * z); }
      ImGui::PopID();
    }
    ImGui::PopStyleVar(4); ImGui::PopStyleColor(5); ImGui::PopFont();
    ImGui::Dummy(ImVec2(1, 2 * z));
    float bwid = textSize(F_BODY, "Done").x + 28 * z;
    ImGui::SetCursorScreenPos(ImVec2(p0.x + (bw - bwid) * 0.5f, ImGui::GetCursorScreenPos().y));
    if (padOpen) { ImGui::SetKeyboardFocusHere(); padOpen = false; }
    bool done = button("Done", BTN_PRIMARY, true, bwid);
    ImGui::End();
    ImGui::PopStyleVar();
    if (submitted >= 0 && submitted < 3) focusLine = submitted + 1;
    if (done || submitted == 3) uiDefer([this](App& app) { if (app.topScreen() == this) app.popScreen(); });
  }
};
}

std::unique_ptr<Screen> makeSignScreen(ivec3 at) { return std::make_unique<SignScreen>(at); }

} // namespace dq
