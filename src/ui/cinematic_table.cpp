// ui/cinematic_table.cpp — see cinematic.h. After Cinematic Interactions (MIT, Southware / XiaoCraft): its CameraRig's
// three phases (the approach gliding from the eyes to the station's lean, the work with the camera breathing, the leave
// gliding back to the eyes), its crafting table worked on the table's own top. Ours sets the pieces on the in-world grid
// of sim/worktable.h (one at a time, read from where you stand, synced like any block entity).
#include "ui/cinematic.h"
#include "ui/book_ui.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "client/app.h"
#include "client/session.h"
#include "render/camera.h"
#include "sim/worktable.h"
#include "sim/anvil_work.h"
#include "sim/ench_work.h"
#include "sim/brewing.h"
#include "sim/furnace_work.h"
#include "sim/chest_work.h"
#include "sim/station_work.h"
#include "sim/stations.h"
#include "render/chest_layer.h"
#include "audio/audio.h"
#include "sim/blockinfo.h"
#include "ui/magic_screens.h"
#include "sim/gameplay.h"
#include "sim/crafting.h"
#include "sim/player.h"
#include "core/registry.h"
#include <imgui.h>
#include <cmath>

namespace dq {
namespace {

constexpr double INTRO = 0.6, OUTRO = 0.45;

struct Scene {
  int phase = 0;   // 0 off, 1 the approach, 2 at work, 3 leaving
  double t = 0, clock = 0;
  ivec3 at{0};
  glm::dvec3 viewPos{0}; float viewYaw = 0, viewPitch = 0;   // the lean over the table
  glm::dvec3 lastPos{0}; float lastYaw = 0, lastPitch = 0;    // the pose shown last frame (the leave sets off from it)
  int kind = 0;    // 0 the crafting table, 1 an anvil, 2 the enchanting table, 3 a brewing stand, 4 a furnace
  float fov = 66, roll = 0;
  vec3 rest[2]{};  // where the hands lie at rest (right, left)
  vec3 front{0};   // a station with a front of its own (a furnace): lean in there, wherever you stand
  vec3 viewOff{0};     // the eye's offset from bending (cinematicViewOffset)
  float bend = 0;      // seamless first person: how far bent toward the work (0..1, eased)
  bool lean = false;   // the camera leans over the station (Options "Lean the camera over stations"); off: seamless first person
};
Scene& scene() { static Scene s; return s; }
EnchantView g_ev; AnvilView g_av; bool g_evOn = false, g_avOn = false;   // (what the scenes show in the world: cinematicEnchantView / cinematicAnvilView)

double g_forgeAtShared();   // (the anvil's hammer: below)
double inOutCubic(double u) { u = clampv(u, 0.0, 1.0); return u < 0.5 ? 4 * u * u * u : 1 - std::pow(-2 * u + 2, 3) / 2; }
// walking off ends a scene: the walk keys or a jump, or on a pad the left stick pushed (A there is the scene's own
// button, the jump's too: pressing it at a station left the scene and jumped)
bool sceneWalkOff(const Input& in) {
  const bool keys = in.down(Act::Forward) || in.down(Act::Back) || in.down(Act::Left) || in.down(Act::Right);
  return keys || (in.down(Act::Jump) && !in.usingPad()) || (in.usingPad() && glm::length(in.moveAxis()) > 0.6f);
}
float wrapPi(float a) { while (a > PI) a -= TAU; while (a < -PI) a += TAU; return a; }

// where to lean from: over the side of the table you stand at (so the grid reads square to you), above its top
void leanPose(Scene& S, vec3 eye) {
  const vec3 c = vec3(S.at) + vec3(0.5f, 1.f, 0.5f);
  vec3 d = vec3(eye.x - c.x, 0, eye.z - c.z);
  d = std::fabs(d.x) > std::fabs(d.z) ? vec3(d.x > 0 ? 1.f : -1.f, 0, 0) : vec3(0, 0, d.z > 0 ? 1.f : -1.f);
  if (glm::length(S.front) > 0.5f) d = S.front;
  const vec3 right(d.z, 0, -d.x);   // to your right as you face the station
  auto aimAt = [&](vec3 from, vec3 to) {
    const vec3 v = to - from;
    S.viewPos = glm::dvec3(from); S.viewYaw = std::atan2(-v.x, -v.z); S.viewPitch = std::atan2(v.y, std::hypot(v.x, v.z));
  };
  S.fov = 66; S.roll = 0;
  const vec3 base = vec3(S.at) + vec3(0.5f, 0, 0.5f);
  // the leans and the hands at rest, each from Cinematic Interactions' station (lean(), rest(), leanFov())
  if (S.kind == 1) {          // AnvilStation: the eye 0.66 back and 0.7 up, looking just short of the middle, 0.12 above it
    aimAt(c + d * 0.66f + vec3(0, 0.7f, 0), c - d * 0.06f + vec3(0, 0.12f, 0));
    for (int k = 0; k < 2; k++) S.rest[k] = c + d * 0.24f + right * (k ? -0.3f : 0.3f) + vec3(0, 0.062f, 0);
  } else if (S.kind == 2) {   // EnchantStation: 1.15 back and 1 up, looking at the air half a block over the table; fov 68
    const vec3 top = base + vec3(0, 0.75f, 0);
    aimAt(top + d * 1.15f + vec3(0, 1.f, 0), top + vec3(0, 0.5f, 0));
    for (int k = 0; k < 2; k++) S.rest[k] = top + d * 0.36f + right * (k ? -0.24f : 0.24f) + vec3(0, 0.062f, 0);
    S.fov = 68;
  } else if (S.kind == 3) {   // BrewingStation: 1.2 back and 0.88 up, looking at its middle (0.46 up), rolled 2 degrees
    aimAt(base + d * 1.2f + vec3(0, 0.88f, 0), base + vec3(0, 0.46f, 0));
    for (int k = 0; k < 2; k++) S.rest[k] = base + d * 0.55f + right * (k ? -0.3f : 0.3f) + vec3(0, 0.2f, 0);
    S.roll = -2.f * PI / 180;
  } else if (S.kind == 4) {   // FurnaceStation: crouched before its front, looking into the mouth; the hands low before it
    const vec3 face = base + vec3(0, 0.5f, 0) + d * 0.5f;
    aimAt(face + d * 1.25f + vec3(0, 0.42f, 0), face + vec3(0, 0.02f, 0));   // (back enough to see its top too)
    for (int k = 0; k < 2; k++) S.rest[k] = face + d * 0.3f + right * (k ? -0.26f : 0.26f) + vec3(0, -0.3f, 0);
  } else if (S.kind == 6) {   // GrindstoneStation: 0.95 back from the wheel and 1 up, looking 0.32 over its middle; fov 64, rolled 1.5
    const vec3 wheel = base + vec3(0, 0.625f, 0);
    aimAt(wheel + d * 0.95f + vec3(0, 1.f, 0), wheel + vec3(0, 0.32f, 0));
    for (int k = 0; k < 2; k++) S.rest[k] = wheel + right * (k ? -0.31f : 0.31f) + d * 0.1f + vec3(0, 0.2495f, 0);
    S.fov = 64; S.roll = 1.5f * PI / 180;
  } else if (S.kind == 7) {   // StonecutterStation: 1 back from the table and 0.95 up, looking 0.25 over it just past the saw
    const vec3 top = base + vec3(0, 0.5625f, 0);
    aimAt(top + d * 1.f + vec3(0, 0.95f, 0), top - d * 0.05f + vec3(0, 0.25f, 0));
    for (int k = 0; k < 2; k++) S.rest[k] = top + d * 0.36f + right * (k ? -0.38f : 0.26f) + vec3(0, 0.062f, 0);
    S.roll = -1.5f * PI / 180;
  } else {                    // TableStation: LEAN_BACK 0.86, LEAN_UP 0.72, the pitch atan2(LEAN_UP, LEAN_BACK - LOOK_BACK)
    constexpr float LEAN_BACK = 0.86f, LEAN_UP = 0.72f, LOOK_BACK = -0.04f;
    S.viewPos = glm::dvec3(c + d * LEAN_BACK + vec3(0, LEAN_UP, 0));
    S.viewYaw = std::atan2(d.x, d.z);   // facing the table (lookVector: yaw 0 looks toward -z)
    S.viewPitch = -std::atan2(LEAN_UP, LEAN_BACK - LOOK_BACK);
    for (int k = 0; k < 2; k++) S.rest[k] = c + d * 0.4f + right * (k ? -0.24f : 0.24f) + vec3(0, 0.062f, 0);
  }
}

// the bag on a pad (a console's way): LB / RB go between the station and the bag, the d-pad walks the bag, A takes a
// stack into your hand, B (or LB / RB) back to the station
struct BagPad { bool on = false; int cur = 27; };
int bagSlotAt(int i) { return i < 27 ? 9 + i : i - 27; }   // the panel's order: the bag's rows, then the hotbar
bool bagPadUpdate(Input& in, Player& p, BagPad& b) {
  if (in.padButtonPressed(4) || in.padButtonPressed(5)) { b.on = !b.on; if (b.on) b.cur = 27 + p.selected; return true; }
  if (!b.on) return false;
  const ivec2 nav = in.navStep();
  if (nav != ivec2(0)) { int x = b.cur % 9 + nav.x, y = b.cur / 9 + nav.y; b.cur = std::clamp(y, 0, 3) * 9 + std::clamp(x, 0, 8); }
  if (in.pressed(Act::MenuOk)) { const int slot = bagSlotAt(b.cur); if (slot < 9) p.selected = slot; else std::swap(p.inv[slot], p.inv[p.selected]); }
  return true;
}
void drawBag(App& a, Player& p, int padCur);

class CinematicTableScreen : public Screen {
public:
  explicit CinematicTableScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_table"; }
  bool pausesGame() const override { return false; }
  bool showsWorld() const override { return true; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    const bool fresh = S.phase == 0;
    S.at = at; S.t = 0; S.phase = 1; S.kind = 0;
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    if (fresh) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    p.yaw = S.viewYaw;   // (turned to the table: the grid reads from where you stand)
  }
  void onClose(App&) override { leave(); }
  bool onBack(App& a) override { if (bag.on) { bag.on = false; return true; } leave(); return false; }   // (B in the bag: back to the table; else the camera glides back)
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player;
    World& w = s->world();
    const bool tableHere = reg().block(w.getBlock(at.x, at.y, at.z)).has("table");
    Input& in = a.input;
    const bool walk = sceneWalkOff(in);
    if (in.pressed(Act::Inventory) && !in.pressed(Act::MenuY)) showBag = !showBag;   // (your bag, beside the table: what to work with)
    if (!tableHere || walk || p.dead || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f) {
      DQ_LOG("crafting table scene: left (%s)", !tableHere ? "no table" : walk ? "walked off" : p.dead ? "died" : "too far");
      a.popScreen(); return;
    }
    p.yaw = scene().viewYaw;
    // the hotbar: number keys, the wheel, the shoulder buttons
    for (int k = 0; k < 9; k++) if (in.pressed((Act)((int)Act::Hot1 + k))) p.selected = k;
    if (bagPadUpdate(in, p, bag)) return;   // (LB / RB: the bag on a pad)
    if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && !(ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook())) p.selected = ((p.selected - (wh > 0 ? 1 : -1)) % 9 + 9) % 9;
    // the pad: the d-pad walks a square cursor (the mouse takes over again when it moves)
    const ivec2 nav = in.navStep();
    if (nav != ivec2(0)) { padMode = true; if (cursor < 0) cursor = 4; int cx = cursor % 3 + nav.x, cz = cursor / 3 + nav.y; cursor = std::clamp(cz, 0, 2) * 3 + std::clamp(cx, 0, 2); }
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) padMode = false;
    if (padMode && cursor >= 0) {
      if (in.pressed(Act::MenuOk)) act(a, cursor, false);
      if (in.pressed(Act::MenuX)) act(a, cursor, true);
    }
    if (in.pressed(Act::MenuY)) craft(a);
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player;
    World& w = s->world();
    Camera& cam = s->frame.cam;
    const float W = (float)a.width, H = (float)a.height, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    auto project = [&](vec3 wp, ImVec2& out) {
      vec4 c = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1);
      if (c.w <= 0.01f) return false;
      out = ImVec2((c.x / c.w * 0.5f + 0.5f) * W, (1 - (c.y / c.w * 0.5f + 0.5f)) * H); return true;
    };
    // the square under the mouse: the ray through it onto the table's top
    int hover = -1;
    const bool overUi = (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook());
    if (!padMode && !overUi && scene().phase == 2) {
      const ImVec2 m = ImGui::GetIO().MousePos;
      const vec2 ndc(m.x / W * 2 - 1, 1 - m.y / H * 2);
      const mat4 inv = glm::inverse(cam.viewProj);
      vec4 n0 = inv * vec4(ndc, -1, 1), n1 = inv * vec4(ndc, 1, 1);
      const vec3 a0 = vec3(n0) / n0.w, a1 = vec3(n1) / n1.w;
      const float topY = (float)(at.y + 1 - cam.origin.y);
      if (std::fabs(a1.y - a0.y) > 1e-5f) {
        const float u = (topY - a0.y) / (a1.y - a0.y);
        const vec3 hitRel = a0 + (a1 - a0) * u;
        const vec3 hp = vec3(glm::dvec3(hitRel) + cam.origin);
        const vec4 gr = worktableGridRect();
        if (u > 0 && hp.x >= at.x + gr.x - 0.02f && hp.x < at.x + gr.z + 0.02f && hp.z >= at.z + gr.y - 0.02f && hp.z < at.z + gr.w + 0.02f) {
          RayHit h; h.hit = true; h.block = at; h.normal = ivec3(0, 1, 0); h.point = hp;
          hover = worktableCell(h);
        }
      }
    }
    const int shown = padMode ? cursor : hover;
    cinematicSetAim(scene().phase == 2 ? shown : -1);
    if (shown >= 0 && scene().phase == 2) {   // the square lit
      const vec4 gr = worktableGridRect();   // (the squares as the table's picture draws them)
      const float cw = (gr.z - gr.x) / 3, cd = (gr.w - gr.y) / 3;
      const float x0 = at.x + gr.x + (shown % 3) * cw, z0 = at.z + gr.y + (shown / 3) * cd, y = at.y + 1.005f;
      ImVec2 q[4];
      if (project({x0, y, z0}, q[0]) && project({x0 + cw, y, z0}, q[1]) && project({x0 + cw, y, z0 + cd}, q[2]) && project({x0, y, z0 + cd}, q[3])) {
        dl->AddQuadFilled(q[0], q[1], q[2], q[3], IM_COL32(255, 236, 160, 60));
        dl->AddQuad(q[0], q[1], q[2], q[3], IM_COL32(255, 236, 160, 220), 2 * z);
      }
    }
    // what it makes, floating over the table (render/pile_layer.cpp draws it): click it to make it
    const CraftMatch m = worktableMatch(w, at, p.yaw, w.immersive);
    ImVec2 ghost; bool ghostOn = false;
    if (m && project(vec3(at) + vec3(0.5f, 1.5f, 0.5f), ghost)) {
      ghostOn = true;
      const ImVec2 mp = ImGui::GetIO().MousePos;
      const bool overGhost = !padMode && !overUi && std::hypot(mp.x - ghost.x, mp.y - ghost.y) < 48 * z;
      dl->AddCircle(ghost, 44 * z, overGhost ? IM_COL32(255, 236, 160, 230) : IM_COL32(255, 255, 255, 90), 32, 2 * z);
      if (overGhost && ImGui::IsMouseClicked(0)) { craft(a); return; }
    }
    if (scene().phase == 2 && !overUi && !padMode && hover >= 0) {
      if (ImGui::IsMouseClicked(0)) act(a, hover, p.held().empty());
      else if (ImGui::IsMouseClicked(1)) act(a, hover, true);
    }
    // your bag beside the table (Cinematic Interactions keeps the inventory to hand): click a stack to work with it
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    // the panel: what's in hand, what it makes, the ways out
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));   // (over the scene, not over the table)
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_table", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    const ItemStack& held = p.held();
    const std::string heldName = held.empty() ? "nothing" : (held.def() ? held.def()->name : held.id);
    const std::string makes = m ? ((m.result().def() ? m.result().def()->name : m.result().id) + (m.result().n > 1 ? " x" + std::to_string(m.result().n) : "")) : std::string("nothing yet");
    ImGui::Text("In hand: %s   \xC2\xB7   Makes: %s", heldName.c_str(), makes.c_str());
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the table"
                      : padMode ? "D-pad square \xC2\xB7 A set one down \xC2\xB7 X take back \xC2\xB7 Y make it \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                                : "Left click: set one down (empty hand: take it back) \xC2\xB7 Right click: take back \xC2\xB7 wheel / 1-9: what's in hand");
    if (ImGui::Button(m ? "Make it" : "Make it##off") && m) craft(a);
    ImGui::SameLine();
    if (ImGui::Button("Recipe book")) {   // the crafting screen as ever
      const ivec3 pos = at;
      uiDefer([pos](App& a2) { if (a2.session && a2.session->player) openClassic(a2, pos); });
      a.popScreen();
      ImGui::End();
      return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
    (void)ghostOn;
  }

