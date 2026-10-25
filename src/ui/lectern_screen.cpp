// ui/lectern_screen.cpp — an old book on a lectern (game/structures.js BookUI): its title, the page you leave it
// on (which is the page the lectern shows, render/lectern_layer.cpp), turn pages with the arrows / A D / the
// d-pad, take the book. Controller: d-pad or LB/RB turn, A presses the focused button, B closes.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "assets/lore.h"
#include "audio/audio.h"
#include <imgui.h>

namespace dq {
int giveItem(Level& lv, Player& p, ItemStack s);   // sim/gameplay.h (its header clashes with the client headers)
using namespace ui;

namespace {
class LecternScreen : public Screen {
public:
  explicit LecternScreen(ivec3 at) : at(at) {}
  ivec3 at;
  std::string key;
  const LoreBook* book = nullptr;
  int page = 0;
  const char* id() const override { return "lectern"; }
  bool pausesGame() const override { return false; }

  json* entity(App& a) {
    if (!a.session) return nullptr;
    World& w = a.session->world();
    if (!reg().block(w.getBlock(at.x, at.y, at.z)).has("lectern")) return nullptr;
    json* e = w.blockEntity(at.x, at.y, at.z);
    return e && e->is_object() && e->value("book", false) && e->contains("lore") ? e : nullptr;
  }
  void onOpen(App& a) override {
    if (json* e = entity(a)) { key = (*e).value("lore", ""); book = loreBook(key); page = (*e).value("page", 0); }
    if (book) page = std::clamp(page, 0, (int)book->pages.size() - 1);
    if (a.audio) a.audio->play("map_paper");
  }
  void turn(App& a, int d) {
    if (!book) return;
    int n = page + d;
    if (n < 0 || n >= (int)book->pages.size()) return;
    page = n;
    if (json* e = entity(a)) { (*e)["page"] = page; a.session->world().blockEntityChanged(at.x, at.y, at.z); }   // the lectern shows this page
    if (a.audio) a.audio->play("map_paper");
  }
  void take(App& a) {
    json* e = entity(a);
    if (!e || !a.session || !a.session->player) return;
    giveItem(*a.session->level, *a.session->player, ItemStack("lore_" + key, 1));
    a.session->world().setBlockEntity(at.x, at.y, at.z, nullptr);
    uiDefer([this](App& app) { if (app.topScreen() == this) app.popScreen(); });
  }
  void update(App& a, double) override {
    if (!entity(a) || !book) a.popScreen();   // the book was taken, or the lectern broke
  }
  void draw(App& a) override {
    if (!book) return;
    float z = Z(), W = (float)a.width, H = (float)a.height;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 3, 2, 150));
    float bw = std::min(520.f * z, W - 32 * z), bh = std::min(430.f * z, H - 32 * z);
    ImVec2 p0((W - bw) * 0.5f, (H - bh) * 0.5f), p1 = p0 + ImVec2(bw, bh);
    ImGui::SetNextWindowPos(p0); ImGui::SetNextWindowSize(ImVec2(bw, bh));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26 * z, 20 * z));
    ImGui::Begin("##lectern", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // the open book: parchment pages in a leather binding, black rim
    dl->AddRectFilled(p0 - ImVec2(3, 3) * z, p1 + ImVec2(3, 3) * z, col::black);
    dl->AddRectFilled(p0, p1, col::rgb(0xefe2c0));
    bevel(dl, p0, p1, col::rgb(0xfaf0d6), col::rgb(0x8a6a3a), 3 * z);
    float footH = textSize(F_BODY, "Ag").y + 16 * z;
    float sc = 24.f / fontPx(F_H3);
    ImVec2 ts = textSize(F_H3, book->title.c_str(), bw - 52 * z, sc);
    text(dl, F_H3, ImVec2(p0.x + (bw - ts.x) * 0.5f, p0.y + 18 * z), col::rgb(0x3b2a18), book->title.c_str(), bw - 52 * z, sc);
    float ty = p0.y + 18 * z + ts.y + 8 * z;
    dl->AddLine(ImVec2(p0.x + 40 * z, ty), ImVec2(p1.x - 40 * z, ty), col::rgb(0x8a6a3a, 160), 2 * z);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 26 * z, ty + 10 * z));
    ImGui::BeginChild("##pg", ImVec2(bw - 52 * z, p1.y - footH - 14 * z - (ty + 10 * z)), false, ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(font(F_BTN));
    ImGui::PushStyleColor(ImGuiCol_Text, col::rgb(0x3b2a18));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(book->pages[page].c_str());
    ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::PopFont();
    ImGui::EndChild();
    // the foot: ‹  n / N  ›    Take the book    Close
    int n = (int)book->pages.size();
    float by = p1.y - footH - 6 * z;
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 26 * z, by));
    bool prev = button("<", BTN_NORMAL, true, 36 * z, page == 0);
    ImGui::SameLine();
    char buf[32]; std::snprintf(buf, sizeof buf, "%d / %d", page + 1, n);
    ImVec2 ps = textSize(F_BODY, buf);
    ImGui::Dummy(ImVec2(ps.x + 20 * z, 1)); text(dl, F_BODY, ImVec2(ImGui::GetItemRectMin().x + 10 * z, by + 6 * z), col::rgb(0x3b2a18), buf);
    ImGui::SameLine();
    bool next = button(">", BTN_NORMAL, true, 36 * z, page >= n - 1);
    float wTake = textSize(F_BODY, "Take the book").x + 28 * z, wClose = textSize(F_BODY, "Close").x + 28 * z;
    ImGui::SameLine(bw - 26 * z - wTake - 8 * z - wClose);
    bool tk = button("Take the book", BTN_PRIMARY, true, wTake);
    ImGui::SameLine();
    bool cl = button("Close", BTN_NORMAL, true, wClose);
    ImGui::End();
    ImGui::PopStyleVar();
    if (prev) turn(a, -1);
    if (next) turn(a, 1);
    if (tk) take(a);
    if (cl) uiDefer([this](App& app) { if (app.topScreen() == this) app.popScreen(); });
    // keys / pad shoulders turn the page; E closes
    if (!prev && !next) {
      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_D, false) || a.input.pressed(Act::MenuTabRight)) turn(a, 1);
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_A, false) || a.input.pressed(Act::MenuTabLeft)) turn(a, -1);
    }
    if (a.input.pressed(Act::Inventory)) uiDefer([this](App& app) { if (app.topScreen() == this) app.popScreen(); });
  }
};
}

std::unique_ptr<Screen> makeLecternScreen(ivec3 at) { return std::make_unique<LecternScreen>(at); }

} // namespace dq
