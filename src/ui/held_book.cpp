// ui/held_book.cpp — see held_book.h
#include "ui/held_book.h"
#include "ui/book_pages.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/player.h"
#include "sim/level.h"
#include "audio/audio.h"
#include <SDL.h>
#include <imgui.h>

namespace dq {
namespace {
struct View { bool on = false; BookText b; int spread = 0, prev = 0, caret = -1; double turnAt = -10; int dir = 1; } g_view;
double nowS() { return SDL_GetTicks() / 1000.0; }

class HeldBookScreen : public Screen {
public:
  explicit HeldBookScreen(int slot) : slot(slot) {}
  const char* id() const override { return "held_book"; }
  bool pausesGame() const override { return false; }
  bool showsWorld() const override { return true; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override { g_view = View{}; g_view.on = true; if (a.audio) a.audio->play("map_paper"); reload(a); }
  void onClose(App& a) override { g_view.on = false; flushEdit(a); }
  bool onBack(App&) override { if (signing) { signing = false; return true; } return false; }

  ItemStack* stack(App& a) {
    if (!a.session || !a.session->player) return nullptr;
    Player& p = *a.session->player;
    if (slot < 0 || slot >= (int)p.inv.size()) return nullptr;
    ItemStack& s = p.inv[slot];
    BookText t;
    return bookTextOf(s, t) ? &s : nullptr;
  }
  void reload(App& a) {
    ItemStack* s = stack(a);
    if (!s) return;
    bookTextOf(*s, g_view.b);
    if (g_view.b.writable) g_view.caret = std::max(0, (int)g_view.b.pages.size() - 1);   // (the pen at the last page)
    g_view.spread = std::clamp(g_view.spread, 0, bookSpreadCount(g_view.b) - 1);
  }
  void turn(App& a, int d) {
    const int n = std::clamp(g_view.spread + d, 0, bookSpreadCount(g_view.b) - 1);
    if (n == g_view.spread) return;
    g_view.prev = g_view.spread; g_view.spread = n; g_view.turnAt = nowS(); g_view.dir = d > 0 ? 1 : -1;
    if (g_view.b.writable) g_view.caret = std::min(n * 2 + 1, (int)g_view.b.pages.size() - 1);   // (the pen to the last page of these two)
    if (a.audio) a.audio->play("map_paper");
  }
  // writing: the pages back into the book and quill
  void save(App& a) {
    ItemStack* s = stack(a);
    if (!s) return;
    if (!s->tag.is_object()) s->tag = json::object();
    json pg = json::array(); for (auto& t : g_view.b.pages) pg.push_back(t);
    s->tag["pages"] = pg;
    editDue = true; flushEdit(a, false);
  }
  // a guest's writing goes to the host as it is written (net/server_host.cpp "bookEdit"): the host's copy of your bag is
  // the one that lasts, and its own changes to your bag (a thing picked up) would otherwise bring the old pages back
  void flushEdit(App& a, bool now = true, const std::string& signAs = "") {
    if (!a.session || !a.session->player || !a.session->level || !a.session->level->net) { editDue = false; return; }
    if (!editDue && signAs.empty()) return;
    if (!now && nowS() - editSentAt < 0.25) return;   // (at most four a second while typing; update() sends the rest)
    json pg = json::array(); for (auto& t : g_view.b.pages) pg.push_back(t);
    json args = {{"slot", slot}, {"pages", pg}};
    if (!signAs.empty()) args["sign"] = signAs;
    a.session->level->net->action(*a.session->player, "bookEdit", args);
    editDue = false; editSentAt = nowS();
  }
  void write(App& a) {
    Input& in = a.input;
    if (signing || g_view.caret < 0) return;
    std::string& t = g_view.b.pages[g_view.caret];
    bool changed = false;
    for (char c : in.typed) if ((unsigned char)c >= 32 || c == '\t') { t += c == '\t' ? ' ' : c; changed = true; }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, true)) { t += '\n'; changed = true; }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
      if (!t.empty()) { do t.pop_back(); while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80); changed = true; }   // (a whole letter)
      else if (g_view.caret > 0 && g_view.b.pages.size() > 1 && g_view.caret == (int)g_view.b.pages.size() - 1) {   // an empty last page goes
        g_view.b.pages.pop_back(); g_view.caret--; changed = true;
        if (g_view.caret / 2 != g_view.spread) turn(a, -1);
      }
    }
    if ((int)t.size() > BOOK_PAGE_CHARS) {   // the page is full: on to a new one
      std::string carry;
      const size_t cut = t.find_last_of(' ', BOOK_PAGE_CHARS);
      if (cut != std::string::npos && cut > BOOK_PAGE_CHARS / 2) { carry = t.substr(cut + 1); t.resize(cut); } else { carry = t.substr(BOOK_PAGE_CHARS); t.resize(BOOK_PAGE_CHARS); }
      if (g_view.caret + 1 >= (int)g_view.b.pages.size()) g_view.b.pages.push_back("");
      g_view.caret++;
      g_view.b.pages[g_view.caret] = carry + g_view.b.pages[g_view.caret];
      if (g_view.caret / 2 != g_view.spread) turn(a, 1);
    }
    if (changed) save(a);
  }
  void sign(App& a) {
    ItemStack* s = stack(a);
    if (!s || !a.session || !a.session->player) return;
    std::string title = signTitle; while (!title.empty() && title.back() == ' ') title.pop_back();
    if (title.empty()) return;
    flushEdit(a, true, title);   // (a guest: the host signs its copy too, with the name it knows you by)
    json pg = json::array(); for (auto& t : g_view.b.pages) pg.push_back(t);
    ItemStack w("written_book", 1); w.tag = json{{"title", title}, {"author", a.session->player->name}, {"pages", pg}};
    *s = w;
    if (a.audio) a.audio->play("levelup");
    signing = false;
    g_view.spread = 0; g_view.caret = -1;
    reload(a);
  }

  void update(App& a, double) override {
    if (!stack(a)) { a.popScreen(); return; }
    if (a.session && a.session->player) a.session->player->selected = slot < 9 ? slot : a.session->player->selected;
    Input& in = a.input;
    if (g_view.b.writable && !signing) { write(a); a.session->uiWantsText = true; }   // (the keys write: they don't walk you about)
    if (editDue) flushEdit(a, false);
    const bool typing = g_view.b.writable || signing;
    if (in.pressed(Act::MenuTabRight) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false) || (!typing && (ImGui::IsKeyPressed(ImGuiKey_D, false) || ImGui::IsKeyPressed(ImGuiKey_E, false)))) turn(a, 1);
    if (in.pressed(Act::MenuTabLeft) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false) || (!typing && (ImGui::IsKeyPressed(ImGuiKey_A, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false)))) turn(a, -1);
    if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) turn(a, wh < 0 ? 1 : -1);
    if (!typing && in.pressed(Act::Inventory)) { a.popScreen(); return; }
  }
  void draw(App& a) override {
    const float W = (float)a.width, H = (float)a.height, z = ui::Z();
    // a click on a page turns it (the left page back, the right on); in a book and quill, a click picks the page to write on
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && ImGui::IsMouseClicked(0) && !signing) {
      const ImVec2 m = ImGui::GetIO().MousePos;
      const int side = m.x > W * 0.5f ? 1 : 0;
      if (g_view.b.writable) {
        const int pg = g_view.spread * 2 + side;
        if (pg < (int)g_view.b.pages.size()) g_view.caret = pg;
        else if (pg == (int)g_view.b.pages.size() && g_view.b.pages.back().size() > 0) { g_view.b.pages.push_back(""); g_view.caret = pg; save(a); }
      } else turn(a, side ? 1 : -1);
    }
    // the foot: where you are, and what you can do
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H - 18 * z), ImGuiCond_Always, ImVec2(0.5f, 1));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("##heldbook", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    const int n = bookSpreadCount(g_view.b);
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) turn(a, -1);
    ImGui::SameLine(); ImGui::Text("%d / %d", g_view.spread + 1, n); ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) turn(a, 1);
    if (g_view.b.writable) {
      ImGui::SameLine();
      if (!signing) {
        if (ImGui::Button("New page")) { g_view.b.pages.push_back(""); g_view.caret = (int)g_view.b.pages.size() - 1; save(a); if (g_view.caret / 2 != g_view.spread) turn(a, g_view.caret / 2 - g_view.spread); }
        ImGui::SameLine();
        if (ImGui::Button("Sign...")) { signing = true; signTitle.clear(); }
        ImGui::TextDisabled("Type to write on page %d \xC2\xB7 Enter: a new line \xC2\xB7 click a page to write there \xC2\xB7 arrows / PgUp PgDn: turn", g_view.caret + 1);
      } else {
        ImGui::TextUnformatted("Title:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(260 * z);
        if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        const bool enter = ui::inputText("##title", &signTitle, "A title for it", 32, 260 * z, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Sign and close") || enter) sign(a);
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) signing = false;
        ImGui::TextDisabled("Signed, it can't be written in again");
      }
    } else ImGui::TextDisabled("Click a page, A / D or the arrows to turn \xC2\xB7 E or Esc to close");
    ImGui::SameLine();
    if (ImGui::Button("Done")) { ImGui::End(); a.popScreen(); return; }
    ImGui::End();
  }
private:
  int slot;
  bool signing = false;
  std::string signTitle;
  bool editDue = false; double editSentAt = -10;   // (a guest's pages not yet sent to the host)
};
}  // namespace

bool heldBookView(JournalPic& cur, JournalPic& prev, float& leaf, int& dir) {
  if (!g_view.on) return false;
  const CanvasPic c = paintBookSpread("held/" + std::to_string(g_view.spread), g_view.b, g_view.spread, g_view.caret);
  cur = JournalPic{}; cur.tex = c.tex; cur.texRect = vec4(0, 1, 1, 0);
  const double t = nowS() - g_view.turnAt;
  leaf = t < 0.6 ? (float)(t / 0.6) : -1.f;
  dir = g_view.dir;
  prev = JournalPic{};
  if (leaf >= 0) { const CanvasPic p = paintBookSpread("held/" + std::to_string(g_view.prev), g_view.b, g_view.prev, -1); prev.tex = p.tex; prev.texRect = vec4(0, 1, 1, 0); }
  return cur.tex != 0;
}

bool heldBookOpen() { return g_view.on; }
std::unique_ptr<Screen> makeHeldBookScreen(int slot) { return std::make_unique<HeldBookScreen>(slot); }

} // namespace dq