private:
  ivec3 at;
  int cursor = -1;
  bool padMode = false, showBag = true;
  BagPad bag;
  void leave() { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } }
  void act(App& a, int cell, bool take) {
    GameSession* s = a.session.get();
    if (!s || !s->player || scene().phase != 2) return;
    Player& p = *s->player; World& w = s->world();
    cinematicTap();   // (the hand dips to the square)
    if (take) worktableTake(*s->level, p, w, at, cell);
    else worktablePut(*s->level, p, w, at, cell, p.held());
  }
  void craft(App& a) {
    GameSession* s = a.session.get();
    if (!s || !s->player || scene().phase != 2) return;
    worktableCraft(*s->level, *s->player, s->world(), at);
  }
  static void openClassic(App& a, ivec3 pos);
};

void CinematicTableScreen::openClassic(App& a, ivec3 pos) {
  if (a.session && a.session->level && a.session->player)
    a.events.emit("screenOpen", json::array({a.session->player->name, "table", json{{"pos", {pos.x, pos.y, pos.z}}, {"classic", true}}}));
}

} // namespace

vec3 cinematicViewOffset() { const Scene& S = scene(); return S.lean || S.phase == 0 ? vec3(0) : S.viewOff; }
float cinematicCrouch() { const Scene& S = scene(); return !S.lean && (S.kind == 3 || S.kind == 4 || S.kind == 7) ? S.bend : 0.f; }
bool cinematicLeaning() { return scene().lean; }
bool cinematicActive() { return scene().phase != 0; }
namespace { int g_aim = -1; double g_tapAt = -10, g_tapN = -1; }
void cinematicSetAim(int cell) { g_aim = cell; }
float cinematicMoving() {   // 0..1: how far into a move in or out (the bars come in and go as it settles)
  const Scene& S = scene();
  if (!S.lean) return 0.f;   // (seamless first person: no bars)
  if (S.phase == 1) return (float)std::sin(clampv(S.t / INTRO, 0.0, 1.0) * PI);
  if (S.phase == 3) return (float)std::sin(clampv(S.t / OUTRO, 0.0, 1.0) * PI);
  return 0.f;
}
void cinematicTap() { g_tapAt = scene().clock; static double n = 0; g_tapN = ++n; }
double cinematicTapTime() { return g_tapN; }
bool cinematicHandsState(ivec3& at, int& aimCell, double& sinceTap, int& phase, float& viewYaw, vec3* rest) {
  const Scene& S = scene();
  if (S.phase == 0) return false;
  at = S.at; aimCell = S.phase == 2 && S.kind == 0 ? g_aim : -1; sinceTap = S.clock - g_tapAt; phase = S.phase; viewYaw = S.viewYaw;
  if (rest) { rest[0] = S.rest[0]; rest[1] = S.rest[1]; }
  return true;
}

namespace {
// any other station as a scene: the lean in, its own screen once there (on top of this one), the glide back when it closes
class CinematicStationScreen : public Screen {
public:
  CinematicStationScreen(ivec3 at, int kind, std::function<std::unique_ptr<Screen>()> make) : at(at), kind(kind), make(std::move(make)) {}
  const char* id() const override { return "cinematic_station"; }
  bool pausesGame() const override { return false; }
  bool showsWorld() const override { return true; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = kind;
    leanPose(S, p.pos + vec3(0, p.eye, 0));
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } }
  bool onBack(App&) override { return false; }
  void update(App& a, double) override {
    if (!a.session || !a.session->player) { a.popScreen(); return; }
    if (!opened && scene().phase == 2) { opened = true; a.pushScreen(make()); return; }   // there: its screen
    if (opened && a.topScreen() == this) { a.popScreen(); return; }   // its screen closed: back
  }
  void draw(App&) override {}
private:
  ivec3 at; int kind; std::function<std::unique_ptr<Screen>()> make; bool opened = false;
};
}  // namespace

