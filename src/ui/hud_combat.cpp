// ui/hud_combat.cpp — what the combat batch shows (sim/combat_rpg.h): the stamina ring round the crosshair (only while
// it isn't full), the shield up and a parry's flash, and the plate over the creature you look at: its level, its health
// and its posture filling in from both ends; when that breaks it's staggered (a finishing blow lands).
#include "ui/hud_combat.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "render/camera.h"
#include "sim/player.h"
#include "sim/combat_rpg.h"
#include "core/registry.h"
#include <imgui.h>
#include <cmath>

namespace dq {

using namespace ui;

namespace {
struct CombatHud { float ringShown = 0, ringSeen = -1; double fullAt = -100; u32 plateId = 0; float plateAlpha = 0; double plateSeen = -100; };
CombatHud& CH() { static CombatHud h; return h; }

bool toScreen(GameSession& s, vec3 wp, ImVec2& out) {
  const Camera& cam = s.frame.cam;
  const vec4 q = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1);
  if (q.w <= 0.01f) return false;
  out = ImVec2((q.x / q.w * 0.5f + 0.5f) * s.app.width, (1 - (q.y / q.w * 0.5f + 0.5f)) * s.app.height);
  return true;
}
// the creature the crosshair is on (the nearest box the eye's ray goes through, within 16 blocks), none: nullptr
Entity* lookedAt(GameSession& s) {
  Player& p = *s.player;
  const Camera& cam = s.frame.cam;
  const vec3 o = vec3(cam.pos), d(-std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), -std::cos(cam.yaw) * std::cos(cam.pitch));
  Entity* best = nullptr; float bt = 16;
  const json& mobs = reg().mobs;
  for (auto& e : s.level->entities()) {
    if (e.get() == &p || e->removed || e->hp <= 0 || e->realm != p.realm || !mobs.contains(e->type)) continue;
    const vec3 lo = e->pos - vec3(e->w, 0, e->w), hi = e->pos + vec3(e->w, e->h, e->w);
    float t0 = 0, t1 = bt;
    for (int k = 0; k < 3; k++) {
      if (std::fabs(d[k]) < 1e-6f) { if (o[k] < lo[k] || o[k] > hi[k]) { t0 = 1; t1 = 0; break; } continue; }
      float a = (lo[k] - o[k]) / d[k], b = (hi[k] - o[k]) / d[k];
      if (a > b) std::swap(a, b);
      t0 = std::max(t0, a); t1 = std::min(t1, b);
    }
    if (t0 <= t1 && t0 < bt) { bt = t0; best = e.get(); }
  }
  return best;
}
}  // namespace

