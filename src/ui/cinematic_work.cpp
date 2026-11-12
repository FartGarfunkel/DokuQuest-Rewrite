// ui/cinematic_work.cpp — the stations worked with things lying on them, as scenes (ui/cinematic.h): the grindstone,
// smithing table, loom, fletching and cartography tables (sim/station_work.h pairWork*) and the stonecutter. After
// Cinematic Interactions' GrindstoneStation, SmithingStation, LoomStation, CartographyStation and StonecutterStation
// (MIT): the leans, the layouts and the beats; ours lays the pieces on the block itself (synced like any block entity).
#include "ui/cinematic.h"
#include "ui/cinematic_internal.h"
#include "ui/book_ui.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/ui.h"
#include "client/app.h"
#include "client/session.h"
#include "render/camera.h"
#include "sim/station_work.h"
#include "sim/stations.h"
#include "sim/maps.h"
#include "audio/audio.h"
#include "sim/player.h"
#include "core/registry.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace dq {
using namespace cine;

namespace {
PairView g_pv; CutView g_cv; JukeView g_jv; FireView g_fv; bool g_pvOn = false, g_cvOn = false, g_jvOn = false, g_fvOn = false;
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

PairBeats pairBeats(int kind) {
  switch (kind) {
    case 6: return {2.4, 0.6, 1.9};
    case 8: return {2.6, 0.95, SMITH_COOLED};
    case 9: return {2.4, LOOM_FROM, LOOM_TO};
    case 10: return {1.8, 0.6, 1.4};
    case 11: return {2.1, 0.6, 1.55};
    default: return {1.0, 0.5, 0.8};
  }
}
int pairSceneKind(const std::string& st) {
  return st == "grindstone" ? 6 : st == "smithing" ? 8 : st == "loom" ? 9 : st == "fletching" ? 10 : st == "cartography" ? 11 : 0;
}

vec3 pairHeldPos(const PairView& v, int k, const vec3 lying[2]) {
  const double t = v.t;
  const vec3 d = sideOf(v.viewYaw), right = rightOf(v.viewYaw);
  const vec3 top = vec3(v.at) + vec3(0.5f, 1.f, 0.5f);
  auto mixv = [](vec3 a, vec3 b, double u) { return a + (b - a) * (float)u; };
  switch (v.kind) {
    case 6: {   // GrindstoneStation.heldPos: lifted to the wheel by 0.6, stroked to and fro (STROKE 0.433 s each way) till 1.9
      const PairBeats B = pairBeats(6);
      const double s = win(t, B.contact, B.letGo), fade = smoother(s * 8) * smoother((1 - s) * 8);
      const double stroke = 0.075 * std::sin(PI * (t - B.contact) / 0.433) * fade;
      const float sideways = (float)((k == 0 ? 0.08 : -0.08) + stroke * (k == 0 ? 1 : -1));
      const vec3 grinding = top + d * 0.04f + vec3(0, 0.03f, 0) + right * sideways;
      const float lift = (float)inOutCubic(win(t, 0.28, B.contact));
      vec3 p = lying[k] + vec3(0, 0.008f, 0) + (grinding - lying[k]) * lift + vec3(0, 0.09f * std::sin(lift * PI), 0);
      p.y -= 0.008f * std::fabs(std::sin(PI * (t - B.contact) / 0.433)) * (float)win(t, B.contact, 0.7);
      return p;
    }
    case 8: {   // SmithingStation: the material up (MATERIAL_UP 0.5), over the gear and down onto it (MATERIAL_DOWN 0.76, ON_GEAR)
      if (k == 0) return lying[0];
      const vec3 up = lying[1] + vec3(0, 0.12f, 0), onGear = lying[0] + vec3(0, 0.025f, 0), over = onGear + vec3(0, 0.12f, 0);
      if (t < 0.5) return mixv(lying[1], up, smoother(win(t, 0.1, 0.5)));
      if (t < 0.66) return mixv(up, over, smoother(win(t, 0.5, 0.66)));
      return mixv(over, onGear, smoother(win(t, 0.66, 0.76)));
    }
    case 9: {   // LoomStation: the dye taken up as the shuttle, PASSES across the cloth between WEAVE_FROM and WEAVE_TO
      if (k == 0) return lying[0];
      const vec3 lane = lying[0] + vec3(0, 0.07f, 0) + d * 0.02f;
      if (t < LOOM_FROM) return mixv(lying[1], lane - right * 0.3f, smoother(win(t, 0, LOOM_FROM)));
      const double u = win(t, LOOM_FROM, LOOM_TO) * LOOM_PASSES;
      const int pass = std::min(LOOM_PASSES - 1, (int)u);
      const float f = (float)smoother(u - pass), x = pass % 2 == 0 ? -0.3f + 0.6f * f : 0.3f - 0.6f * f;
      return lane + right * x + vec3(0, 0.02f * std::sin(f * PI), 0);
    }
    case 10: {   // the fletching table: flint and feather brought together over its middle by 0.6
      const vec3 mid = (lying[0] + lying[1]) * 0.5f + vec3(0, 0.05f, 0) + right * (k == 0 ? -0.03f : 0.03f);
      const double u = smoother(win(t, 0.15, 0.6));
      return mixv(lying[k], mid, u) + vec3(0, 0.07f * (float)std::sin(u * PI), 0);
    }
    case 11: {   // CartographyStation: the paper (or the empty maps) laid on the map by 0.6
      if (k == 0) return lying[0];
      const double u = smoother(win(t, 0.15, 0.6));
      return mixv(lying[1], lying[0] + vec3(0, 0.006f, 0), u) + vec3(0, 0.06f * (float)std::sin(u * PI), 0);
    }
  }
  return lying[k];
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
bool cinematicPairView(PairView& v) { if (!g_pvOn || scene().phase == 0) return false; v = g_pv; return true; }
bool cinematicCutView(CutView& v) { if (!g_cvOn || scene().phase == 0) return false; v = g_cv; return true; }
bool cinematicFireView(FireView& v) { if (!g_fvOn || scene().phase == 0) return false; v = g_fv; return true; }
bool cinematicJukeView(JukeView& v) { if (!g_jvOn || scene().phase == 0) return false; v = g_jv; return true; }
vec3 jukeDialPoint(ivec3 at, float viewYaw, int k, int count, float scroll, double clock) {
  const float a = dialAngle(k, count, scroll) * PI / 180.f;
  const float bob = 0.008f * std::sin((float)clock * 1.5f + k * 1.1f);
  const vec3 top = vec3(at) + vec3(0.5f, 1.f, 0.5f);
  return top - sideOf(viewYaw) * 0.05f + rightOf(viewYaw) * (std::cos(a) * 0.62f) + vec3(0, 0.12f + std::sin(a) * 0.42f + bob, 0);
}
vec3 jukeDiscPos(const JukeView& v, vec3 from) {
  const vec3 top = vec3(v.at) + vec3(0.5f, 1.f, 0.5f), playing = top + vec3(0, 0.06f, 0), over = top + vec3(0, 0.46f, 0);
  auto mixv = [](vec3 a, vec3 b, double u) { return a + (b - a) * (float)u; };
  if (!v.eject) {   // over the slot by 0.5, then down into it (JukeboxStation INSERT)
    if (v.t < 0.5) return mixv(from, over, smoother(win(v.t, 0, 0.5)));
    return mixv(over, playing, smoother(win(v.t, 0.5, JUKE_INSERT - 0.15)));
  }
  // out of the slot by 0.4 (EJECT), then off toward you
  const vec3 you = top + sideOf(v.viewYaw) * 0.7f + vec3(0, 0.3f, 0);
  if (v.t < 0.4) return mixv(playing, over, smoother(win(v.t, 0, 0.4)));
  return mixv(over, you, smoother(win(v.t, 0.4, JUKE_EJECT)));
}

namespace {
// the grindstone, smithing table, loom, fletching and cartography tables: what's worked and what goes into it lie on top.
// With something in hand, a click on the station sets it down where it belongs (all of a stack that goes in; one piece of
// gear); an empty hand on a piece does the work when there's something to make (else takes the piece back); right click
// takes back. The work (pairBeats): the pieces taken up, the thing made at contact and handed to you
class CinematicPairScreen : public Screen {
public:
  CinematicPairScreen(ivec3 at, std::string station) : at(at), st(std::move(station)), kind(pairSceneKind(st)) {}
  const char* id() const override { return "cinematic_pair"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = kind; S.front = vec3(0);
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    g_pv = PairView{}; g_pv.kind = kind; g_pv.at = at; g_pvOn = true;
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } g_pvOn = false; cinematicSetAimPoint(false); }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool here = pairStationAt(w, at) == st;
    if ((!here && workAt < 0) || !sceneKeys(a, at, workAt >= 0, showBag)) { a.popScreen(); return; }
    const bool inBag = bagPadUpdate(in, p, bag);
    if (workAt >= 0) tick(a, *s, p, w);
    if (inBag) return;
    if (in.pressed(Act::MenuY) || in.pressed(Act::MenuStart)) work(a);
    const ivec2 nav = in.navStep();
    if (nav.x) { pad = true; zone = nav.x > 0 ? 1 : 0; }
    if (pad && in.pressed(Act::MenuOk)) { if (!p.held().empty()) put(a, zone); else if (pairWorkResult(*s->level, w, at).ok) work(a); else take(a, zone); }
    if (pad && in.pressed(Act::MenuX)) take(a, zone);
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world();
    const float W = (float)a.width, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook();
    const std::vector<ItemStack> v = pairWorkItems(w, at);
    const StationResult r = pairWorkResult(*s->level, w, at);
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    const float topY = st == "grindstone" ? 0.9f : 0.75f;
    // what the mouse is on: a piece lying there, or the station (something in hand)
    int hover = -1; bool onStation = false;
    if (!pad && !overUi && scene().phase == 2 && workAt < 0) {
      for (int k = 0; k < 2 && hover < 0; k++) if (!v[k].empty() && under(a, pairLiePos(w, at, k), st == "cartography" && k == 0 ? 0.22f : 0.13f)) hover = k;
      onStation = under(a, vec3(at) + vec3(0.5f, topY, 0.5f), 0.45f);
    } else if (pad) hover = zone;
    const vec3 aimAt = hover >= 0 ? pairLiePos(w, at, hover) : vec3(at) + vec3(0.5f, 1.f, 0.5f);
    cinematicSetAimPoint(scene().phase == 2 && workAt < 0 && (hover >= 0 || onStation), aimAt);
    if (scene().phase == 2 && workAt < 0 && (hover >= 0 || (onStation && !p.held().empty()))) {
      ImVec2 sp; if (toScreen(a, aimAt, sp)) dl->AddCircle(sp, 30 * z, IM_COL32(255, 236, 160, 220), 28, 2 * z);
    }
    if (!overUi && !pad && scene().phase == 2 && workAt < 0) {
      if (ImGui::IsMouseClicked(0)) {
        if (!p.held().empty() && (onStation || hover >= 0)) put(a, hover >= 0 ? hover : -1);
        else if (hover >= 0) { if (r.ok) work(a); else take(a, hover); }
      } else if (ImGui::IsMouseClicked(1) && hover >= 0) take(a, hover);
    }
    g_pv.kind = kind; g_pv.at = at; g_pv.viewYaw = scene().viewYaw; g_pv.hover = hover; g_pv.t = workAt < 0 ? -1 : scene().clock - workAt;
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_pair", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("In hand: %s   \xC2\xB7   On the %s: %s, %s", nameOf(p.held()).c_str(), place(), nameOf(v[0]).c_str(), nameOf(v[1]).c_str());
    if (workAt >= 0) ImGui::Text("%s: %s", verbing(), nameOf(g_pv.after).c_str());
    else if (r.ok) ImGui::Text("Makes: %s%s%s", nameOf(r.out).c_str(), r.note.empty() ? "" : "   \xC2\xB7   ", r.note.c_str());
    else ImGui::TextDisabled("%s", v[0].empty() && v[1].empty() ? help() : "Nothing comes of those two");
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the station"
                      : pad ? "D-pad left / right a side \xC2\xB7 A set it down (empty hand: do it) \xC2\xB7 X take back \xC2\xB7 Y do it \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : "Left click the station: set down what's in hand \xC2\xB7 empty hand on a piece: do the work (or take it back) \xC2\xB7 Right click: take back");
    ImGui::BeginDisabled(!r.ok || workAt >= 0);
    if (ImGui::Button(verb())) work(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("The screen")) {
      const ivec3 pos = at; const std::string station = st;
      uiDefer([pos, station](App& a2) {
        if (!a2.session || !a2.session->player) return;
        if (station == "cartography") a2.events.emit("screenOpen", json::array({a2.session->player->name, "table", json{{"classic", true}}}));
        else a2.events.emit("screenOpen", json::array({a2.session->player->name, "station", json{{"kind", station}, {"pos", {pos.x, pos.y, pos.z}}, {"classic", true}}}));
      });
      a.popScreen(); ImGui::End(); return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; std::string st; int kind; int zone = 0, lastBeat = -1; bool pad = false, showBag = true, made = false, cooled = false; double workAt = -1;
  BagPad bag;
  const char* place() const { return st == "grindstone" ? "stone" : st == "loom" ? "loom" : "table"; }
  const char* verb() const { return st == "grindstone" ? "Grind" : st == "smithing" ? "Smith" : st == "loom" ? "Weave" : st == "fletching" ? "Fletch" : "Work the map"; }
  const char* verbing() const { return st == "grindstone" ? "Grinding" : st == "smithing" ? "Smithing" : st == "loom" ? "Weaving" : st == "fletching" ? "Fletching" : "Drawing"; }
  const char* help() const {
    return st == "grindstone" ? "Lay an enchanted piece on the stone (or two worn ones of a kind) to grind"
         : st == "smithing" ? "Lay gear in the middle and its upgrade beside it (diamond + cinderite shard, copper + tin)"
         : st == "loom" ? "Lay blocks to dye on the loom (up to eight a dye) and a dye at its corner"
         : st == "fletching" ? "Lay a flint and a feather on the table: six arrows"
         : "Lay a map on the table, and paper to zoom it out or empty maps to copy it";
  }
  void put(App& a, int slot) { GameSession* s = a.session.get(); if (!s || workAt >= 0 || scene().phase != 2) return; cinematicTap(); pairWorkPut(*s->level, *s->player, s->world(), at, slot, s->player->held()); }
  void take(App& a, int slot) { GameSession* s = a.session.get(); if (!s || workAt >= 0 || scene().phase != 2) return; cinematicTap(); pairWorkTake(*s->level, *s->player, s->world(), at, slot); }
  void work(App& a) {
    GameSession* s = a.session.get();
    if (!s || workAt >= 0 || scene().phase != 2) return;
    World& w = s->world();
    const StationResult r = pairWorkResult(*s->level, w, at);
    if (!r.ok) return;
    const std::vector<ItemStack> v = pairWorkItems(w, at);
    g_pv.before[0] = v[0]; g_pv.before[1] = v[1]; g_pv.after = r.out;
    workAt = scene().clock; made = false; cooled = false; lastBeat = -1;
    cinematicTap();
    if (a.audio) a.audio->play(st == "grindstone" ? "grind" : st == "cartography" ? "map_paper" : "click");
  }
  // the work going on: the thing made at contact; each station's sparks, sounds and smoke on its beats
  void tick(App& a, GameSession& s, Player& p, World& w) {
    const double t = scene().clock - workAt;
    const PairBeats B = pairBeats(kind);
    const vec3 c = vec3(at) + vec3(0.5f, 1.02f, 0.5f);
    if (!made && t >= B.contact) {
      made = true;
      ItemStack out;
      if (!pairWorkDo(*s.level, p, w, at, &out)) { workAt = -1; return; }
      g_pv.after = out;   // (a zoomed-out map has its own new map only now)
      if (s.level->fx && kind == 8) s.level->fx->particles("spark", pairLiePos(w, at, 0) + vec3(0, 0.05f, 0), 12);
      if (s.level->fx && kind == 10) s.level->fx->particles("poof", c, 4);
    }
    if (kind == 6 && t >= B.contact && t < B.letGo) {   // a shower of sparks off the wheel at each stroke's turn
      const int stroke = (int)((t - B.contact) / 0.433);
      if (stroke != lastBeat) {
        lastBeat = stroke;
        if (s.level->fx) s.level->fx->particles("spark", c + sideOf(scene().viewYaw) * 0.06f, 6);
        if (a.audio && stroke > 0) a.audio->play("grind");
        cinematicTap();
      }
    }
    if (kind == 9 && t >= LOOM_FROM && t < LOOM_TO) {   // the shuttle's knock at each pass
      const int pass = (int)(win(t, LOOM_FROM, LOOM_TO) * LOOM_PASSES);
      if (pass != lastBeat) { lastBeat = pass; if (a.audio) a.audio->play("click", 0.45f, 0.8f + 0.05f * pass); cinematicTap(); }
    }
    if (kind == 8 && !cooled && t >= SMITH_COOLED - 0.15) {   // quenched: a hiss and a breath of steam
      cooled = true;
      if (a.audio) a.audio->play("fizz", 0.6f);
      if (s.level->fx) s.level->fx->particles("smoke", pairLiePos(w, at, 0) + vec3(0, 0.08f, 0), 6);
    }
    if (t >= B.length) workAt = -1;
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

// the jukebox as a scene (JukeboxStation): your records float on a dial over it; click one (A on a pad) and it goes over
// the slot and down in, playing; click the jukebox (X) and the one playing rises out and comes back to your bag
class CinematicJukeboxScreen : public Screen {
public:
  explicit CinematicJukeboxScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_jukebox"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 12; S.front = vec3(0);
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    g_jv = JukeView{}; g_jv.at = at; g_jvOn = true;
    // a record in hand when you came: straight in
    if (const ItemDef* d = p.held().empty() ? nullptr : reg().item(p.held().id); d && d->has("disc") && jukeboxDisc(a.session->world(), at).empty()) pending = p.held().id;
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } g_jvOn = false; cinematicSetAimPoint(false); }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double dt) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const bool here = reg().block(w.getBlock(at.x, at.y, at.z)).has("jukebox");
    if ((!here && animAt < 0) || !sceneKeys(a, at, animAt >= 0, showBag)) { a.popScreen(); return; }
    const bool inBag = bagPadUpdate(in, p, bag);
    // your records (one of each), the dial's
    std::vector<std::string> discs;
    for (auto& it : p.inv) if (!it.empty()) if (const ItemDef* d = reg().item(it.id); d && d->has("disc") && std::find(discs.begin(), discs.end(), it.id) == discs.end()) discs.push_back(it.id);
    if (animAt < 0) g_jv.dial = discs;
    const int n = (int)g_jv.dial.size();
    scroll += (scrollTo - scroll) * std::min(1.f, (float)dt * 12.f);
    if (!pending.empty() && scene().phase == 2 && animAt < 0) { const std::string d = pending; pending.clear(); insert(a, d); }
    if (animAt >= 0) {
      const double t = scene().clock - animAt;
      if (!sent && t >= (g_jv.eject ? JUKE_EJECT_SEND : JUKE_INSERT_SEND)) {
        sent = true;
        if (g_jv.eject) jukeboxEject(*s->level, p, w, at, true);
        else for (auto& it : p.inv) if (it.id == g_jv.disc) { jukeboxInsert(*s->level, p, w, at, it); break; }
        cinematicTap();
      }
      if (t >= (g_jv.eject ? JUKE_EJECT : JUKE_INSERT)) animAt = -1;
    }
    if (inBag) return;
    const ivec2 nav = in.navStep();
    if (nav.x && n > 0) { pad = true; padPick = std::clamp(padPick + nav.x, 0, n - 1); scrollTo = (float)std::clamp(padPick - 4, 0, std::max(0, n - 9)); }
    if (pad && in.pressed(Act::MenuOk) && n > 0) insert(a, g_jv.dial[std::min(padPick, n - 1)]);
    if (in.pressed(Act::MenuX) || in.pressed(Act::MenuY)) eject(a);
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world();
    const float W = (float)a.width, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook();
    const std::string playing = jukeboxDisc(w, at);
    const int n = (int)g_jv.dial.size();
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    int hover = -1; bool onBox = false;
    if (!pad && !overUi && scene().phase == 2 && animAt < 0) {
      if (playing.empty()) for (int k = 0; k < n && hover < 0; k++) if (cutDialShown(k, n, scroll) > 0.5f && under(a, jukeDialPoint(at, scene().viewYaw, k, n, scroll, scene().clock), 0.1f)) hover = k;
      onBox = under(a, vec3(at) + vec3(0.5f, 0.85f, 0.5f), 0.4f);
      if (const float wh = ImGui::GetIO().MouseWheel; wh != 0 && n > 9) scrollTo = std::clamp(scrollTo - (wh > 0 ? 1.f : -1.f), 0.f, (float)(n - 9));
    } else if (pad && n > 0 && playing.empty()) hover = std::min(padPick, n - 1);
    const vec3 aimPt = hover >= 0 ? jukeDialPoint(at, scene().viewYaw, hover, n, scroll, scene().clock) - vec3(0, 0.1f, 0) : vec3(at) + vec3(0.5f, 1.f, 0.5f);
    cinematicSetAimPoint(scene().phase == 2 && (hover >= 0 || (onBox && !playing.empty())), aimPt);
    if (scene().phase == 2 && animAt < 0) {
      ImVec2 sp;
      if (hover >= 0 && toScreen(a, jukeDialPoint(at, scene().viewYaw, hover, n, scroll, scene().clock), sp)) {
        dl->AddCircle(sp, 30 * z, IM_COL32(255, 236, 160, 230), 28, 2 * z);
        const std::string label = discName(g_jv.dial[hover]);
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        dl->AddRectFilled(ImVec2(sp.x - ts.x * 0.5f - 6 * z, sp.y - 52 * z), ImVec2(sp.x + ts.x * 0.5f + 6 * z, sp.y - 52 * z + ts.y + 6 * z), IM_COL32(20, 16, 12, 200), 4 * z);
        dl->AddText(ImVec2(sp.x - ts.x * 0.5f, sp.y - 49 * z), IM_COL32(255, 240, 210, 255), label.c_str());
      } else if (onBox && !playing.empty() && toScreen(a, vec3(at) + vec3(0.5f, 1.12f, 0.5f), sp)) dl->AddCircle(sp, 34 * z, IM_COL32(255, 236, 160, 220), 28, 2 * z);
    }
    if (!overUi && !pad && scene().phase == 2 && animAt < 0 && ImGui::IsMouseClicked(0)) {
      if (hover >= 0) insert(a, g_jv.dial[hover]);
      else if (onBox && !playing.empty()) eject(a);
    }
    g_jv.at = at; g_jv.viewYaw = scene().viewYaw; g_jv.hover = hover; g_jv.scroll = scroll; g_jv.t = animAt < 0 ? -1 : scene().clock - animAt;
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_juke", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    if (!playing.empty()) ImGui::Text("Now playing: %s", discName(playing).c_str());
    else ImGui::Text(n ? "Choose a record" : "You have no records with you");
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the jukebox"
                      : pad ? "D-pad left / right a record \xC2\xB7 A play it \xC2\xB7 X take the record out \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : "Click a record to play it \xC2\xB7 click the jukebox to take the record out");
    ImGui::BeginDisabled(playing.empty() || animAt >= 0);
    if (ImGui::Button("Take it out")) eject(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; bool pad = false, showBag = false, sent = false; int padPick = 0; float scroll = 0, scrollTo = 0; double animAt = -1;
  std::string pending;
  BagPad bag;
  static std::string discName(const std::string& id) {
    const ItemDef* d = reg().item(id);
    std::string nm = d ? d->name : id; const std::string pre = "Music Disc \xE2\x80\x94 ";
    return nm.rfind(pre, 0) == 0 ? nm.substr(pre.size()) : nm;
  }
  void insert(App& a, const std::string& disc) {
    GameSession* s = a.session.get();
    if (!s || animAt >= 0 || scene().phase != 2) return;
    if (!jukeboxDisc(s->world(), at).empty()) { hudToast("Take the record out first", "warn"); return; }
    g_jv.disc = disc; g_jv.eject = false; sent = false; animAt = scene().clock;
    const auto it = std::find(g_jv.dial.begin(), g_jv.dial.end(), disc);
    g_jv.hover = it == g_jv.dial.end() ? -1 : (int)(it - g_jv.dial.begin());
    cinematicTap();
  }
  void eject(App& a) {
    GameSession* s = a.session.get();
    if (!s || animAt >= 0 || scene().phase != 2) return;
    const std::string d = jukeboxDisc(s->world(), at);
    if (d.empty()) return;
    g_jv.disc = d; g_jv.eject = true; sent = false; animAt = scene().clock;
    cinematicTap();
  }
};

// the campfire as a scene (CampfireStation): sat down by it; raw food in hand goes on the spot you click (the first free one
// with A), an empty hand on food takes it back; the rings show how done each is (it pops off when it's cooked)
class CinematicCampfireScreen : public Screen {
public:
  explicit CinematicCampfireScreen(ivec3 at) : at(at) {}
  const char* id() const override { return "cinematic_campfire"; }
  bool pausesGame() const override { return false; }
  bool wantsMouse() const override { return true; }
  void onOpen(App& a) override {
    Scene& S = scene();
    if (!a.session || !a.session->player) return;
    Player& p = *a.session->player;
    if (S.phase == 0) { S.lastPos = a.session->frame.cam.pos; S.lastYaw = a.session->frame.cam.yaw; S.lastPitch = a.session->frame.cam.pitch; }
    S.at = at; S.t = 0; S.phase = 1; S.kind = 13; S.front = vec3(0);
    leanPose(S, p.pos + vec3(0, p.eye, 0));
    g_fv = FireView{}; g_fv.at = at; g_fvOn = true;
  }
  void onClose(App&) override { Scene& S = scene(); if (S.phase == 1 || S.phase == 2) { S.phase = 3; S.t = 0; } g_fvOn = false; cinematicSetAimPoint(false); }
  bool onBack(App&) override { if (bag.on) { bag.on = false; return true; } return false; }
  void update(App& a, double) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) { a.popScreen(); return; }
    Player& p = *s->player; World& w = s->world(); Input& in = a.input;
    const std::string sh = reg().block(w.getBlock(at.x, at.y, at.z)).str("shape");
    if ((sh != "campfire" && sh != "soul_campfire") || !sceneKeys(a, at, false, showBag)) { a.popScreen(); return; }
    if (bagPadUpdate(in, p, bag)) return;
    const ivec2 nav = in.navStep();
    if (nav.x || nav.y) { pad = true; padSpot = ((padSpot + (nav.x ? nav.x : nav.y * 2)) % 4 + 4) % 4; }
    if (pad && in.pressed(Act::MenuOk)) act(a, padSpot);
    if (in.pressed(Act::MenuY)) act(a, -1);   // (the first free spot)
  }
  void draw(App& a) override {
    GameSession* s = a.session.get();
    if (!s || !s->player) return;
    Player& p = *s->player; World& w = s->world();
    const float W = (float)a.width, z = ui::Z();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool overUi = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || bookUiMouseOverBook();
    const std::vector<ItemStack> food = campfireFood(w, at);
    const bool cooks = campfireCooks(p.held()), lit = !(w.getMeta(at.x, at.y, at.z) & 8);
    if (glm::length(vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y)) > 0.5f) pad = false;
    int hover = -1;
    if (!pad && !overUi && scene().phase == 2) { for (int k = 0; k < 4 && hover < 0; k++) if (under(a, campfireSpot(at, k), 0.14f)) hover = k; }
    else if (pad) hover = padSpot;
    cinematicSetAimPoint(scene().phase == 2 && hover >= 0, hover >= 0 ? campfireSpot(at, hover) : vec3(at) + vec3(0.5f));
    // the rings: how done each piece is; the spot under the mouse
    for (int k = 0; k < 4 && scene().phase == 2; k++) {
      ImVec2 sp;
      if (!toScreen(a, campfireSpot(at, k), sp)) continue;
      if (!food[k].empty()) {
        const float done = campfireCooked(w, at, k), r = 18 * z;
        dl->PathArcTo(sp, r, -PI / 2, -PI / 2 + TAU * std::max(0.02f, done), 24);
        dl->PathStroke(IM_COL32(255, (int)(200 - 90 * done), 80, 220), 0, 3 * z);
      }
      if (k == hover && (cooks ? food[k].empty() : !food[k].empty())) dl->AddCircle(sp, 26 * z, IM_COL32(255, 236, 160, 230), 28, 2 * z);
    }
    if (!overUi && !pad && scene().phase == 2 && ImGui::IsMouseClicked(0) && hover >= 0) act(a, hover);
    g_fv.at = at; g_fv.hover = hover; g_fv.placing = cooks;
    if (showBag || bag.on) drawBag(a, p, bag.on ? bag.cur : -1);
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, 14 * z), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##cine_fire", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    int n = 0; for (auto& f : food) n += !f.empty();
    if (!lit) ImGui::Text("The fire's out (flint and steel lights it again)");
    else if (n) ImGui::Text("On the fire: %d of 4%s", n, hover >= 0 && !food[hover].empty() ? ("   \xC2\xB7   " + nameOf(food[hover]) + strFormat(", %d%% done", (int)(campfireCooked(w, at, hover) * 100))).c_str() : "");
    else ImGui::Text("Warm by the fire");
    ImGui::TextDisabled(bag.on ? "Bag: D-pad choose \xC2\xB7 A take into hand \xC2\xB7 B / LB / RB back to the fire"
                      : pad ? "D-pad a spot \xC2\xB7 A set it on (empty hand: take it back) \xC2\xB7 Y on the first free spot \xC2\xB7 LB / RB your bag \xC2\xB7 B leave"
                            : cooks ? "Click a spot to set it on the fire" : "Take raw food from your bag to cook it \xC2\xB7 click food on the fire to take it back");
    if (ImGui::Button("Leave")) { a.popScreen(); ImGui::End(); return; }
    ImGui::End();
  }
private:
  ivec3 at; bool pad = false, showBag = true; int padSpot = 0;
  BagPad bag;
  void act(App& a, int k) {
    GameSession* s = a.session.get();
    if (!s || scene().phase != 2) return;
    Player& p = *s->player; World& w = s->world();
    const std::vector<ItemStack> food = campfireFood(w, at);
    cinematicTap();
    if (campfireCooks(p.held())) { if (k < 0 || food[k].empty()) campfirePut(*s->level, p, w, at, k, p.held()); }
    else if (k >= 0 && !food[k].empty()) campfireTake(*s->level, p, w, at, k);
  }
};
}  // namespace

std::unique_ptr<Screen> makeCinematicPairScreen(ivec3 at, const std::string& station) { return std::make_unique<CinematicPairScreen>(at, station); }
std::unique_ptr<Screen> makeCinematicCutScreen(ivec3 at) { return std::make_unique<CinematicCutScreen>(at); }
std::unique_ptr<Screen> makeCinematicJukeboxScreen(ivec3 at) { return std::make_unique<CinematicJukeboxScreen>(at); }
std::unique_ptr<Screen> makeCinematicCampfireScreen(ivec3 at) { return std::make_unique<CinematicCampfireScreen>(at); }

}  // namespace dq