namespace {
double g_forgeAt = -1;   // when the hammer started (the scene's clock), -1: not forging
// your bag beside the station (the same as the table's): click a stack to work with it
void drawBag(App& a, Player& p, int padCur) {
  bookUiStationBag(padCur);   // (seamless first person: the journal in your off hand, open at the bag: ui/station_bag.cpp)
  if (bookUiStationOn(a)) return;
  const float W = (float)a.width, H = (float)a.height, z = ui::Z(), is = 40 * z;
  ImGui::SetNextWindowPos(ImVec2(W - 12 * z, H * 0.5f), ImGuiCond_Always, ImVec2(1, 0.5f));
  ImGui::SetNextWindowBgAlpha(0.72f);
  ImGui::Begin("##cine_bag2", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
  ImGui::TextDisabled("Your bag  (E hides it)");
  const int n = std::min<int>((int)p.inv.size(), 36);
  for (int i = 0; i < n; i++) {
    const int slot = i < 27 ? 9 + i : i - 27;
    if (i % 9) ImGui::SameLine(0, 3 * z);
    ImGui::PushID(i);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("slot", ImVec2(is, is))) { if (slot < 9) p.selected = slot; else std::swap(p.inv[slot], p.inv[p.selected]); }
    ImDrawList* wl = ImGui::GetWindowDrawList();
    wl->AddRectFilled(pos, ImVec2(pos.x + is, pos.y + is), ImGui::IsItemHovered() ? IM_COL32(90, 84, 70, 220) : IM_COL32(40, 36, 30, 200), 3 * z);
    if (slot == p.selected) wl->AddRect(pos, ImVec2(pos.x + is, pos.y + is), IM_COL32(255, 220, 120, 255), 3 * z, 0, 2 * z);
    if (i == padCur) wl->AddRect(ImVec2(pos.x - 2 * z, pos.y - 2 * z), ImVec2(pos.x + is + 2 * z, pos.y + is + 2 * z), IM_COL32(120, 220, 255, 255), 4 * z, 0, 3 * z);   // (the pad's cursor)
    if (!p.inv[slot].empty()) ui::drawItem(wl, p.inv[slot], ImVec2(pos.x + 2 * z, pos.y + 2 * z), is - 4 * z);
    if ((ImGui::IsItemHovered() || i == padCur) && !p.inv[slot].empty() && p.inv[slot].def()) { ImGui::BeginTooltip(); ImGui::TextUnformatted(p.inv[slot].def()->name.c_str()); ImGui::EndTooltip(); }
    ImGui::PopID();
    if (i == 26) ImGui::Dummy(ImVec2(1, 4 * z));
  }
  ImGui::End();
}
std::string nameOf(const ItemStack& s) { return s.empty() ? std::string("nothing") : (s.def() ? s.def()->name : s.id) + (s.n > 1 ? " x" + std::to_string(s.n) : ""); }

// the anvil as a scene (Cinematic Interactions' AnvilStation): the work on its face, what goes into it to the right, and
// the hammer: three blows (0.7, 1.02, 1.3 s), the forging on the first
class CinematicAnvilScreen : public Screen {
public:
  explicit CinematicAnvilScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_anvil"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 1; g_forgeAt = -1;
    leanPose(S, p.pos + vec3(0, p.eye, 0));
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } g_forgeAt = -1; g_avOn = false; }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool here = reg().block(w.getBlock(at.x, at.y, at.z)).str("station") == "anvil";
    const bool walk = sceneWalkOff(in);
    if ((!here && g_forgeAt < 0) || walk || p.dead || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f) { a.popScreen(); return; }
    if (in.pressed(Act::Inventory) && !in.pressed(Act::MenuY)) showBag = !showBag;
    for (int k = 0; k < 9; k++) if (in.pressed((Act)((int)Act::Hot1 + k))) p.selected = k;
    if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && !(ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook())) p.selected = ((p.selected - (wh > 0 ? 1 : -1)) % 9 + 9) % 9;
    const bool inBag = bagPadUpdate(in, p, bag);
    if (g_forgeAt >= 0) {   // the hammer: the work is done on the first blow; the rest ring it out
      const double t = scene().clock - g_forgeAt;
      if (!forged && t >= 0.7) { forged = true; anvilWorkForge(*s->level, p, w, at); }
      for (int b = 0; b < 3; b++) if (blows <= b && t >= BLOWS[b] - 0.18) { blows = b + 1; cinematicTap(); }   // (each blow: your arm swings the hammer down, landing on the blow)
      if (t >= 1.3 + 0.32) g_forgeAt = -1;   // (its PUT_AWAY: the hammer put down)
    }
    if (inBag) return;
    if (in.pressed(Act::MenuY) || in.pressed(Act::MenuStart)) forge(a);
    const ivec2 nav = in.navStep();
    if (nav.x) { pad = true; zone = nav.x > 0 ? 1 : 0; }
    if (pad && in.pressed(Act::MenuOk)) put(a, zone);
    if (pad && in.pressed(Act::MenuX)) take(a, zone);
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world(); Camera& cam = s->frame.cam;
    const float W = (float)a.width, H = (float)a.height, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook());
    const vec3 c = vec3(at) + vec3(0.5f, 1.f, 0.5f);
    const vec3 right(std::cos(scene().viewYaw), 0, -std::sin(scene().viewYaw));
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    int hover = pad ? zone : -1;
    if (!pad && !overUi && scene().phase == 2) {   // the ray through the mouse onto the face
      const ImVec2 m = ImGui::GetIO().MousePos; const vec2 ndc(m.x / W * 2 - 1, 1 - m.y / H * 2);
      const mat4 inv = glm::inverse(cam.viewProj);
      const vec4 n0 = inv * vec4(ndc, -1, 1), n1 = inv * vec4(ndc, 1, 1);
      const vec3 a0 = vec3(n0) / n0.w, a1 = vec3(n1) / n1.w;
      const float topY = (float)(at.y + 1 - cam.origin.y);
      if (std::fabs(a1.y - a0.y) > 1e-5f) {
        const float u = (topY - a0.y) / (a1.y - a0.y);
        const vec3 hp = vec3(glm::dvec3(a0 + (a1 - a0) * u) + cam.origin);
        if (u > 0 && hp.x >= at.x - 0.05f && hp.x < at.x + 1.05f && hp.z >= at.z - 0.05f && hp.z < at.z + 1.05f) hover = glm::dot(hp - c, right) > 0.1f ? 1 : 0;
      }
    }
    zone = hover >= 0 ? hover : zone;
    auto project = [&](vec3 wp, ImVec2& out) { vec4 q = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1); if (q.w <= 0.01f) return false; out = ImVec2((q.x / q.w * 0.5f + 0.5f) * W, (1 - (q.y / q.w * 0.5f + 0.5f)) * H); return true; };
    if (hover >= 0 && scene().phase == 2 && g_forgeAt < 0) {   // the spot lit: the work's in the middle, what goes into it to the right
      ImVec2 sp; const vec3 spot = hover ? c + right * 0.22f : c - right * 0.02f;
      if (project(spot, sp)) dl->AddCircle(sp, 34 * z, IM_COL32(255, 236, 160, 220), 28, 2 * z);
    }
    if (hover >= 0 && !overUi && !pad && scene().phase == 2) {
      // (AnvilStation: a left click on the work with a result ready forges it; with something in hand, it's set down)
      if (ImGui::IsMouseClicked(0)) { if (p.held().empty() && hover == 0 && anvilWorkResult(w, at).ok) forge(a); else if (p.held().empty() && hover == 0 && !anvilWorkItems(w, at)[1].empty()) denyAt = scene().clock; else if (p.held().empty()) take(a, hover); else put(a, hover); }
      else if (ImGui::IsMouseClicked(1)) take(a, hover);
    }
    g_av.at = at; g_av.viewYaw = scene().viewYaw; g_av.hover = hover; g_av.denyT = denyAt < 0 ? -1 : scene().clock - denyAt; g_av.forgeT = g_forgeAt < 0 ? -1 : scene().clock - g_forgeAt; g_avOn = true;
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    const std::vector<ItemStack> v = anvilWorkItems(w, at);
    const StationResult r = anvilWorkResult(w, at);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_anvil", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("In hand: %s   \xC2\xB7   On the anvil: %s   \xC2\xB7   Into it: %s", nameOf(p.held()).c_str(), nameOf(v[0]).c_str(), nameOf(v[1]).c_str());
    if (r.ok) ImGui::Text("Makes: %s%s%s", nameOf(r.out).c_str(), r.note.empty() ? "" : "   \xC2\xB7   ", r.note.c_str());
    else ImGui::TextDisabled(v[0].empty() ? "Lay a worn tool (or what to name or enchant) on the anvil, its material beside it" : "That won't forge into anything");
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the anvil"
                      : pad ? "D-pad left / right the spot \xC2\xB7 A set it down \xC2\xB7 X take back \xC2\xB7 Y forge \xC2\xB7 LB / RB your bag \xC2\xB7 B leave" : "Left click: set down \xC2\xB7 empty hand on the work: forge it (or take it back) \xC2\xB7 Right click: take back");
    ImGui::BeginDisabled(!r.ok || g_forgeAt >= 0);
    if (ImGui::Button("Forge")) forge(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Anvil screen")) {   // the anvil's screen as ever (naming)
      const ivec3 pos = at;
      uiDefer([pos](App& a2) { if (a2.session && a2.session->player) a2.events.emit("screenOpen", json::array({a2.session->player->name, "station", json{{"kind", "anvil"}, {"pos", {pos.x, pos.y, pos.z}}, {"classic", true}}})); });
      a.popScreen(); ImGui::End(); return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; int zone = 0; bool pad = false, showBag = true, forged = false; int blows = 0; double denyAt = -1;
  static constexpr double BLOWS[3] = {0.7, 1.02, 1.3};   // (AnvilStation's blows)
  BagPad bag;
  void put(App& a, int slot) { GameSession* s = a.session.get(); if (!s || g_forgeAt >= 0 || scene().phase != 2) return; cinematicTap(); anvilWorkPut(*s->level, *s->player, s->world(), at, slot, s->player->held()); }
  void take(App& a, int slot) { GameSession* s = a.session.get(); if (!s || g_forgeAt >= 0 || scene().phase != 2) return; cinematicTap(); anvilWorkTake(*s->level, *s->player, s->world(), at, slot); }
  void forge(App& a) {
    GameSession* s = a.session.get();
    if (!s || g_forgeAt >= 0 || scene().phase != 2) return;
    if (!anvilWorkResult(s->world(), at).ok) { denyAt = scene().clock; return; }   // (it won't: the card shakes no)
    g_forgeAt = scene().clock; forged = false; blows = 0;
  }
};
}  // namespace

namespace {
// ---- the grindstone and the stonecutter as scenes (Cinematic Interactions' GrindstoneStation / StonecutterStation) ----
GrindView g_gv; CutView g_cv; bool g_gvOn = false, g_cvOn = false;
double win(double t, double a, double b) { return clampv((t - a) / (b - a), 0.0, 1.0); }
double smoother(double x) { x = clampv(x, 0.0, 1.0); return x * x * x * (x * (x * 6 - 15) + 10); }
vec3 sideOf(float viewYaw) { return vec3(std::sin(viewYaw), 0, std::cos(viewYaw)); }   // from the station toward you
vec3 rightOf(float viewYaw) { return vec3(std::cos(viewYaw), 0, -std::sin(viewYaw)); }
// the screen point of a world point (false: behind the camera)
bool toScreen(App& a, vec3 wp, ImVec2& out) {
  Camera& cam = a.session->frame.cam;
  const vec4 q = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1);
  if (q.w <= 0.01f) return false;
  out = ImVec2((q.x / q.w * 0.5f + 0.5f) * a.width, (1 - (q.y / q.w * 0.5f + 0.5f)) * a.height);
  return true;
}
// GrindstoneStation.under: the mouse within a world radius of a point, as it shows on screen (at least 9 px)
bool under(App& a, vec3 c, float r) {
  ImVec2 p, e;
  if (!toScreen(a, c, p) || !toScreen(a, c + vec3(0, r, 0), e)) return false;
  const ImVec2 m = ImGui::GetIO().MousePos;
  return std::hypot(m.x - p.x, m.y - p.y) < std::max(9.f, std::hypot(p.x - e.x, p.y - e.y));
}
// the common part of the two scenes' updates: false when the scene must end
bool sceneKeys(App& a, ivec3 at, bool working, bool& showBag) {
  GameSession* s = a.session.get();
  Player& p = *s->player; Input& in = a.input;
  const bool walk = sceneWalkOff(in);
  if ((walk && !working) || p.dead || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f) return false;
  if (in.pressed(Act::Inventory) && !in.pressed(Act::MenuY)) showBag = !showBag;
  for (int k = 0; k < 9; k++) if (in.pressed((Act)((int)Act::Hot1 + k))) p.selected = k;
  return true;
}
}  // namespace

