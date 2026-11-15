// render/cinematic_hands.cpp — your own arms at the crafting table scene (ui/cinematic.h), after Cinematic Interactions'
// Hands / TableStation (MIT, Southware): each arm comes from just below and beside the view (the shoulder: 0.38 out,
// 0.46 down, 0.08 ahead); at rest the hands lie on the table's near edge (0.4 back, 0.24 apart each way, 0.062 up); the
// working hand goes over the square you aim at (0.075 above the top with something in it, 0.045 without, held back
// 0.075 along the reach), dips 0.06 as it sets a piece down (a 0.22 s tap), and the left hand takes the squares well to
// the left. They follow on springs (17 working, 12 at rest). What you're working with is in the working hand.
#include "assets/local_models.h"
#include "client/session.h"
#include "client/app.h"
#include "render/item_layers.h"
#include "render/char_layer.h"
#include "assets/item_models.h"
#include "assets/voxel.h"
#include "assets/item_build.h"
#include "core/tool_parts.h"
#include "sim/worktable.h"
#include "sim/station_work.h"
#include "sim/player.h"
#include "ui/cinematic.h"
#include "ui/book_ui.h"
#include <cmath>
#include <imgui.h>

namespace dq {
static vec3 g_hands[2]{}; static double g_handsAt = -1;
bool cinematicHandsWorld(vec3 out[2]) { if (g_handsAt < 0 || !cinematicActive()) return false; out[0] = g_hands[0]; out[1] = g_hands[1]; return true; }

namespace {

constexpr float ARM_SCALE = 0.55f;   // (its Hands.ARM_SCALE: the arms 0.55 of a body's)
constexpr float HAND_Y = 0.062f, REST_BACK = 0.4f, REST_SIDE = 0.24f, ITEM_REACH = 0.075f, MAX_REACH = 0.95f;

float hump(double u) { return u <= 0 || u >= 1 ? 0.f : (float)std::sin(u * PI); }

class CinematicHandsLayer : public RenderLayer {
public:
  const char* name() const override { return "cinematic-hands"; }
  bool setStation = false;   // (we set the player's station: ours to clear)
  int order() const override { return 124; }
  void update(GameSession& s, double dt) override {
    ivec3 at; int aim; double since; int phase; float viewYaw;
    vec3 restAt[2];
    on = s.player && cinematicHandsState(at, aim, since, phase, viewYaw, restAt);
    if (!on) { placed = false; if (s.player && setStation) { s.player->stationKind = -1; setStation = false; } return; }
    const Camera& cam = s.frame.cam;
    // (your own eyes and facing: in third person the camera is behind you, and the arms reached back to it)
    const bool tp = s.viewMode != 0 && !cinematicLeaning();
    const vec3 camPos = tp ? s.player->pos + vec3(0, s.player->eye, 0) + cinematicViewOffset() : vec3(cam.pos);
    const float vyaw = tp ? s.player->yaw : cam.yaw;
    const vec3 fwd(-std::sin(vyaw), 0, -std::cos(vyaw)), right(std::cos(vyaw), 0, -std::sin(vyaw));
    // 0 right, 1 left. Leaning: the mod's shoulders (0.38 out, 0.46 down, 0.08 forward of the leaning eye). Seamless first
    // person: your own shoulders, lower and a little ahead of your eye (at the eye's plane they filled the screen)
    const bool lean = cinematicLeaning();
    for (int k = 0; k < 2; k++) shoulder[k] = lean ? camPos + right * (k == 0 ? 0.38f : -0.38f) + vec3(0, -0.46f, 0) + fwd * 0.08f
                                                   : camPos + right * (k == 0 ? 0.3f : -0.3f) + vec3(0, -0.62f, 0) + fwd * 0.12f;   // (just below the view's bottom edge, as the hand view's arm comes in)
    const vec3 top = vec3(at) + vec3(0.5f, 1.f, 0.5f);
    const vec3 side(-std::sin(viewYaw + PI), 0, -std::cos(viewYaw + PI));   // from the table toward you
    vec3 rest[2];
    for (int k = 0; k < 2; k++) {
      // leaning: the station's own rest (ui/cinematic_table.cpp leanPose); seamless first person: low before your own eyes
      const vec3 r0 = cinematicLeaning() ? restAt[k] : camPos + fwd * 0.5f + right * (k == 0 ? 0.2f : -0.2f) + vec3(0, -0.36f, 0);
      rest[k] = r0 + vec3(0, 0.003f * std::sin((float)since * (k ? 1.5f : 1.7f) + k * 2), 0);
    }
    const bool carrying = !s.player->held().empty();
    const float tap = hump(since / 0.22);
    vec3 aimPt(0);
    const bool pointAim = aim < 0 && cinematicAimPoint(aimPt);   // (a chest's slot: no table squares)
    if (aim >= 0 || pointAim) {
      const vec3 a = aim >= 0 ? worktableCellCenter(at, aim) : aimPt;
      const float lateral = glm::dot(a - top, right);
      if (!leftWorks && lateral < -0.13f && !bookUiStationOn(s.app)) leftWorks = true; else if (leftWorks && lateral > -0.07f) leftWorks = false;
      if (bookUiStationOn(s.app)) leftWorks = false;   // (the off hand holds the journal at a station)
    }
    const int w = leftWorks ? 1 : 0;
    vec3 working = rest[w];
    if (aim >= 0 || pointAim) {
      const vec3 a = aim >= 0 ? worktableCellCenter(at, aim) : aimPt;
      vec3 reach(a.x - shoulder[w].x, 0, a.z - shoulder[w].z); reach = glm::length(reach) > 1e-4f ? glm::normalize(reach) : fwd;
      working = vec3(a.x, (aim >= 0 ? top.y : a.y) + HAND_Y + (carrying ? 0.075f : 0.045f) - 0.06f * tap, a.z) - reach * ITEM_REACH;
    } else working += vec3(0, (carrying ? 0.05f : 0.f) - 0.02f * tap, 0);
    if (glm::length(working - shoulder[w]) > MAX_REACH) working = shoulder[w] + glm::normalize(working - shoulder[w]) * MAX_REACH;
    vec3 target[2] = {w == 0 ? working : rest[0], w == 1 ? working : rest[1]};
    forging = cinematicKind() == 1 ? cinematicForgeT() : -1;
    if (cinematicKind() == 8 || cinematicKind() == 15) {   // the smithing table (and the Tool Forge): the anvil's three blows, the first landing as it's pressed (SmithingStation PRESS)
      PairView pv;
      if (cinematicPairView(pv) && pv.t >= 0) { const double f = pv.t - (pairBeats(8).contact - 0.7); if (f >= 0.45 && f < 1.62) forging = f; }
    }
    if (forging >= 0) {   // the anvil's hammer (AnvilStation: BLOWS 0.7, 1.02, 1.3; put away 0.32 after): raised, then down on each
      static const double BLOWS[3] = {0.7, 1.02, 1.3};
      const vec3 strike = top + side * 0.02f + right * 0.03f + vec3(0, 0.14f, 0);
      float lift = 0;
      double prev = 0;
      for (double b : BLOWS) {
        if (forging < b) { const double u = (forging - prev) / (b - prev); lift = u < 0.7 ? 0.26f * (float)std::sin(u / 0.7 * PI / 2) : 0.26f * (float)std::pow(1 - (u - 0.7) / 0.3, 2); break; }
        prev = b;
      }
      if (forging >= BLOWS[2]) lift = 0.12f * (float)std::max(0.0, 1 - (forging - BLOWS[2]) / 0.32);
      target[0] = strike + vec3(0, lift, 0) + side * (lift * 0.4f);
      work = 0;
    }
    if (cinematicKind() == 2) if (const double rt = cinematicRitualT(); rt >= 0) {   // the rite: both hands rise round the item
      const float up = (float)std::sin(std::min(1.0, rt / 0.5) * PI / 2) * (float)std::max(0.0, std::min(1.0, (1.5 - rt) / 0.4));
      const vec3 item = vec3(at) + vec3(0.5f, 0.75f + 0.42f, 0.5f);
      for (int k = 0; k < 2; k++) target[k] = rest[k] + (item + right * (k ? -0.17f : 0.17f) + side * 0.06f - rest[k]) * up;
    }
    {   // the grindstone, smithing table, loom, fletching and cartography tables: the hands carry the pieces in the work
      PairView pv;
      if (cinematicPairView(pv) && pv.t >= 0 && pv.t < pairBeats(pv.kind).length) {
        const PairBeats B = pairBeats(pv.kind);
        const vec3 lying[2] = {pairLieOnModel(s.world(), pv.at, 0), pairLieOnModel(s.world(), pv.at, 1)};
        auto hold = [&](int k) { return pairHeldPos(pv, k, lying) + side * 0.07f + right * (k ? -0.05f : 0.05f) + vec3(0, HAND_Y + 0.03f, 0); };
        const float back = (float)std::clamp((pv.t - B.letGo) / std::max(0.05, B.length - B.letGo), 0.0, 1.0);
        if (pv.kind == 6 || (pv.kind == 10 && pv.t < B.contact + 0.1)) for (int k = 0; k < 2; k++) { const vec3 h = hold(k); target[k] = h + (rest[k] - h) * back; }   // (both)
        else if (((pv.kind == 8 || pv.kind == 15) && pv.t < 0.76 && !pv.before[1].empty()) || (pv.kind == 11 && pv.t < B.contact + 0.05)) target[0] = hold(1);   // (the material, the paper)
        else if (pv.kind == 9 && pv.t < LOOM_TO) target[0] = hold(1);                                 // (the shuttle)
      }
    }
    if (cinematicKind() == 7) {   // the stonecutter: the working hand pushes the stone into the saw, then gathers the pieces
      CutView cv;
      if (cinematicCutView(cv) && cv.t >= 0) {
        vec3 out, rt; stationWorkFrame(s.world(), cv.at, out, rt);
        const vec3 lie = cutLiePos(s.world(), cv.at), blade = vec3(cv.at) + vec3(0.5f, 0.5625f, 0.5f) - rt * 0.25f;
        const float u = (float)std::clamp(cv.t / CUT_BITE, 0.0, 1.0);
        const vec3 push = lie + (blade - lie) * (u * u) + side * 0.09f + vec3(0, HAND_Y + 0.07f, 0);
        const vec3 gather = blade + rt * 0.24f + side * 0.12f + vec3(0, HAND_Y + 0.06f, 0);
        const float g = (float)std::clamp((cv.t - CUT_BITE) / 0.3, 0.0, 1.0);
        target[w] = push + (gather - push) * g;
      }
    }
    if (cinematicKind() == 12) {   // the jukebox: the working hand carries the record in, or takes it out
      JukeView jv;
      if (cinematicJukeView(jv) && jv.t >= 0) {
        const int n = (int)jv.dial.size();
        const vec3 from = jv.hover >= 0 && jv.hover < n ? jukeDialPoint(jv.at, jv.viewYaw, jv.hover, n, jv.scroll, s.frame.time) : vec3(jv.at) + vec3(0.5f, 1.5f, 0.5f);
        if (jv.eject ? (jv.t > 0.05 && jv.t < 0.9) : jv.t < 1.05) target[0] = jukeDiscPos(jv, from) + side * 0.08f + vec3(0, 0.02f, 0);
      }
    }
    if (phase == 3) { target[0] = shoulder[0] + vec3(0, -0.3f, 0); target[1] = shoulder[1] + vec3(0, -0.3f, 0); }   // leaving: down out of sight
    if (!placed) { for (int k = 0; k < 2; k++) hand[k] = shoulder[k] + vec3(0, -0.25f, 0); placed = true; }   // (they come up into view)
    for (int k = 0; k < 2; k++) {
      const float stiff = k == w && (aim >= 0 || pointAim) ? 17.f : 12.f;
      hand[k] += (target[k] - hand[k]) * (1.f - std::exp(-stiff * (float)dt));
    }
    work = w; light = s.world().getLight(at.x, at.y + 1, at.z);
    g_hands[0] = hand[0]; g_hands[1] = hand[1]; g_handsAt = s.frame.time;
    {   // for the others: what you're working at and where your hand is (net: "wk", "wa", "wt")
      Player& me = *s.player;
      me.stationKind = (i8)cinematicKind(); me.workAt = hand[w]; setStation = true;
      static double lastTap = -1; const double tt = cinematicTapTime();
      if (tt != lastTap) { if (lastTap >= 0 || tt > 0) me.workTap++; lastTap = tt; }
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!on || !s.player) return;
    const VoxelModel* skin = localPlayerArm();
    const VoxelModel& arm = skin && skin->ready() ? *skin : firstPersonArm();
    if (!arm.ready()) return;
    VoxelRenderer& VR = voxelRenderer();
    VR.begin(f, f.cam.viewProj);
    const VoxelLight L{(light >> 4) / 15.f, std::max(light & 15, 4) / 15.f};
    for (int k = 0; k < 2 && cinematicLeaning(); k++) {   // (seamless first person: your own first-person arms, drawn as ever)
      // the arm model hangs down its y (the fist about -0.30, the shoulder +0.40): stretched from the shoulder to the hand
      const vec3 S = shoulder[k], H = hand[k];
      const vec3 up = S - H; const float len = glm::length(up);
      if (len < 0.05f) continue;
      const vec3 y = up / len;
      vec3 x = glm::cross(y, vec3(0, 0, 1)); if (glm::length(x) < 1e-3f) x = glm::cross(y, vec3(1, 0, 0));
      x = glm::normalize(x); const vec3 z = glm::cross(x, y);
      mat4 B(1); B[0] = vec4(x, 0); B[1] = vec4(y, 0); B[2] = vec4(z, 0); B[3] = vec4(f.cam.rel(glm::dvec3(H)), 1);
      const float aw = cinematicLeaning() ? ARM_SCALE : ARM_SCALE * 0.72f;   // (seamless: the hand view's arm, its thickness the same all the way)
      const mat4 M = B * glm::scale(mat4(1), vec3(aw, len / 0.70f, aw)) * glm::translate(mat4(1), vec3(0, 0.30f, 0));
      VR.draw(arm.gl, M * arm.matrix(), L);
    }
    if (forging >= 0 && cinematicLeaning()) {   // the hammer in the right hand, its head down to the work (not leaning: your own body's arm and fist, render/char_layer.cpp)
      // your own hammer if you hold one (the stone, flint or smith's: the same as in your hand anywhere), else the smith's
      const ItemStack& hs = s.player->held();
      const bool ownHammer = !hs.empty() && model3dKind(hs.id, reg().item(hs.id)) == "hammer";
      const VoxelModel& hm = itemModel(ownHammer ? hs.id : std::string("smith_hammer"));
      if (hm.ready()) {
        mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(hand[0] + vec3(0, -0.02f, 0))));
        m = glm::rotate(m, s.frame.cam.yaw, vec3(0, 1, 0));
        // held as in your hand anywhere else (render/item_layers.cpp fpGripMatrix): the haft up out of the fist, the head on
        // top, its face forward; each blow swings it forward and down, the face onto the work, then back up
        float swing = 0;
        for (double b : {0.7, 1.02, 1.3}) {   // (the anvil's blows, ui/cinematic_table.cpp BLOWS)
          const double d = forging - b;
          if (d > -0.2 && d < 0.18) swing = std::max(swing, d < 0 ? (float)(1 - (-d / 0.2) * (-d / 0.2)) : (float)(1 - d / 0.18));
        }
        m = glm::rotate(m, 0.15f, vec3(0, 0, 1));                        // (leaning a little in, as a fist holds it)
        m = glm::rotate(m, 0.25f - 1.45f * swing, vec3(1, 0, 0));       // (drawn back, then down onto the work)
        m = glm::scale(m, vec3(0.62f));   // (the size it is in your hand: it had been half that, a toy)
        m = glm::translate(m, vec3(0, 0.62f, 0));
        VR.draw(hm.gl, m * hm.matrix(), L);
      }
    }
    // what you're working with, in the working hand (Cinematic Interactions' heldItemPoint)
    const ItemStack& held = s.player->held();
    if (!held.empty() && forging < 0 && cinematicLeaning()) {
      const std::string pk = partToolModelKey(held);
      const VoxelModel& im = itemModel(pk.empty() ? held.id : pk);
      if (im.ready()) {
        const vec3 H = hand[work], S = shoulder[work];
        vec3 reach(H.x - S.x, 0, H.z - S.z); reach = glm::length(reach) > 1e-4f ? glm::normalize(reach) : vec3(0, 0, -1);
        const vec3 p = H + reach * ITEM_REACH + vec3(0, -HAND_Y + 0.012f, 0);
        mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(p)));
        m = glm::rotate(m, std::atan2(reach.x, reach.z), vec3(0, 1, 0));
        if (im.kind == VoxelModel::Block) m = glm::scale(m, vec3(0.16f));
        else if (im.stand || im.torch3d) m = glm::scale(m, vec3(0.2f));   // a bottle, a bucket, a torch: carried upright, as it'll stand
        else { m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.26f)); }
        VR.draw(im.gl, m * im.matrix(), L);
      }
    }
    VR.end();
  }

  void drawHud(GameSession& s) override {   // its letterbox: black bars top and bottom while the camera moves in and out
    if (!on || s.hudHidden) return;
    ivec3 at; int aim; double since; int phase; float vy;
    if (!cinematicHandsState(at, aim, since, phase, vy)) return;
    const float k = cinematicMoving();
    if (k <= 0.001f) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float W = (float)s.app.width, H = (float)s.app.height, bh = H * 0.075f * k;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, bh), IM_COL32(0, 0, 0, 255));
    dl->AddRectFilled(ImVec2(0, H - bh), ImVec2(W, H), IM_COL32(0, 0, 0, 255));
  }

private:
  bool on = false, placed = false, leftWorks = false;
  vec3 shoulder[2]{}, hand[2]{};
  int work = 0; u8 light = 0xF0; double forging = -1;
};

} // namespace

void registerCinematicHands() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<CinematicHandsLayer>(); });
}

} // namespace dq
