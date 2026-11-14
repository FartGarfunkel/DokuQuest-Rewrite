// ui/cinematic_slots.cpp — the trommel and the machines (millstone, sawmill, crusher) worked by hand, as the furnace is
// (ui/cinematic_table.cpp CinematicFurnaceScreen): you bend to it, put what's in your hand into its hopper (its mouth),
// fuel into the trommel's firebox, and take what's done back out; your bag in the journal in your off hand. What the
// trommel sifts out flies from its side (sim/trommel.cpp).
#include "ui/cinematic.h"
#include "ui/cinematic_internal.h"
#include "ui/book_ui.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "client/app.h"
#include "client/session.h"
#include "render/camera.h"
#include "sim/slot_work.h"
#include "sim/player.h"
#include "core/registry.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace dq {
using namespace cine;

namespace {
class CinematicSlotsScreen : public Screen {
public:
  explicit CinematicSlotsScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_slots"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    rig = slotRigAt(a.session->world(), at);
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    // the trommel bent to at its front, as a furnace (its firebox low in it); a machine leant over, its mouth on top
    S.at = at; S.t = 0; S.phase = 1; S.kind = rig == SlotRig::Trommel ? 4 : 14;
    if (rig == SlotRig::Trommel) {
      static const vec3 FRONT[4] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};
      S.front = FRONT[a.session->world().getMeta(at.x, at.y, at.z) & 3];
    }
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    S.front = vec3(0);
  }
  void onClose(App&) override { cinematicSetAimPoint(false); Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool walk = sceneWalkOff(in);
    if (slotRigAt(w, at) == SlotRig::None || walk || p.dead || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f) { a.popScreen(); return; }
    if (in.pressed(Act::Inventory) && !in.pressed(Act::MenuY)) showBag = !showBag;
    for (int k = 0; k < 9; k++) if (in.pressed((Act)((int)Act::Hot1 + k))) p.selected = k;
    if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && !(ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook())) p.selected = ((p.selected - (wh > 0 ? 1 : -1)) % 9 + 9) % 9;
    if (bagPadUpdate(in, p, bag)) return;
    const ivec2 nav = in.navStep();
    if (nav.y || nav.x) { pad = true; spot = std::clamp((spot < 0 ? 0 : spot) + (nav.y + nav.x > 0 ? 1 : -1), 0, 1); }
    if (pad && spot >= 0 && in.pressed(Act::MenuOk)) act(a, spot, false);
    if (pad && spot >= 0 && in.pressed(Act::MenuX)) act(a, spot, true);
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world(); Camera& cam = s->frame.cam;
    const float W = (float)a.width, H = (float)a.height, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook());
    if (mouseTookOver(a)) pad = false;
    auto project = [&](vec3 wp, ImVec2& out) { vec4 q = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1); if (q.w <= 0.01f) return false; out = ImVec2((q.x / q.w * 0.5f + 0.5f) * W, (1 - (q.y / q.w * 0.5f + 0.5f)) * H); return true; };
    const vec3 side(std::sin(scene().viewYaw), 0, std::cos(scene().viewYaw));   // from it toward you
    const vec3 face = vec3(at) + vec3(0.5f) + side * 0.5f;
    const bool tr = rig == SlotRig::Trommel;
    // the spots: its hopper (mouth) on top; the trommel's firebox low in its front, a machine's chute at its front
    const vec3 SPOT[2] = {tr ? face + vec3(0, 0.42f, 0) - side * 0.15f : vec3(at) + vec3(0.5f, 1.02f, 0.5f), face + vec3(0, tr ? -0.28f : -0.1f, 0)};   // (the trommel's hopper at its top edge: bent low, its top is out of sight)
    const std::vector<ItemStack> v = slotItems(w, at);
    ItemStack shown[2];
    if (tr) { for (int i = 3; i >= 0; i--) if (!v[i].empty()) { shown[0] = v[i]; break; } shown[1] = v[4]; }
    else { shown[0] = v[0]; shown[1] = v[1]; }
    int inN = 0; if (tr) for (int i = 0; i < 4; i++) inN += v[i].n;
    const ImVec2 mp = ImGui::GetIO().MousePos;
    int hover = pad ? spot : -1; float best = 50 * z;
    for (int k = 0; k < 2; k++) {
      ImVec2 sp; if (!project(SPOT[k], sp)) continue;
      if (!pad && !overUi) { const float d = std::hypot(mp.x - sp.x, mp.y - sp.y); if (d < best) { best = d; hover = k; } }
      const float is = 34 * z;
      dl->AddRectFilled(ImVec2(sp.x - is / 2 - 3, sp.y - is / 2 - 3), ImVec2(sp.x + is / 2 + 3, sp.y + is / 2 + 3), IM_COL32(20, 16, 12, 150), 4 * z);
      if (!shown[k].empty()) { ItemStack d = shown[k]; if (tr && k == 0) d.n = inN; ui::drawItem(dl, d, ImVec2(sp.x - is / 2, sp.y - is / 2), is); }
      if (k == hover && scene().phase == 2) dl->AddRect(ImVec2(sp.x - 22 * z, sp.y - 22 * z), ImVec2(sp.x + 22 * z, sp.y + 22 * z), IM_COL32(255, 236, 160, 230), 4 * z, 0, 2 * z);
    }
    if (scene().phase == 2 && hover >= 0) cinematicSetAimPoint(true, SPOT[hover] + side * 0.05f); else cinematicSetAimPoint(false);   // (the hand goes to it)
    {   // the gauges on its front: the work (orange) over the fire (red; a trommel's)
      ImVec2 g0, g1;
      const vec3 rgt(side.z, 0, -side.x);
      if (project(face + vec3(0, 0.15f, 0) - rgt * 0.3f, g0) && project(face + vec3(0, 0.15f, 0) + rgt * 0.3f, g1)) {
        const float ck = slotProgress(w, at);
        dl->AddLine(g0, g1, IM_COL32(40, 30, 20, 200), 4 * z);
        dl->AddLine(g0, ImVec2(g0.x + (g1.x - g0.x) * ck, g0.y + (g1.y - g0.y) * ck), IM_COL32(255, 170, 60, 255), 4 * z);
        if (tr) {
          const float fu = slotFuel(w, at);
          dl->AddLine(ImVec2(g0.x, g0.y + 7 * z), ImVec2(g1.x, g1.y + 7 * z), IM_COL32(40, 30, 20, 200), 4 * z);
          dl->AddLine(ImVec2(g0.x, g0.y + 7 * z), ImVec2(g0.x + (g1.x - g0.x) * fu, g0.y + 7 * z + (g1.y - g0.y) * fu), IM_COL32(255, 80, 40, 255), 4 * z);
        }
      }
    }
    if (!pad && !overUi && hover >= 0 && scene().phase == 2) {
      if (ImGui::IsMouseClicked(0)) act(a, hover, handFree(p) || (!tr && hover == 1));
      else if (ImGui::IsMouseClicked(1)) act(a, hover, true);
    }
    spot = hover >= 0 ? hover : spot;
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    const std::string name = reg().block(w.getBlock(at.x, at.y, at.z)).name;
    static const char* TNAMES[2] = {"the hopper: what sifts (four kinds)", "the firebox: fuel"};
    static const char* MNAMES[2] = {"its mouth: what goes through", "the chute: what's done (take it)"};
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_slots", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    if (tr) ImGui::Text("%s   \xC2\xB7   In hand: %s   \xC2\xB7   Sifting: %d   \xC2\xB7   Fuel: %s", name.c_str(), nameOf(p.held()).c_str(), inN, nameOf(v[4]).c_str());
    else ImGui::Text("%s   \xC2\xB7   In hand: %s   \xC2\xB7   In it: %s   \xC2\xB7   Done: %s", name.c_str(), nameOf(p.held()).c_str(), nameOf(v[0]).c_str(), nameOf(v[1]).c_str());
    if (hover >= 0) ImGui::TextDisabled("%s", (tr ? TNAMES : MNAMES)[hover]);
    if (tr) ImGui::TextDisabled("What it sifts out falls from its side (into a chest standing there)");
    else ImGui::TextDisabled("It works while it's turned (a windmill, a water wheel, a crank)");
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back"
                      : pad ? "D-pad the spots \xC2\xB7 A put in \xC2\xB7 X take out \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : "Left click a spot: put in what's in hand (empty hand: take out) \xC2\xB7 Right click: take out");
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; SlotRig rig = SlotRig::None; int spot = -1; bool pad = false, showBag = true;
  BagPad bag;
  void act(App& a, int k, bool take) {
    GameSession* s = a.session.get();
    if (!s || scene().phase != 2) return;
    cinematicTap();
    if (take) slotTake(*s->level, *s->player, s->world(), at, k);
    else slotPut(*s->level, *s->player, s->world(), at, k, s->player->held());
  }
};
}  // namespace

std::unique_ptr<Screen> makeCinematicSlotsScreen(ivec3 at) { return std::make_unique<CinematicSlotsScreen>(at); }

}  // namespace dq