vec3 grindHeldPos(ivec3 at, float viewYaw, int k, double t, vec3 lying) {
  const vec3 d = sideOf(viewYaw), right = rightOf(viewYaw);
  const vec3 top = vec3(at) + vec3(0.5f, 1.f, 0.5f);
  const double s = win(t, GRIND_CONTACT, GRIND_LET_GO), fade = smoother(s * 8) * smoother((1 - s) * 8);
  const double stroke = 0.075 * std::sin(PI * (t - GRIND_CONTACT) / 0.433) * fade;   // (STROKE 0.433 s each way)
  const float sideways = (float)((k == 0 ? 0.08 : -0.08) + stroke * (k == 0 ? 1 : -1));
  const vec3 grinding = top + d * 0.04f + vec3(0, 0.03f, 0) + right * sideways;
  const float lift = (float)inOutCubic(win(t, 0.28, GRIND_CONTACT));
  vec3 p = lying + vec3(0, 0.008f, 0) + (grinding - lying) * lift + vec3(0, 0.09f * std::sin(lift * PI), 0);
  p.y -= 0.008f * std::fabs(std::sin(PI * (t - GRIND_CONTACT) / 0.433)) * (float)win(t, GRIND_CONTACT, 0.7);
  return p;
}
namespace { int dialVisible(int n) { return std::min(9, n); } float dialStep(int n) { const int v = dialVisible(n); return v <= 1 ? 0.f : std::min(22.f, 144.f / (v - 1)); } }
static float dialAngle(int k, int n, float scroll) { return 90.f + ((dialVisible(n) - 1) / 2.f - (k - scroll)) * dialStep(n); }
float cutDialShown(int k, int count, float scroll) {
  const float a = dialAngle(k, count, scroll);
  return (float)(smoother(win(a, 4, 18)) * smoother(win(176 - a, 0, 14)));
}
vec3 cutDialPoint(ivec3 at, float viewYaw, int k, int count, float scroll, double clock) {
  const float a = dialAngle(k, count, scroll) * PI / 180.f;
  const float bob = 0.008f * std::sin((float)clock * 1.7f + k * 1.3f);
  const vec3 top = vec3(at) + vec3(0.5f, 0.5625f, 0.5f);
  return top - sideOf(viewYaw) * 0.06f + rightOf(viewYaw) * (std::cos(a) * 0.72f) + vec3(0, 0.04f + std::sin(a) * 0.5f + bob, 0);
}
bool cinematicGrindView(GrindView& v) { if (!g_gvOn || scene().phase == 0) return false; v = g_gv; return true; }
bool cinematicCutView(CutView& v) { if (!g_cvOn || scene().phase == 0) return false; v = g_cv; return true; }

namespace {
// the grindstone: two pieces lie on its top; with something in hand, a click on the stone sets one piece of it down (the
// other side if this one's taken); an empty hand on a piece grinds when there's a result (else takes it back); right
// click takes back. The grinding (GRIND_LENGTH 2.4 s): lifted to the wheel by 0.6 s, the result made then, stroked to and
// fro till 1.9 s, sparks at each stroke
class CinematicGrindScreen : public Screen {
public:
  explicit CinematicGrindScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_grind"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 6; S.front = vec3(0);
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    g_gv = GrindView{}; g_gv.at = at; g_gvOn = true;
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } g_gvOn = false; cinematicSetAimPoint(false); }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool here = reg().block(w.getBlock(at.x, at.y, at.z)).str("station") == "grindstone";
    if ((!here && grindAt < 0) || !sceneKeys(a, at, grindAt >= 0, showBag)) { a.popScreen(); return; }
    const bool inBag = bagPadUpdate(in, p, bag);
    if (grindAt >= 0) {
      const double t = scene().clock - grindAt;
      if (!ground && t >= GRIND_CONTACT) { ground = true; if (!grindWorkDo(*s->level, p, w, at)) grindAt = -1; }
      // a shower of sparks off the wheel at each stroke's turn (and the stone's grind sound with it)
      if (grindAt >= 0 && t >= GRIND_CONTACT && t < GRIND_LET_GO) {
        const int stroke = (int)((t - GRIND_CONTACT) / 0.433);
        if (stroke != lastStroke) {
          lastStroke = stroke;
          const vec3 c = vec3(at) + vec3(0.5f, 1.02f, 0.5f) + sideOf(scene().viewYaw) * 0.06f;
          if (s->level->fx) s->level->fx->particles("spark", c, 6);
          if (a.audio && stroke > 0) a.audio->play("grind");
          cinematicTap();
        }
      }
      if (grindAt >= 0 && t >= GRIND_LENGTH) grindAt = -1;
    }
    if (inBag) return;
    if (in.pressed(Act::MenuY) || in.pressed(Act::MenuStart)) grind(a);
    const ivec2 nav = in.navStep();
    if (nav.x) { pad = true; zone = nav.x > 0 ? 1 : 0; }
    if (pad && in.pressed(Act::MenuOk)) put(a, zone);
    if (pad && in.pressed(Act::MenuX)) take(a, zone);
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world();
    const float W = (float)a.width, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook();
    const std::vector<ItemStack> v = grindWorkItems(w, at);
    const StationResult r = grindWorkResult(w, at);
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    // what the mouse is on: a piece lying there (empty hand), or the stone (something in hand)
    int hover = -1; bool onStone = false;
    if (!pad && !overUi && scene().phase == 2 && grindAt < 0) {
      for (int k = 0; k < 2 && hover < 0; k++) if (!v[k].empty() && under(a, grindLiePos(w, at, k), 0.13f)) hover = k;
      onStone = under(a, vec3(at) + vec3(0.5f, 0.9f, 0.5f), 0.4f);
    } else if (pad) hover = zone;
    const vec3 aimAt = hover >= 0 ? grindLiePos(w, at, hover) : vec3(at) + vec3(0.5f, 1.f, 0.5f);
    cinematicSetAimPoint(scene().phase == 2 && (hover >= 0 || onStone || grindAt >= 0), grindAt >= 0 ? vec3(at) + vec3(0.5f, 1.05f, 0.5f) : aimAt);
    if (scene().phase == 2 && grindAt < 0 && (hover >= 0 || (onStone && !p.held().empty()))) {
      ImVec2 sp; if (toScreen(a, aimAt, sp)) dl->AddCircle(sp, 30 * z, IM_COL32(255, 236, 160, 220), 28, 2 * z);
    }
    if (!overUi && !pad && scene().phase == 2 && grindAt < 0) {
      if (ImGui::IsMouseClicked(0)) {
        if (!p.held().empty() && (onStone || hover >= 0)) put(a, hover >= 0 ? hover : (v[0].empty() ? 0 : 1));
        else if (hover >= 0) { if (r.ok) grind(a); else take(a, hover); }
      } else if (ImGui::IsMouseClicked(1) && hover >= 0) take(a, hover);
    }
    g_gv.at = at; g_gv.viewYaw = scene().viewYaw; g_gv.hover = hover; g_gv.t = grindAt < 0 ? -1 : scene().clock - grindAt;
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_grind", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("In hand: %s   \xC2\xB7   On the stone: %s, %s", nameOf(p.held()).c_str(), nameOf(v[0]).c_str(), nameOf(v[1]).c_str());
    if (grindAt >= 0) ImGui::Text("Grinding: %s", nameOf(g_gv.after).c_str());
    else if (r.ok) ImGui::Text("Grinds into: %s%s%s", nameOf(r.out).c_str(), r.note.empty() ? "" : "   \xC2\xB7   ", r.note.c_str());
    else ImGui::TextDisabled(v[0].empty() && v[1].empty() ? "Lay an enchanted piece on the stone (or two worn ones of a kind) to grind" : "Nothing to grind off that");
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the grindstone"
                      : pad ? "D-pad left / right the side \xC2\xB7 A set it down \xC2\xB7 X take back \xC2\xB7 Y grind \xC2\xB7 LB / RB your bag \xC2\xB7 B leave" : "Left click the stone: set down \xC2\xB7 empty hand on a piece: grind it (or take it back) \xC2\xB7 Right click: take back");
    ImGui::BeginDisabled(!r.ok || grindAt >= 0);
    if (ImGui::Button("Grind")) grind(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Grindstone screen")) {
      const ivec3 pos = at;
      uiDefer([pos](App& a2) { if (a2.session && a2.session->player) a2.events.emit("screenOpen", json::array({a2.session->player->name, "station", json{{"kind", "grindstone"}, {"pos", {pos.x, pos.y, pos.z}}, {"classic", true}}})); });
      a.popScreen(); ImGui::End(); return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; int zone = 0, lastStroke = -1; bool pad = false, showBag = true, ground = false; double grindAt = -1;
  BagPad bag;
  void put(App& a, int slot) { GameSession* s = a.session.get(); if (!s || grindAt >= 0 || scene().phase != 2) return; cinematicTap(); grindWorkPut(*s->level, *s->player, s->world(), at, slot, s->player->held()); }
  void take(App& a, int slot) { GameSession* s = a.session.get(); if (!s || grindAt >= 0 || scene().phase != 2) return; cinematicTap(); grindWorkTake(*s->level, *s->player, s->world(), at, slot); }
  void grind(App& a) {
    GameSession* s = a.session.get();
    if (!s || grindAt >= 0 || scene().phase != 2) return;
    World& w = s->world();
    const StationResult r = grindWorkResult(w, at);
    if (!r.ok) return;
    const std::vector<ItemStack> v = grindWorkItems(w, at);
    g_gv.before[0] = v[0]; g_gv.before[1] = v[1]; g_gv.after = r.out;
    grindAt = scene().clock; ground = false; lastStroke = -1;
    if (a.audio) a.audio->play("grind");
  }
};