void drawCombatHud(GameSession& s, ImDrawList* dl, float z, float W, float H) {
  if (!s.player || !s.level) return;
  Player& p = *s.player;
  CombatHud& h = CH();
  const double now = s.app.time;
  const float dt = (float)std::min(0.1, s.app.dt);
  const ImVec2 c(std::round(W * 0.5f), std::round(H * 0.5f));
  // ---- the stamina ring: three quarters round the crosshair, from its bottom, clockwise; it fades once full again
  const float maxS = std::max(1.f, p.maxStamina), f = std::clamp(p.stamina / maxS, 0.f, 1.f);
  if (f < 0.999f) h.fullAt = now;
  const float want = (now - h.fullAt < 0.9 && !p.dead && !p.creative()) ? 1.f : 0.f;
  h.ringShown += (want - h.ringShown) * std::min(1.f, dt * (want > h.ringShown ? 10.f : 3.f));
  if (h.ringShown > 0.01f) {
    const float r = 21 * z, a0 = PI * 0.75f, span = PI * 1.5f, al = h.ringShown;
    const bool low = p.stamina < 30;   // (less than a roll's worth: sim/combat_rpg.cpp DODGE_COST)
    dl->PathArcTo(c, r, a0, a0 + span, 40);
    dl->PathStroke(IM_COL32(0, 0, 0, (int)(130 * al)), 0, 5 * z);
    if (f > 0.002f) {
      dl->PathArcTo(c, r, a0, a0 + span * f, 40);
      const float pulse = low ? 0.6f + 0.4f * std::sin((float)now * 12) : 1.f;
      dl->PathStroke(low ? IM_COL32(235, 90, 60, (int)(235 * al * pulse)) : IM_COL32(240, 205, 90, (int)(235 * al)), 0, 3 * z);
    }
    // a notch where one roll's worth begins
    const float an = a0 + span * std::min(1.f, 30.f / maxS);
    const float ca = std::cos(an), sa = std::sin(an);
    dl->AddLine(ImVec2(c.x + ca * (r - 4 * z), c.y + sa * (r - 4 * z)), ImVec2(c.x + ca * (r + 4 * z), c.y + sa * (r + 4 * z)), IM_COL32(255, 255, 255, (int)(110 * al)), 1 * z);
  }
  // ---- the shield up, and a parry's flash
  if (p.blockT > 0) {
    const bool parry = p.blockT < 0.18f;   // (PARRY_WINDOW: a hit now is parried)
    const ImVec2 b0(c.x - 7 * z, c.y + 13 * z);
    const ImVec2 pts[5] = {b0, ImVec2(b0.x + 14 * z, b0.y), ImVec2(b0.x + 14 * z, b0.y + 7 * z), ImVec2(b0.x + 7 * z, b0.y + 13 * z), ImVec2(b0.x, b0.y + 7 * z)};
    dl->AddConvexPolyFilled(pts, 5, parry ? IM_COL32(255, 245, 200, 230) : IM_COL32(200, 200, 210, 170));
    dl->AddPolyline(pts, 5, IM_COL32(0, 0, 0, 160), ImDrawFlags_Closed, 1.2f * z);
  }
  if (p.parryFlash > 0) {
    const float k = std::clamp(p.parryFlash / 0.35f, 0.f, 1.f), r = (14 + (1 - k) * 40) * z;
    dl->AddCircle(c, r, IM_COL32(255, 250, 220, (int)(255 * k)), 40, 3 * z * k + 1);
    dl->AddCircle(c, r * 0.6f, IM_COL32(255, 220, 140, (int)(180 * k)), 32, 2 * z * k + 1);
  }
  // ---- the plate over the creature you look at: Lv, name, health; posture from both ends, STAGGERED when it breaks
  Entity* e = lookedAt(s);
  if (e) { h.plateId = e->id; h.plateSeen = now; }
  const bool keep = now - h.plateSeen < 1.2;   // (lingers a moment after you look away)
  h.plateAlpha += ((keep ? 1.f : 0.f) - h.plateAlpha) * std::min(1.f, dt * (keep ? 12.f : 4.f));
  if (!e && keep) for (auto& q : s.level->entities()) if (q->id == h.plateId && !q->removed && q->hp > 0) { e = q.get(); break; }
  if (!e || h.plateAlpha < 0.02f) return;
  ImVec2 sp;
  if (!toScreen(s, e->pos + vec3(0, e->h + 0.45f, 0), sp)) return;
  sp.y = std::max(sp.y, 46 * z);   // (one right in front of you: its plate kept on the screen)
  const json& mobs = reg().mobs;
  std::string nm = mobs.contains(e->type) ? mobs[e->type].value("label", e->type) : e->type;
  if (!nm.empty() && nm.rfind("a ", 0) == 0) nm = nm.substr(2);
  if (!nm.empty() && nm.rfind("an ", 0) == 0) nm = nm.substr(3);
  if (!nm.empty() && nm.rfind("the ", 0) == 0) nm = nm.substr(4);
  if (!nm.empty()) nm[0] = (char)std::toupper((unsigned char)nm[0]);
  const bool elite = e->data.is_object() && e->data.value("elite", false);
  const std::string label = "Lv " + std::to_string(std::max(1, e->mobLevel)) + "  " + (elite ? "\xE2\x98\x85 Elite " : "") + nm;
  const float al = h.plateAlpha, bw = 92 * z;
  const ImVec2 ts = textSize(F_SMALL, label.c_str());
  const int you = playerCombatLevel(p);   // (red: well above you; gold: an elite)
  const ImU32 lvCol = e->mobLevel > you + 3 ? IM_COL32(255, 120, 100, (int)(255 * al)) : elite ? IM_COL32(255, 205, 90, (int)(255 * al)) : IM_COL32(240, 232, 210, (int)(255 * al));
  textOutline(dl, F_SMALL, ImVec2(sp.x - ts.x * 0.5f, sp.y - ts.y - 12 * z), lvCol, label.c_str(), 1 * z, IM_COL32(0, 0, 0, (int)(200 * al)));
  // health
  const ImVec2 h0(sp.x - bw * 0.5f, sp.y - 9 * z), h1(sp.x + bw * 0.5f, sp.y - 4 * z);
  dl->AddRectFilled(h0, h1, IM_COL32(20, 8, 6, (int)(200 * al)), 1.5f * z);
  const float hf = std::clamp(e->hp / std::max(1.f, e->maxHp), 0.f, 1.f);
  if (hf > 0) dl->AddRectFilled(ImVec2(h0.x + 1 * z, h0.y + 1 * z), ImVec2(h0.x + 1 * z + (bw - 2 * z) * hf, h1.y - 1 * z), IM_COL32(210, 52, 40, (int)(240 * al)), 1 * z);
  // posture: fills from both ends toward the middle (it breaks when they meet)
  const ImVec2 q0(sp.x - bw * 0.5f, sp.y - 2 * z), q1(sp.x + bw * 0.5f, sp.y + 2 * z);
  const float mp = std::max(1.f, e->maxPosture);   // (set when it's born: sim/combat_rpg.cpp)
  const float pf = e->staggerT > 0 ? 1.f : std::clamp(e->posture / mp, 0.f, 1.f);
  if (pf > 0.01f || e->staggerT > 0) {
    dl->AddRectFilled(q0, q1, IM_COL32(20, 16, 6, (int)(170 * al)), 1 * z);
    const float half = bw * 0.5f * pf;
    const ImU32 pc = e->staggerT > 0 ? IM_COL32(255, 250, 230, (int)(255 * al)) : pf > 0.75f ? IM_COL32(255, 150, 60, (int)(240 * al)) : IM_COL32(240, 200, 80, (int)(230 * al));
    dl->AddRectFilled(q0, ImVec2(q0.x + half, q1.y), pc, 1 * z);
    dl->AddRectFilled(ImVec2(q1.x - half, q0.y), q1, pc, 1 * z);
  }
  if (e->staggerT > 0) {   // the opening: a finishing blow lands now
    const char* st = "STAGGERED";
    const float pulse = 0.65f + 0.35f * std::sin((float)now * 14);
    const ImVec2 ss = textSize(F_SMALL, st);
    textOutline(dl, F_SMALL, ImVec2(sp.x - ss.x * 0.5f, sp.y + 5 * z), IM_COL32(255, 236, 160, (int)(255 * al * pulse)), st, 1 * z, IM_COL32(60, 20, 0, (int)(220 * al)));
  }
}

}  // namespace dq
