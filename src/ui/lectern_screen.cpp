// ui/lectern_screen.cpp — a book on a lectern, read where it lies (after Cinematic Interactions' LecternStation): your
// eyes go to it, its pages turn over on the lectern (render/lectern_layer.cpp draws the book and the page turning), the
// page you leave it at stays for the next reader. Turn with a click on a page, A / D, the arrows, Q / E, LB / RB, the
// wheel; take the book; E, Esc or B to step back. Old books of the world, written books and books and quills alike.
#include "ui/book_ui.h"
#include "ui/book_pages.h"
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "client/play.h"
#include "sim/player.h"
#include "audio/audio.h"
#include <imgui.h>

namespace dq {
int giveItem(Level& lv, Player& p, ItemStack s);   // sim/gameplay.h (its header clashes with the client headers)

namespace {
class LecternScreen : public Screen {
public:
  explicit LecternScreen(ivec3 at) : at(at) {}
  ivec3 at;
  BookText book;
  int spread = 0;
  float lookT = 0; float yaw0 = 0, pitch0 = 0;
  const char* id() const override { return "lectern_scene"; }
  bool pausesGame() const override { return false; }
  bool showsWorld() const override { return true; }
  bool wantsMouse() const override { return true; }

  json* entity(App& a) {
    if (!a.session) return nullptr;
    World& w = a.session->world();
    if (!reg().block(w.getBlock(at.x, at.y, at.z)).has("lectern")) return nullptr;
    json* e = w.blockEntity(at.x, at.y, at.z);
    BookText t;
    return e && bookTextOfLectern(*e, t) ? e : nullptr;
  }
  void onOpen(App& a) override {
    if (json* e = entity(a)) { bookTextOfLectern(*e, book); spread = std::clamp(e->value("page", 0), 0, bookSpreadCount(book) - 1); }
    if (a.session && a.session->player) { yaw0 = a.session->player->yaw; pitch0 = a.session->player->pitch; }
    if (a.audio) a.audio->play("map_paper");
  }
  void turn(App& a, int d) {
    const int n = std::clamp(spread + d, 0, bookSpreadCount(book) - 1);
    if (n == spread) return;
    spread = n;
    if (json* e = entity(a)) { (*e)["page"] = spread; a.session->world().blockEntityChanged(at.x, at.y, at.z); }   // (the lectern shows it: everyone sees it turn)
    if (a.audio) a.audio->play("map_paper");
  }
  void take(App& a) {
    json* e = entity(a);
    if (!e || !a.session || !a.session->player) return;
    const ItemStack s = e->contains("stack") ? ItemStack::fromJson((*e)["stack"]) : ItemStack("lore_" + e->value("lore", std::string()), 1);
    giveItem(*a.session->level, *a.session->player, s);
    a.session->world().setBlockEntity(at.x, at.y, at.z, nullptr);
    uiDefer([this](App& app) { if (app.topScreen() == this) app.popScreen(); });
  }
  void update(App& a, double dt) override {
    json* e = entity(a);
    if (!e || !a.session || !a.session->player) { a.popScreen(); return; }
    // (someone else turned it)
    if (const int pg = std::clamp(e->value("page", 0), 0, bookSpreadCount(book) - 1); pg != spread) spread = pg;
    Player& p = *a.session->player;
    // your eyes go down to the book (and stay there while you read)
    const vec3 eye = p.pos + vec3(0, p.eye, 0), tgt = vec3(at) + vec3(0.5f, 0.98f, 0.5f), d = tgt - eye;
    const float wy = std::atan2(-d.x, -d.z), wp = std::atan2(d.y, std::hypot(d.x, d.z));
    lookT = std::min(1.f, lookT + (float)dt * 3.5f);
    const float k = lookT * lookT * (3 - 2 * lookT);
    float dy = wy - yaw0; while (dy > PI) dy -= TAU; while (dy < -PI) dy += TAU;
    p.yaw = yaw0 + dy * k; p.pitch = pitch0 + (wp - pitch0) * k;
    cameraLeanTo(*a.session, glm::dvec3(tgt), 0.62f);   // and you lean in over it, so its writing can be read (client/play.cpp)
    Input& in = a.input;
    const bool walk = in.down(Act::Forward) || in.down(Act::Back) || (in.down(Act::Jump) && !in.usingPad());   // (A / D turn the pages here: only forward, back or a jump walks off)
    if (walk || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f || in.pressed(Act::Inventory)) { a.popScreen(); return; }
    if (in.pressed(Act::MenuTabRight) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_D, false) || ImGui::IsKeyPressed(ImGuiKey_E, false)) turn(a, 1);
    if (in.pressed(Act::MenuTabLeft) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_A, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false)) turn(a, -1);
    if (in.pressed(Act::MenuX)) take(a);
    if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) turn(a, wh < 0 ? 1 : -1);
  }
  void draw(App& a) override {
    const float W = (float)a.width, H = (float)a.height, z = ui::Z();
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && ImGui::IsMouseClicked(0)) turn(a, ImGui::GetIO().MousePos.x > W * 0.5f ? 1 : -1);   // (a click on a page)
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H - 18 * z), ImGuiCond_Always, ImVec2(0.5f, 1));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("##lectern", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted(book.title.empty() ? "A book" : book.title.c_str());
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) turn(a, -1);
    ImGui::SameLine(); ImGui::Text("%d / %d", spread + 1, bookSpreadCount(book)); ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) turn(a, 1);
    ImGui::SameLine();
    if (ImGui::Button("Take the book")) take(a);
    ImGui::SameLine();
    if (ImGui::Button("Step back")) uiDefer([this](App& app) { if (app.topScreen() == this) app.popScreen(); });
    ImGui::TextDisabled("Click a page, A / D or the arrows to turn \xC2\xB7 X (pad) takes it \xC2\xB7 E, Esc or B to step back");
    ImGui::End();
  }
};
}

std::unique_ptr<Screen> makeLecternScreen(ivec3 at) { return std::make_unique<LecternScreen>(at); }

} // namespace dq