// the stonecutter: the stone lies on its table beside the saw; the shapes it cuts into float on a dial over the saw
// (scroll it when there are more than nine); a click on a shape cuts one (right click or X: all of it). The cut
// (CUT_LENGTH 1.05 s, all 1.5): a block slides into the blade, the cut made at 0.26 s, the pieces out toward you
class CinematicCutScreen : public Screen {
public:
  explicit CinematicCutScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_cut"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 7; S.front = vec3(0);
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    g_cv = CutView{}; g_cv.at = at; g_cvOn = true;
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } g_cvOn = false; cinematicSetAimPoint(false); }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double dt) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool here = reg().block(w.getBlock(at.x, at.y, at.z)).has("stonecutter");
    if ((!here && cutAt < 0) || !sceneKeys(a, at, cutAt >= 0, showBag)) { a.popScreen(); return; }
    const bool inBag = bagPadUpdate(in, p, bag);
    const int n = (int)cutWorkOptions(w, at).size();
    if (n != lastCount) { lastCount = n; scrollTo = 0; scroll = 0; padPick = 0; }
    scroll += (scrollTo - scroll) * std::min(1.f, (float)dt * 12.f);
    if (cutAt >= 0) {
      const double t = scene().clock - cutAt;
      if (!cut && t >= CUT_BITE) {
        cut = true;
        if (!cutWorkDo(*s->level, p, w, at, pick, g_cv.all)) cutAt = -1;
        else if (s->level->fx) s->level->fx->particles("block", vec3(at) + vec3(0.5f, 0.75f, 0.5f), 10, json{{"tile", g_cv.input.id}});
      }
      if (cutAt >= 0 && t >= (g_cv.all ? CUT_ALL_LENGTH : CUT_LENGTH)) cutAt = -1;
    }
    if (inBag) return;
    const ivec2 nav = in.navStep();
    if (nav.x && n > 0) { pad = true; padPick = std::clamp(padPick + nav.x, 0, n - 1); scrollTo = (float)std::clamp(padPick - 4, 0, std::max(0, n - 9)); }
    // (A cuts one, X all; with nothing on the saw A sets down what's in hand. Y: what's in hand onto the saw, or the stone back)
    if (pad && in.pressed(Act::MenuOk)) { if (n == 0 && !p.held().empty()) put(a); else cutShape(a, padPick, false); }
    if (pad && in.pressed(Act::MenuX)) cutShape(a, padPick, true);
    if (in.pressed(Act::MenuY)) { if (!p.held().empty() && !stonecutterOptions(p.held().id).empty()) put(a); else take(a); }
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world();
    const float W = (float)a.width, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook();
    const ItemStack stone = cutWorkItem(w, at);
    const auto opts = cutWorkOptions(w, at);
    const int n = (int)opts.size();
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    int hover = -1; bool onStone = false, onTable = false;
    if (!pad && !overUi && scene().phase == 2 && cutAt < 0) {
      for (int k = 0; k < n && hover < 0; k++) if (cutDialShown(k, n, scroll) > 0.5f && under(a, cutDialPoint(at, scene().viewYaw, k, n, scroll, scene().clock), 0.075f)) hover = k;
      if (hover < 0) { onStone = !stone.empty() && under(a, cutLiePos(w, at) + vec3(0, 0.08f, 0), 0.12f); onTable = under(a, vec3(at) + vec3(0.5f, 0.56f, 0.5f), 0.45f); }
      if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && n > 9) scrollTo = std::clamp(scrollTo - (wh > 0 ? 1.f : -1.f), 0.f, (float)(n - 9));
      else if (wh != 0) p.selected = ((p.selected - (wh > 0 ? 1 : -1)) % 9 + 9) % 9;
    } else if (pad && n > 0) hover = std::min(padPick, n - 1);
    const vec3 aimPt = hover >= 0 ? cutDialPoint(at, scene().viewYaw, hover, n, scroll, scene().clock) - vec3(0, 0.1f, 0) + sideOf(scene().viewYaw) * 0.12f
                     : onStone || onTable ? cutLiePos(w, at) : vec3(at) + vec3(0.5f, 0.6f, 0.5f);
    cinematicSetAimPoint(scene().phase == 2 && (hover >= 0 || onStone || onTable || cutAt >= 0), cutAt >= 0 ? vec3(at) + vec3(0.5f, 0.62f, 0.5f) : aimPt);
    if (scene().phase == 2 && cutAt < 0) {
      ImVec2 sp;
      if (hover >= 0 && toScreen(a, cutDialPoint(at, scene().viewYaw, hover, n, scroll, scene().clock), sp)) {
        dl->AddCircle(sp, 26 * z, IM_COL32(255, 236, 160, 230), 28, 2 * z);
        const ItemStack out(opts[hover].first, opts[hover].second);
        const std::string label = nameOf(out) + (stone.n > 1 ? std::string(pad ? "   (X: all " : "   (right click: all ") + std::to_string(stone.n) + ")" : "");
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        dl->AddRectFilled(ImVec2(sp.x - ts.x * 0.5f - 6 * z, sp.y - 46 * z), ImVec2(sp.x + ts.x * 0.5f + 6 * z, sp.y - 46 * z + ts.y + 6 * z), IM_COL32(20, 16, 12, 200), 4 * z);
        dl->AddText(ImVec2(sp.x - ts.x * 0.5f, sp.y - 43 * z), IM_COL32(255, 240, 210, 255), label.c_str());
      } else if ((onStone || (onTable && !p.held().empty())) && toScreen(a, cutLiePos(w, at), sp)) dl->AddCircle(sp, 30 * z, IM_COL32(255, 236, 160, 220), 28, 2 * z);
    }
    if (!overUi && !pad && scene().phase == 2 && cutAt < 0) {
      if (ImGui::IsMouseClicked(0)) {
        if (hover >= 0) cutShape(a, hover, ImGui::GetIO().KeyShift);
        else if (!p.held().empty() && (onTable || onStone)) put(a);
        else if (onStone) take(a);
      } else if (ImGui::IsMouseClicked(1)) { if (hover >= 0) cutShape(a, hover, true); else if (onStone) take(a); }
    }
    g_cv.at = at; g_cv.viewYaw = scene().viewYaw; g_cv.hover = hover; g_cv.scroll = scroll; g_cv.t = cutAt < 0 ? -1 : scene().clock - cutAt;
    if (cutAt < 0) g_cv.input = stone;
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_cut", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("In hand: %s   \xC2\xB7   On the saw: %s", nameOf(p.held()).c_str(), nameOf(stone).c_str());
    if (stone.empty()) ImGui::TextDisabled("Lay stone on the table by the saw: what it cuts into rises over the blade");
    else ImGui::TextDisabled("%d shape%s%s", n, n == 1 ? "" : "s", n > 9 ? "  \xC2\xB7  the wheel turns the dial" : "");
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the stonecutter"
                      : pad ? "D-pad left / right a shape \xC2\xB7 A cut one \xC2\xB7 X cut all \xC2\xB7 Y set down what's in hand (empty hand: take the stone back) \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : "Left click the table: set the stone down \xC2\xB7 left click a shape: cut one (shift / right click: all) \xC2\xB7 empty hand on the stone: take it back");
    if (ImGui::Button("Stonecutter screen")) {
      uiDefer([](App& a2) { if (a2.session && a2.session->player) a2.events.emit("screenOpen", json::array({a2.session->player->name, "stonecutter", json{{"classic", true}}})); });
      a.popScreen(); ImGui::End(); return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; bool pad = false, showBag = true, cut = false; int padPick = 0, pick = 0, lastCount = -1; float scroll = 0, scrollTo = 0; double cutAt = -1;
  BagPad bag;
  void put(App& a) { GameSession* s = a.session.get(); if (!s || cutAt >= 0 || scene().phase != 2) return; cinematicTap(); cutWorkPut(*s->level, *s->player, s->world(), at, s->player->held()); }
  void take(App& a) { GameSession* s = a.session.get(); if (!s || cutAt >= 0 || scene().phase != 2) return; cinematicTap(); cutWorkTake(*s->level, *s->player, s->world(), at); }
  void cutShape(App& a, int k, bool all) {
    GameSession* s = a.session.get();
    if (!s || cutAt >= 0 || scene().phase != 2) return;
    World& w = s->world();
    const auto opts = cutWorkOptions(w, at);
    const ItemStack stone = cutWorkItem(w, at);
    if (k < 0 || k >= (int)opts.size() || stone.empty()) return;
    pick = k; cut = false; cutAt = scene().clock;
    g_cv.all = all && stone.n > 1; g_cv.input = stone;
    g_cv.piece = ItemStack(opts[k].first, opts[k].second * (g_cv.all ? stone.n : 1));
    cinematicTap();
    if (a.audio) a.audio->play("click");
  }
};
}  // namespace

std::unique_ptr<Screen> makeCinematicGrindScreen(ivec3 at) { return std::make_unique<CinematicGrindScreen>(at); }
std::unique_ptr<Screen> makeCinematicCutScreen(ivec3 at) { return std::make_unique<CinematicCutScreen>(at); }

namespace {
double g_ritualAt = -1;   // the enchanting rite (the scene's clock), -1: none
// the enchanting table as a scene (Cinematic Interactions' EnchantStation): the item over the table, the three offers in a
// crown over it (the middle highest: OFFER_HIGH 0.92, the others OFFER_LOW 0.74 and OFFER_SPREAD 0.4 out), the rite
// (RITUAL_LENGTH 1.5 s, the enchantment at RITUAL_PEAK 0.5)
class CinematicEnchantScreen : public Screen {
public:
  explicit CinematicEnchantScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_enchant"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 2; g_ritualAt = -1;
    leanPose(S, p.pos + vec3(0, p.eye, 0));
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } g_ritualAt = -1; g_evOn = false; }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool here = reg().block(w.getBlock(at.x, at.y, at.z)).str("station") == "enchant";
    const bool walk = sceneWalkOff(in);
    if (!here || walk || p.dead || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f) { a.popScreen(); return; }
    if (in.pressed(Act::Inventory) && !in.pressed(Act::MenuY)) showBag = !showBag;
    for (int k = 0; k < 9; k++) if (in.pressed((Act)((int)Act::Hot1 + k))) p.selected = k;
    if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && !(ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook())) p.selected = ((p.selected - (wh > 0 ? 1 : -1)) % 9 + 9) % 9;
    if (g_ritualAt >= 0) {   // the rite: the enchantment at its peak
      const double t = scene().clock - g_ritualAt;
      if (!applied && t >= 0.5) { applied = true; const std::string why = enchWorkApply(*s->level, p, w, at, pick); if (!why.empty()) toast(*s->level, p, why, "warn"); }
      if (t >= 1.5) g_ritualAt = -1;
      return;
    }
    if (bagPadUpdate(in, p, bag)) return;
    const ivec2 nav = in.navStep();
    if (nav.x) { pad = true; offer = std::clamp((offer < 0 ? 1 : offer) + nav.x, 0, 2); }
    if (pad && in.pressed(Act::MenuOk)) choose(a, offer);
    if (in.pressed(Act::MenuX)) { if (!p.held().empty()) enchWorkPut(*s->level, p, w, at, p.held()); else enchWorkTake(*s->level, p, w, at, 0); cinematicTap(); }
    if (in.pressed(Act::MenuY)) enchWorkTake(*s->level, p, w, at, 1);
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world(); Camera& cam = s->frame.cam;
    const float W = (float)a.width, H = (float)a.height, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook());
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    auto project = [&](vec3 wp, ImVec2& out) { vec4 q = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1); if (q.w <= 0.01f) return false; out = ImVec2((q.x / q.w * 0.5f + 0.5f) * W, (1 - (q.y / q.w * 0.5f + 0.5f)) * H); return true; };
    const vec3 top = vec3(at) + vec3(0.5f, 0.75f, 0.5f);
    const vec3 side(std::sin(scene().viewYaw), 0, std::cos(scene().viewYaw)), right(std::cos(scene().viewYaw), 0, -std::sin(scene().viewYaw));
    std::array<EnchOffer, 3> offers{};
    const bool has = enchWorkOffers(w, at, p, offers);
    const std::vector<ItemStack> v = enchWorkItems(w, at);
    const ImVec2 mp = ImGui::GetIO().MousePos;
    int hover = pad ? offer : -1;
    if (has && scene().phase >= 1) for (int k = 0; k < 3; k++) {   // the crown of offers: tablets in the world (render/station_cards.cpp); the mouse on one picks it
      const vec3 c = cinematicOfferPos(at, scene().viewYaw, k, scene().clock);
      ImVec2 sp, se;
      if (!project(c, sp) || !project(c + right * 0.17f, se)) continue;
      const float r = std::max(18.f, std::hypot(se.x - sp.x, se.y - sp.y));
      if (!pad && !overUi && std::fabs(mp.x - sp.x) < r && std::fabs(mp.y - sp.y) < r * 0.8f) hover = k;
    }
    if (hover >= 0 && has) {   // what it is, when you look at it (EnchantStation's clue: the first enchantment, the rest a mystery)
      const EnchOffer& o = offers[hover];
      const bool can = p.creative() || (p.level >= o.level && v[1].id == "lapis_lazuli" && v[1].n >= o.cost);
      ImGui::BeginTooltip();
      if (!o.order.empty()) ImGui::Text("%s . . . ?", enchName(o.order[0], o.en.is_object() && o.en.contains(o.order[0]) ? o.en[o.order[0]].get<int>() : 1).c_str());
      ImGui::TextColored(can ? ImVec4(0.6f, 1, 0.5f, 1) : ImVec4(1, 0.5f, 0.45f, 1), "Level %d  \xC2\xB7  %d lapis%s", o.level, o.cost, can ? "" : p.level < o.level ? "  (your level is too low)" : "  (not enough lapis)");
      ImGui::EndTooltip();
    }
    g_ev.at = at; g_ev.viewYaw = scene().viewYaw; g_ev.hover = has ? hover : -1; g_ev.ritualT = g_ritualAt < 0 ? -1 : scene().clock - g_ritualAt; g_ev.pick = pick; g_evOn = true;
    if (hover >= 0) offer = hover;
    if (!pad && !overUi && scene().phase == 2 && g_ritualAt < 0) {
      if (ImGui::IsMouseClicked(0)) {
        // the table itself (or the item over it) under the mouse: set down / take back there; anywhere else, nothing
        ImVec2 ip, ie; bool onTable = false;
        if (project(vec3(at) + vec3(0.5f, 1.05f, 0.5f), ip) && project(vec3(at) + vec3(0.5f, 1.05f, 0.5f) + right * 0.45f, ie)) {
          const float r = std::hypot(ie.x - ip.x, ie.y - ip.y);
          onTable = std::fabs(mp.x - ip.x) < r && mp.y > ip.y - r * 1.1f && mp.y < ip.y + r * 1.4f;
        }
        if (hover >= 0 && has) choose(a, hover);
        else if (onTable && !p.held().empty()) { cinematicTap(); enchWorkPut(*s->level, p, w, at, p.held()); }
        else if (onTable) { cinematicTap(); enchWorkTake(*s->level, p, w, at, 0); }
      } else if (ImGui::IsMouseClicked(1) && hover < 0) enchWorkTake(*s->level, p, w, at, 0);
    }
    if (g_ritualAt >= 0) {   // the burst: a white-violet flash at the peak (EnchantStation's flash)
      const double t = scene().clock - g_ritualAt;
      const float fl = (float)std::exp(-std::pow((t - 0.5) / 0.12, 2));
      if (fl > 0.01f) ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(235, 215, 255, (int)(150 * fl)));
    }
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_ench", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("In hand: %s   \xC2\xB7   Over the table: %s   \xC2\xB7   Lapis: %d   \xC2\xB7   Your level: %d", nameOf(p.held()).c_str(), nameOf(v[0]).c_str(), v[1].empty() ? 0 : v[1].n, p.level);
    if (v[0].empty()) ImGui::TextDisabled("Set what you'd enchant over the table, and lapis by it");
    else if (!has) ImGui::TextDisabled("The table has nothing for that");
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the table"
                      : pad ? "D-pad the offers \xC2\xB7 A enchant \xC2\xB7 X set down / take back \xC2\xB7 Y the lapis back \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : "Click an offer to enchant \xC2\xB7 click the table to set down what's in hand (empty hand: take it back) \xC2\xB7 right click: take it back");
    if (ImGui::Button("Lapis back")) enchWorkTake(*s->level, p, w, at, 1);
    ImGui::SameLine();
    if (ImGui::Button("Enchanting screen")) {
      const ivec3 pos = at;
      uiDefer([pos](App& a2) { if (a2.session && a2.session->player) a2.events.emit("screenOpen", json::array({a2.session->player->name, "station", json{{"kind", "enchant"}, {"pos", {pos.x, pos.y, pos.z}}, {"classic", true}}})); });
      a.popScreen(); ImGui::End(); return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; int offer = 1, pick = 0; bool pad = false, showBag = true, applied = false;
  BagPad bag;
  void choose(App& a, int k) {
    GameSession* s = a.session.get();
    if (!s || g_ritualAt >= 0 || scene().phase != 2 || k < 0 || k > 2) return;
    std::array<EnchOffer, 3> offers{};
    if (!enchWorkOffers(s->world(), at, *s->player, offers)) return;
    const std::vector<ItemStack> v = enchWorkItems(s->world(), at);
    Player& p = *s->player;
    if (!p.creative() && (p.level < offers[k].level || v[1].id != "lapis_lazuli" || v[1].n < offers[k].cost)) {
      toast(*s->level, p, p.level < offers[k].level ? "You need level " + std::to_string(offers[k].level) : "That takes " + std::to_string(offers[k].cost) + " lapis", "warn");
      return;
    }
    pick = k; applied = false; g_ritualAt = scene().clock;
  }
};
}  // namespace

namespace {
// the brewing stand as a scene (Cinematic Interactions' BrewingStation): the three bottles on its feet, the ingredient in
// the bowl, the powder at its foot, each a spot to set down or take back; the brew bubbling on as ever
class CinematicBrewScreen : public Screen {
public:
  explicit CinematicBrewScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_brew"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 3;
    leanPose(S, p.pos + vec3(0, p.eye, 0));
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool here = reg().block(w.getBlock(at.x, at.y, at.z)).key == "brewing_stand";
    const bool walk = sceneWalkOff(in);
    if (!here || walk || p.dead || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f) { a.popScreen(); return; }
    if (in.pressed(Act::Inventory) && !in.pressed(Act::MenuY)) showBag = !showBag;
    for (int k = 0; k < 9; k++) if (in.pressed((Act)((int)Act::Hot1 + k))) p.selected = k;
    if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && !(ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook())) p.selected = ((p.selected - (wh > 0 ? 1 : -1)) % 9 + 9) % 9;
    if (bagPadUpdate(in, p, bag)) return;
    const ivec2 nav = in.navStep();
    if (nav.x || nav.y) { pad = true; spot = ((spot < 0 ? 0 : spot) + (nav.x ? nav.x : nav.y) + 5) % 5; }
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
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    auto project = [&](vec3 wp, ImVec2& out) { vec4 q = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1); if (q.w <= 0.01f) return false; out = ImVec2((q.x / q.w * 0.5f + 0.5f) * W, (1 - (q.y / q.w * 0.5f + 0.5f)) * H); return true; };
    // (on the stand's own pads, data/shapes.json: their tops 2 px up, centred 12.5,8  3.5,4  3.5,12; the bowl at the top of the rod)
    static const vec3 SPOT[5] = {{12.5f / 16, 2.5f / 16, 8 / 16.f}, {3.5f / 16, 2.5f / 16, 4 / 16.f}, {3.5f / 16, 2.5f / 16, 12 / 16.f}, {0.5f, 14.5f / 16, 0.5f}, {11 / 16.f, 1.5f / 16.f, 13 / 16.f}};
    static const char* NAMES[5] = {"a bottle", "a bottle", "a bottle", "the ingredient", "cinder powder"};
    const ImVec2 mp = ImGui::GetIO().MousePos;
    int hover = pad ? spot : -1; float best = 40 * z;
    ImVec2 sp[5]; bool vis[5];
    for (int k = 0; k < 5; k++) {
      vis[k] = project(vec3(at) + SPOT[k], sp[k]);
      if (!pad && !overUi && vis[k]) { const float d = std::hypot(mp.x - sp[k].x, mp.y - sp[k].y); if (d < best) { best = d; hover = k; } }
    }
    if (scene().phase == 2 && hover >= 0 && vis[hover]) dl->AddCircle(sp[hover], 26 * z, IM_COL32(255, 236, 160, 220), 24, 2 * z);
    if (!pad && !overUi && hover >= 0 && scene().phase == 2) {
      if (ImGui::IsMouseClicked(0)) act(a, hover, p.held().empty());
      else if (ImGui::IsMouseClicked(1)) act(a, hover, true);
    }
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    json* ep = w.blockEntity(at.x, at.y, at.z);
    const bool rec = ep && ep->is_object() && ep->value("type", "") == "brew";
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_brew", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("In hand: %s   \xC2\xB7   %s", nameOf(p.held()).c_str(), hover >= 0 ? (std::string("Here: ") + NAMES[hover]).c_str() : "");
    ImGui::TextUnformatted(rec ? brewStatus(*ep).c_str() : "Stand bottles on its feet, an ingredient in the bowl, cinder powder at its foot");
    if (rec) { ImGui::ProgressBar(brewProgress(*ep), ImVec2(220 * z, 8 * z), ""); ImGui::SameLine(); ImGui::ProgressBar(brewFuel(*ep), ImVec2(80 * z, 8 * z), ""); }
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the stand"
                      : pad ? "D-pad the spots \xC2\xB7 A set it down \xC2\xB7 X take back \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : "Click a spot: set down what's in hand (empty hand: take it back) \xC2\xB7 right click: take back");
    if (ImGui::Button("Brewing screen")) {
      const ivec3 pos = at;
      uiDefer([pos](App& a2) { if (a2.session && a2.session->player) a2.pushScreen(makeBrewScreen(pos)); });
      a.popScreen(); ImGui::End(); return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; int spot = -1; bool pad = false, showBag = true;
  BagPad bag;
  void act(App& a, int k, bool take) {
    GameSession* s = a.session.get();
    if (!s || scene().phase != 2) return;
    cinematicTap();
    if (take) brewTake(*s->level, *s->player, s->world(), at, k);
    else if (!brewPut(*s->level, *s->player, s->world(), at, k, s->player->held())) toast(*s->level, *s->player, "That doesn't go there");
  }
};
}  // namespace
std::unique_ptr<Screen> makeCinematicBrewScreen(ivec3 at) { return std::make_unique<CinematicBrewScreen>(at); }

namespace {
// a furnace as a scene (Cinematic Interactions' FurnaceStation): crouched before it, what smelts into its mouth, fuel
// into the grate under it, the smelted taken off its top; the two gauges (cooking, fuel) on its front
class CinematicFurnaceScreen : public Screen {
public:
  explicit CinematicFurnaceScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_furnace"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 4;
    static const vec3 FRONT[4] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};   // meta & 3 -> its front (render/mesher.cpp FACING_FACE)
    S.front = FRONT[a.session->world().getMeta(at.x, at.y, at.z) & 3];
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    S.front = vec3(0);
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool here = bhas(w.getBlock(at.x, at.y, at.z), BF::Furnace);
    const bool walk = sceneWalkOff(in);
    if (!here || walk || p.dead || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f) { a.popScreen(); return; }
    if (in.pressed(Act::Inventory) && !in.pressed(Act::MenuY)) showBag = !showBag;
    for (int k = 0; k < 9; k++) if (in.pressed((Act)((int)Act::Hot1 + k))) p.selected = k;
    if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && !(ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook())) p.selected = ((p.selected - (wh > 0 ? 1 : -1)) % 9 + 9) % 9;
    if (bagPadUpdate(in, p, bag)) return;
    const ivec2 nav = in.navStep();
    if (nav.y) { pad = true; spot = std::clamp((spot < 0 ? 0 : spot) + (nav.y > 0 ? 1 : -1), 0, 2); }   // top, mouth, grate: up and down
    if (pad && spot >= 0 && in.pressed(Act::MenuOk)) act(a, ZONE[spot], false);
    if (pad && spot >= 0 && in.pressed(Act::MenuX)) act(a, ZONE[spot], true);
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world(); Camera& cam = s->frame.cam;
    const float W = (float)a.width, H = (float)a.height, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook());
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    auto project = [&](vec3 wp, ImVec2& out) { vec4 q = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1); if (q.w <= 0.01f) return false; out = ImVec2((q.x / q.w * 0.5f + 0.5f) * W, (1 - (q.y / q.w * 0.5f + 0.5f)) * H); return true; };
    const vec3 side(std::sin(scene().viewYaw), 0, std::cos(scene().viewYaw));
    const vec3 face = vec3(at) + vec3(0.5f) + side * 0.5f;
    const vec3 SPOT[3] = {vec3(at) + vec3(0.5f, 1.02f, 0.5f), face + vec3(0, 0.12f, 0), face + vec3(0, -0.28f, 0)};   // the top, the mouth, the grate
    const std::vector<ItemStack> v = furnaceItems(w, at);
    const ItemStack* shown[3] = {&v[2], &v[0], &v[1]};
    const ImVec2 mp = ImGui::GetIO().MousePos;
    int hover = pad ? spot : -1; float best = 46 * z;
    for (int k = 0; k < 3; k++) {
      ImVec2 sp; if (!project(SPOT[k], sp)) continue;
      if (!pad && !overUi) { const float d = std::hypot(mp.x - sp.x, mp.y - sp.y); if (d < best) { best = d; hover = k; } }
      const float is = 34 * z;
      dl->AddRectFilled(ImVec2(sp.x - is / 2 - 3, sp.y - is / 2 - 3), ImVec2(sp.x + is / 2 + 3, sp.y + is / 2 + 3), IM_COL32(20, 16, 12, 150), 4 * z);
      if (!shown[k]->empty()) ui::drawItem(dl, *shown[k], ImVec2(sp.x - is / 2, sp.y - is / 2), is);
    }
    for (int k = 0; k < 3; k++) if (k == hover && scene().phase == 2) { ImVec2 sp; if (project(SPOT[k], sp)) dl->AddRect(ImVec2(sp.x - 22 * z, sp.y - 22 * z), ImVec2(sp.x + 22 * z, sp.y + 22 * z), IM_COL32(255, 236, 160, 230), 4 * z, 0, 2 * z); }
    {   // the gauges burnt into the front: the cooking (orange) over the fuel (red)
      ImVec2 g0, g1;
      const vec3 rgt(side.z, 0, -side.x);
      if (project(face + vec3(0, -0.05f, 0) - rgt * 0.3f, g0) && project(face + vec3(0, -0.05f, 0) + rgt * 0.3f, g1)) {
        const float ck = furnaceCook(w, at), fu = furnaceBurn(w, at);
        dl->AddLine(g0, g1, IM_COL32(40, 30, 20, 200), 4 * z);
        dl->AddLine(g0, ImVec2(g0.x + (g1.x - g0.x) * ck, g0.y + (g1.y - g0.y) * ck), IM_COL32(255, 170, 60, 255), 4 * z);
        dl->AddLine(ImVec2(g0.x, g0.y + 7 * z), ImVec2(g1.x, g1.y + 7 * z), IM_COL32(40, 30, 20, 200), 4 * z);
        dl->AddLine(ImVec2(g0.x, g0.y + 7 * z), ImVec2(g0.x + (g1.x - g0.x) * fu, g0.y + 7 * z + (g1.y - g0.y) * fu), IM_COL32(255, 80, 40, 255), 4 * z);
      }
    }
    if (!pad && !overUi && hover >= 0 && scene().phase == 2) {
      if (ImGui::IsMouseClicked(0)) act(a, ZONE[hover], p.held().empty() || hover == 0);
      else if (ImGui::IsMouseClicked(1)) act(a, ZONE[hover], true);
    }
    spot = hover >= 0 ? hover : spot;
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    static const char* NAMES[3] = {"the smelted (take it)", "its mouth: what smelts", "the grate: fuel"};
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_furnace", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("In hand: %s   \xC2\xB7   Smelting: %s   \xC2\xB7   Fuel: %s   \xC2\xB7   Done: %s", nameOf(p.held()).c_str(), nameOf(v[0]).c_str(), nameOf(v[1]).c_str(), nameOf(v[2]).c_str());
    if (hover >= 0) ImGui::TextDisabled("%s", NAMES[hover]);
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the furnace"
                      : pad ? "D-pad up / down the spots \xC2\xB7 A put in \xC2\xB7 X take out \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : "Click the mouth or the grate: put in what's in hand (empty hand: take it out) \xC2\xB7 click the top: take the smelted");
    if (ImGui::Button("Furnace screen")) {
      const ivec3 pos = at; const std::string fk = binfo(w.getBlock(at.x, at.y, at.z)).furnace;
      uiDefer([pos, fk](App& a2) { if (a2.session && a2.session->player) a2.events.emit("screenOpen", json::array({a2.session->player->name, "furnace", json{{"kind", fk}, {"pos", {pos.x, pos.y, pos.z}}, {"classic", true}}})); });
      a.popScreen(); ImGui::End(); return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  static constexpr int ZONE[3] = {2, 0, 1};   // the spots' slots: the top the output, the mouth the input, the grate the fuel
  ivec3 at; int spot = -1; bool pad = false, showBag = true;
  BagPad bag;
  void act(App& a, int slot, bool take) {
    GameSession* s = a.session.get();
    if (!s || scene().phase != 2) return;
    cinematicTap();
    if (take || slot == 2) furnaceTake(*s->level, *s->player, s->world(), at, slot);
    else furnacePut(*s->level, *s->player, s->world(), at, slot, s->player->held());
  }
};
}  // namespace
std::unique_ptr<Screen> makeCinematicFurnaceScreen(ivec3 at) { return std::make_unique<CinematicFurnaceScreen>(at); }

namespace { bool g_aimPtOn = false; vec3 g_aimPt{0}; }
void cinematicSetAimPoint(bool on, vec3 p) { g_aimPtOn = on; g_aimPt = p; }
bool cinematicAimPoint(vec3& p) { if (!g_aimPtOn || scene().phase != 2) return false; p = g_aimPt; return true; }

namespace {
// a chest worked by hand (after Immersive Craft's chest): the lid swings up in the world, what's in it lies inside in its
// rows of nine (a large chest's two halves side by side), and you put in what's in your hand, or take it back out
class CinematicChestScreen : public Screen {
public:
  explicit CinematicChestScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_chest"; }
  bool pausesGame() const override { return false; }
  bool showsWorld() const override { return true; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 5;
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    chestScreenOpened(at, true); opened = true;   // (render/chest_layer.cpp: the lid up, both halves of a large chest)
  }
  void onClose(App& a) override {
    Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; }
    cinematicSetAimPoint(false);
    if (opened) { chestScreenOpened(at, false); opened = false; if (a.audio) a.audio->playAt("chest_close", vec3(at) + vec3(0.5f)); }
  }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    ivec3 hs[2]; nh = chestHalves(w, at, hs); halves[0] = hs[0]; halves[1] = hs[1];
    per = nh ? chestSlotsPerHalf(w, at) : 0;
    const bool walk = sceneWalkOff(in);
    if (!nh || walk || p.dead || glm::distance(p.pos, vec3(at) + vec3(0.5f)) > 5.f) { DQ_LOG("chest scene: left (%d %d %d)", nh, (int)walk, (int)p.dead); a.popScreen(); return; }
    if (in.pressed(Act::Inventory) && !in.pressed(Act::MenuY)) showBag = !showBag;
    for (int k = 0; k < 9; k++) if (in.pressed((Act)((int)Act::Hot1 + k))) p.selected = k;
    if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && !(ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook())) p.selected = ((p.selected - (wh > 0 ? 1 : -1)) % 9 + 9) % 9;
    if (bagPadUpdate(in, p, bag)) return;
    // the pad: the d-pad walks the slots as you see them (left / right across both halves, up / down the rows)
    const ivec2 nav = in.navStep();
    if (nav != ivec2(0)) {
      pad = true;
      if (cursor < 0) cursor = 0;
      int h = cursor / per, i = cursor % per, c = i % 9, r = i / 9;
      const int rows = std::max(1, (per + 8) / 9);
      // which half is on your right (the slots' columns run left to right as you face the chest)
      const vec3 right(std::cos(s->frame.cam.yaw), 0, -std::sin(s->frame.cam.yaw));
      const int rightHalf = nh == 2 && glm::dot(vec3(halves[1] - halves[0]), right) < 0 ? 0 : 1;
      if (nav.x) {
        // the column as you see it: the chest's own columns run to the right of someone facing its front
        const vec3 c0 = chestSlotCenter(w, at, h * per + r * 9), c8 = chestSlotCenter(w, at, h * per + r * 9 + 8);
        const int step = glm::dot(c8 - c0, right) >= 0 ? nav.x : -nav.x;
        c += step;
        if (c > 8 || c < 0) {
          if (nh == 2 && ((nav.x > 0) == (h != rightHalf))) { h ^= 1; c = c > 8 ? 0 : 8; } else c = std::clamp(c, 0, 8);
        }
      }
      if (nav.y) {
        const vec3 d = chestSlotCenter(w, at, h * per + std::min(per - 1, 9 + c)) - chestSlotCenter(w, at, h * per + c);   // a row on (toward its front)
        const vec3 fwd(-std::sin(s->frame.cam.yaw), 0, -std::cos(s->frame.cam.yaw));
        r = std::clamp(r + (glm::dot(d, fwd) < 0 ? nav.y : -nav.y), 0, rows - 1);   // (down on the pad: toward you)
      }
      cursor = h * per + std::min(per - 1, r * 9 + c);
    }
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    if (pad && cursor >= 0) {
      if (in.pressed(Act::MenuOk)) act(a, cursor, false, false);
      if (in.pressed(Act::MenuX)) act(a, cursor, true, false);
      if (in.pressed(Act::MenuY)) act(a, cursor, false, true);
    }
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player || !nh) return;
    Player& p = *s->player; World& w = s->world(); Camera& cam = s->frame.cam;
    const float W = (float)a.width, H = (float)a.height, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook());
    auto project = [&](vec3 wp, ImVec2& out) { vec4 q = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1); if (q.w <= 0.01f) return false; out = ImVec2((q.x / q.w * 0.5f + 0.5f) * W, (1 - (q.y / q.w * 0.5f + 0.5f)) * H); return true; };
    const std::vector<ItemStack> items = chestItems(*s->level, p, w, at);
    // the slot under the mouse: the ray through it onto the chest's floor
    int hover = -1;
    if (!pad && !overUi && scene().phase == 2) {
      const ImVec2 m = ImGui::GetIO().MousePos;
      const vec2 ndc(m.x / W * 2 - 1, 1 - m.y / H * 2);
      const mat4 inv = glm::inverse(cam.viewProj);
      const vec4 n0 = inv * vec4(ndc, -1, 1), n1 = inv * vec4(ndc, 1, 1);
      const vec3 a0 = vec3(n0) / n0.w, a1 = vec3(n1) / n1.w;
      const float floorY = (float)(at.y + chestFloorY(w, at) - cam.origin.y);
      if (std::fabs(a1.y - a0.y) > 1e-5f) {
        const float u = (floorY - a0.y) / (a1.y - a0.y);
        if (u > 0) hover = chestSlotAt(w, at, vec3(glm::dvec3(a0 + (a1 - a0) * u) + cam.origin));
      }
    }
    const int shown = pad ? cursor : hover;
    if (scene().phase == 2 && shown >= 0) cinematicSetAimPoint(true, chestSlotCenter(w, at, shown)); else cinematicSetAimPoint(false);
    // the slots: a faint outline on each in the dark floor, the one aimed at lit; counts on the stacks
    const float cs = chestSlotSize(w, at);
    for (int k = 0; k < (int)items.size(); k++) {
      const vec3 c = chestSlotCenter(w, at, k) + vec3(0, 0.004f, 0);
      ivec3 half = halves[k / per];
      static const vec3 FRONT[4] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};
      const vec3 d = FRONT[w.getMeta(half.x, half.y, half.z) & 3], r(d.z, 0, -d.x);
      const float hw = cs * 0.46f, hd = (1 - 2 * 0.07f) / std::max(1, (per + 8) / 9) * 0.46f;
      ImVec2 q[4];
      if (!project(c - r * hw - d * hd, q[0]) || !project(c + r * hw - d * hd, q[1]) || !project(c + r * hw + d * hd, q[2]) || !project(c - r * hw + d * hd, q[3])) continue;
      { ImVec2 mid; if (project(c, mid) && bookUiPointOverBook(vec2(mid.x, mid.y))) continue; }   // (behind the journal in your off hand)
      if (k == shown && scene().phase == 2) {
        dl->AddQuadFilled(q[0], q[1], q[2], q[3], IM_COL32(255, 236, 160, 50));
        dl->AddQuad(q[0], q[1], q[2], q[3], IM_COL32(255, 236, 160, 230), 2 * z);
      } else if (scene().phase == 2 && s->viewMode == 0) dl->AddQuad(q[0], q[1], q[2], q[3], IM_COL32(255, 255, 255, 22), 1);
      if (items[k].n > 1 && s->viewMode == 0) {   // (third person: over your own body, so first person only)   // (the count, small, at the slot's front corner)
        ImVec2 tp; if (project(c + r * hw * 0.6f + d * hd * 0.7f, tp)) {
          const std::string n = std::to_string(items[k].n);
          ImGui::PushFont(ui::font(ui::F_SMALL));
          const ImVec2 ts = ImGui::CalcTextSize(n.c_str());
          dl->AddText(ImVec2(tp.x - ts.x * 0.5f + 1, tp.y - ts.y * 0.5f + 1), IM_COL32(0, 0, 0, 200), n.c_str());
          dl->AddText(ImVec2(tp.x - ts.x * 0.5f, tp.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 235), n.c_str());
          ImGui::PopFont();
        }
      }
    }
    if (!pad && !overUi && hover >= 0 && scene().phase == 2) {
      const bool shift = ImGui::GetIO().KeyShift;
      if (ImGui::IsMouseClicked(0)) act(a, hover, false, shift);
      else if (ImGui::IsMouseClicked(1)) act(a, hover, true, false);
    }
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    // the panel: what's in hand, what's aimed at, the ways out
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_chest", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    const ItemStack none;
    ImGui::Text("%s   \xC2\xB7   In hand: %s   \xC2\xB7   Here: %s", nh == 2 ? "Large Chest" : reg().block(w.getBlock(at.x, at.y, at.z)).name.c_str(),
                nameOf(p.held()).c_str(), nameOf(shown >= 0 && shown < (int)items.size() ? items[shown] : none).c_str());
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the chest"
                      : pad ? "D-pad the slots \xC2\xB7 A put in / take \xC2\xB7 X one \xC2\xB7 Y to your bag \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : "Click: put in what's in hand (empty hand: take it) \xC2\xB7 Right click: just one \xC2\xB7 Shift-click: to your bag \xC2\xB7 wheel / 1-9: what's in hand");
    if (ImGui::Button("Take all")) { cinematicTap(); chestTakeAll(*s->level, p, w, at); }
    ImGui::SameLine();
    if (ImGui::Button("Store all")) { cinematicTap(); chestStoreAll(*s->level, p, w, at); }
    ImGui::SameLine();
    if (ImGui::Button("Chest screen")) {   // the container pages in the journal, as ever
      DQ_LOG("chest scene: to the chest screen");
      const ivec3 pos = at;
      json o{{"pos", {pos.x, pos.y, pos.z}}, {"classic", true}};
      const BlockDef& bd = reg().block(w.getBlock(at.x, at.y, at.z));
      if (bd.has("enderChest")) o["ender"] = true; else if (bd.has("partyChest")) o["party"] = true;
      uiDefer([o](App& a2) { if (a2.session && a2.session->player) a2.events.emit("screenOpen", json::array({a2.session->player->name, "container", o})); });
      a.popScreen(); ImGui::End(); return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; ivec3 halves[2]{}; int nh = 0, per = 27;
  int cursor = -1; bool pad = false, showBag = true, opened = false;
  BagPad bag;
  // the hand into a slot: put in what's in it (empty hand: take the stack); one: just one; toBag: the stack to your bag
  void act(App& a, int slot, bool one, bool toBag) {
    GameSession* s = a.session.get();
    if (!s || !s->player || scene().phase != 2) return;
    Player& p = *s->player; World& w = s->world();
    cinematicTap();
    if (toBag) { chestTake(*s->level, p, w, at, slot, false, true); return; }
    if (!p.held().empty()) chestPut(*s->level, p, w, at, slot, one);
    else chestTake(*s->level, p, w, at, slot, one, false);
  }
};
}  // namespace
std::unique_ptr<Screen> makeCinematicChestScreen(ivec3 at) { return std::make_unique<CinematicChestScreen>(at); }

double cinematicClock() { return scene().clock; }
vec3 cinematicOfferPos(ivec3 at, float viewYaw, int k, double clock) {   // EnchantStation: OFFER_HIGH 0.92 the middle, OFFER_LOW 0.74 and OFFER_SPREAD 0.4 the sides
  const vec3 top = vec3(at) + vec3(0.5f, 0.75f, 0.5f);
  const vec3 side(std::sin(viewYaw), 0, std::cos(viewYaw)), right(std::cos(viewYaw), 0, -std::sin(viewYaw));
  const vec3 c = k == 1 ? top + vec3(0, 0.92f, 0) - side * 0.12f : top + vec3(0, 0.74f, 0) + right * (k == 0 ? -0.4f : 0.4f) + side * 0.02f;
  return c + vec3(0, 0.02f * std::sin((float)clock * 1.3f + k * 2.1f), 0);
}
bool cinematicEnchantView(EnchantView& v) { if (!g_evOn || scene().phase == 0 || scene().kind != 2) return false; v = g_ev; return true; }
bool cinematicAnvilView(AnvilView& v) { if (!g_avOn || scene().phase == 0 || scene().kind != 1) return false; v = g_av; return true; }
double cinematicRitualT() { return g_ritualAt < 0 ? -1 : scene().clock - g_ritualAt; }
std::unique_ptr<Screen> makeCinematicEnchantScreen(ivec3 at) { return std::make_unique<CinematicEnchantScreen>(at); }

double cinematicForgeT() { return g_forgeAt < 0 ? -1 : scene().clock - g_forgeAt; }
namespace { double g_forgeAtShared() { return g_forgeAt; } }
int cinematicKind() { return scene().phase == 0 ? -1 : scene().kind; }
std::unique_ptr<Screen> makeCinematicAnvilScreen(ivec3 at) { return std::make_unique<CinematicAnvilScreen>(at); }

std::unique_ptr<Screen> makeCinematicStationScreen(ivec3 at, int kind, std::function<std::unique_ptr<Screen>()> make) {
  return std::make_unique<CinematicStationScreen>(at, kind, std::move(make));
}

void cinematicCamera(GameSession& s, Camera& cam, double dt) {
  Scene& S = scene();
  if (S.phase == 0) return;
  S.t += dt; S.clock += dt;
  S.lean = s.app.settings.extra.value("cinematicLean", false);
  if (!S.lean) {   // seamless first person (the default): your own view; your body bends to the work and the eye goes with it
    if (S.phase == 1 && S.t >= 0.35) { S.phase = 2; S.t = 0; }
    else if (S.phase == 3 && S.t >= 0.35) S.phase = 0;
    const float want = S.phase == 3 || S.phase == 0 ? 0.f : 1.f;
    S.bend += (want - S.bend) * (float)std::min(1.0, dt * 6.0);
    const float b = S.bend * S.bend * (3 - 2 * S.bend);
    const bool low = S.kind == 3 || S.kind == 4 || S.kind == 7;   // the brewing stand, the furnace, the stonecutter: crouched down to them (the mod's LOW lean)
    glm::dvec3 to = glm::dvec3(S.at) + glm::dvec3(0.5, 0, 0.5) - cam.pos; to.y = 0;
    const double tl = glm::length(to); if (tl > 1e-4) to /= tl;
    S.viewOff = vec3(glm::dvec3(0, -(low ? 0.34 : 0.0) * b, 0) + to * (0.14 * b));
    cam.pos += glm::dvec3(S.viewOff);   // (as low as a crouch, a lean in toward it)
    S.lastPos = cam.pos; S.lastYaw = cam.yaw; S.lastPitch = cam.pitch;
    return;
  }
  const glm::dvec3 live = cam.pos; const float liveYaw = cam.yaw, livePitch = cam.pitch;
  glm::dvec3 pos; float yaw, pitch;
  // breathing at the lean: a slow rise and fall, a little drift
  const double br = std::sin(S.clock * 1.3) * 0.0035;
  const float by = (float)(std::sin(S.clock * 0.61) * 0.0028), bp = (float)(std::sin(S.clock * 0.83 + 1.1) * 0.0023);
  if (S.phase == 1) {
    const double u = S.t / INTRO, e = inOutCubic(u), er = inOutCubic(u / 0.82);
    const double hump = std::sin(clampv(u, 0.0, 1.0) * PI) * 0.045;
    pos = live + (S.viewPos - live) * e + glm::dvec3(0, hump, 0);
    yaw = liveYaw + wrapPi(S.viewYaw - liveYaw) * (float)er; pitch = livePitch + (S.viewPitch - livePitch) * (float)er;
    if (u >= 1) { S.phase = 2; S.t = 0; }
  } else if (S.phase == 2) {
    pos = S.viewPos + glm::dvec3(0, br, 0); yaw = S.viewYaw + by; pitch = S.viewPitch + bp;
    if (S.kind == 1 && g_forgeAtShared() >= 0) {   // a blow lands: the tremor dies down with it (CameraRig.kick)
      const double t = S.clock - g_forgeAtShared(); double kick = 0;
      for (double b : {0.7, 1.02, 1.3}) if (t >= b) kick += std::exp(-(t - b) * 14) * (b == 0.7 ? 1.0 : 0.6);
      const double c = S.clock;
      yaw += (float)((std::sin(c * 47) + std::sin(c * 31 + 1)) * 0.18 * kick * PI / 180);
      pitch += (float)((std::sin(c * 53 + 2) + std::sin(c * 29)) * 0.14 * kick * PI / 180);
    }
  } else {
    const double u = S.t / OUTRO, e = inOutCubic((u - 0.1) / 0.9), er = inOutCubic((u - 0.15) / 0.85);
    pos = S.lastPos + (live - S.lastPos) * e;
    yaw = S.lastYaw + wrapPi(liveYaw - S.lastYaw) * (float)er; pitch = S.lastPitch + (livePitch - S.lastPitch) * (float)er;
    if (u >= 1) { S.phase = 0; return; }
  }
  if (S.phase != 3) { S.lastPos = pos; S.lastYaw = yaw; S.lastPitch = pitch; }
  const float k = S.phase == 2 ? 1.f : S.phase == 1 ? (float)inOutCubic(S.t / INTRO) : 1.f - (float)inOutCubic(S.t / OUTRO);
  cam.pos = pos; cam.yaw = yaw; cam.pitch = pitch; cam.roll = S.roll * k;
  cam.fov = cam.fov + (S.fov - cam.fov) * k;   // (its leanFov: 66 degrees at the lean, 68 at the enchanting table)
}

std::unique_ptr<Screen> makeCinematicTableScreen(ivec3 at) { return std::make_unique<CinematicTableScreen>(at); }

} // namespace dq
