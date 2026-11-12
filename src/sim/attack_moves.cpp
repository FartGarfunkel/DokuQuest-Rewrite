// sim/attack_moves.cpp — the moves' timings and the blade's path (see the header). Owner: claude (timings, path);
// the rules using them: grok.
#include "sim/attack_moves.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace dq {

namespace {
// Epic Fight's clips start cocked and blend into that from whatever you were doing (its "convert" time is the
// wind-up), strike in about a tenth of a second, hold the follow-through and take the next press at recovery. Our
// times count from the press, so the wind-up is in them. Rounded from the measurements (docs/ANIMATION_STUDY.md);
// targets per blow as Soulrend's weapon patch has them (swords 2, daggers and fists 1).
//                 name           len   lock  hitFrom hitTo recov lunge dmg  impact strikes left
const AttackMove MOVES[MV_COUNT] = {
  {"none",         0,    0,    0,    0,    0,    0,    0,   0,   0, false},
  {"sword_1",      0.60f, 0.05f, 0.08f, 0.17f, 0.40f, 0.25f, 1.0f, 1.0f, 2, false},
  {"sword_2",      0.62f, 0.08f, 0.11f, 0.21f, 0.42f, 0.60f, 1.0f, 1.0f, 2, false},
  {"sword_3",      0.80f, 0.08f, 0.12f, 0.22f, 0.62f, 0.55f, 1.3f, 1.6f, 3, false},
  {"sword_dash",   0.75f, 0.10f, 0.17f, 0.27f, 0.60f, 1.30f, 1.1f, 1.4f, 2, false},
  {"sword_air",    0.70f, 0.10f, 0.20f, 0.31f, 0.52f, 0.20f, 1.5f, 1.2f, 2, false},
  {"dagger_1",     0.42f, 0.03f, 0.05f, 0.11f, 0.24f, 0.45f, 0.8f, 0.6f, 1, false},
  {"dagger_2",     0.45f, 0.04f, 0.08f, 0.15f, 0.26f, 0.40f, 0.8f, 0.6f, 1, false},
  {"dagger_3",     0.70f, 0.08f, 0.16f, 0.26f, 0.45f, 0.90f, 1.2f, 1.0f, 1, false},
  {"axe_1",        0.85f, 0.08f, 0.13f, 0.26f, 0.70f, 0.45f, 1.0f, 1.4f, 2, false},
  {"axe_2",        0.90f, 0.10f, 0.16f, 0.30f, 0.78f, 0.45f, 1.1f, 1.5f, 2, false},
  {"fist_1",       0.42f, 0.03f, 0.06f, 0.11f, 0.20f, 0.40f, 1.0f, 0.6f, 1, true},
  {"fist_2",       0.42f, 0.03f, 0.06f, 0.11f, 0.20f, 0.40f, 1.0f, 0.7f, 1, false},
  {"fist_3",       0.58f, 0.05f, 0.08f, 0.15f, 0.45f, 0.40f, 1.3f, 1.2f, 1, true},
  {"tool_1",       0.60f, 0.06f, 0.10f, 0.20f, 0.40f, 0.10f, 1.0f, 1.0f, 1, false},
  {"tool_2",       0.62f, 0.06f, 0.10f, 0.20f, 0.42f, 0.10f, 1.0f, 1.0f, 1, false},
};

constexpr float SHOULDER_Y = 1.35f, SHOULDER_X = 0.30f;

// ---- the blade's path: (time, hilt, tip) keys per move in the attacker's frame ----
struct BladeKey { float t; vec3 hilt, tip; };
// An arc key: the arm out from the shoulder toward (yaw th, elevation ph) (degrees: 0 ahead, + right / + up) by ext,
// the blade len on along the same line. TODO(claude): replace with keys sampled from the drawn rig
// (tools/test/anim_test.cpp --dump-blades) once every move has its drawn keys.
struct ArcKey { float t, th, ph, ext; };
vec3 arcDir(float th, float ph) {
  const float a = th * 0.0174533f, b = ph * 0.0174533f;
  return vec3(std::sin(a) * std::cos(b), std::sin(b), std::cos(a) * std::cos(b));
}
std::vector<BladeKey> fromArcs(std::initializer_list<ArcKey> ks, float len, bool left) {
  std::vector<BladeKey> out;
  const vec3 sh(left ? -SHOULDER_X : SHOULDER_X, SHOULDER_Y, 0);
  const std::vector<ArcKey> v(ks);
  for (size_t i = 0; i < v.size(); i++) {   // (dense keys, every 1/120 s, the angles themselves eased between)
    const ArcKey &a = v[i], &b = i + 1 < v.size() ? v[i + 1] : v[i];
    const int n = i + 1 < v.size() ? std::max(1, (int)std::ceil((b.t - a.t) * 120)) : 1;
    for (int j = 0; j < n; j++) {
      const float w = (float)j / n, th = a.th + (b.th - a.th) * w, ph = a.ph + (b.ph - a.ph) * w, ext = a.ext + (b.ext - a.ext) * w;
      const vec3 d = arcDir(th, ph), h = sh + d * ext;
      out.push_back({a.t + (b.t - a.t) * w, h, h + d * len});
    }
  }
  return out;
}
const std::vector<BladeKey>& keysOf(u8 id) {
  static std::vector<BladeKey> K[MV_COUNT];
  static bool built = false;
  if (!built) {
    built = true;
    const float SW = 0.95f, DG = 0.55f, AX = 0.85f, FI = 0.18f, TL = 0.70f;
    K[MV_SWORD1] = fromArcs({{0, 110, 60, 0.45f}, {0.08f, 70, 35, 0.55f}, {0.17f, -70, -30, 0.55f}, {0.60f, -75, -32, 0.5f}}, SW, false);
    K[MV_SWORD2] = fromArcs({{0, -110, 20, 0.45f}, {0.11f, -80, 5, 0.55f}, {0.21f, 80, 0, 0.55f}, {0.62f, 85, 0, 0.5f}}, SW, false);
    K[MV_SWORD3] = fromArcs({{0, 95, -45, 0.45f}, {0.12f, 60, -35, 0.55f}, {0.22f, -55, 45, 0.55f}, {0.80f, -60, 50, 0.5f}}, SW, false);
    K[MV_SWORD_DASH] = fromArcs({{0, 5, -5, 0.10f}, {0.17f, 2, -3, 0.25f}, {0.27f, 0, -3, 0.65f}, {0.75f, 0, -3, 0.6f}}, SW, false);
    K[MV_SWORD_AIR] = fromArcs({{0, 5, 100, 0.45f}, {0.20f, 5, 70, 0.55f}, {0.31f, 5, -50, 0.55f}, {0.70f, 5, -55, 0.5f}}, SW, false);
    K[MV_DAGGER1] = fromArcs({{0, 20, -15, 0.15f}, {0.05f, 10, -10, 0.2f}, {0.11f, 5, -8, 0.6f}, {0.42f, 5, -8, 0.55f}}, DG, false);
    K[MV_DAGGER2] = fromArcs({{0, -100, 15, 0.45f}, {0.08f, -70, 10, 0.55f}, {0.15f, 70, 10, 0.55f}, {0.45f, 75, 10, 0.5f}}, DG, false);
    K[MV_DAGGER3] = fromArcs({{0, 15, -10, 0.10f}, {0.16f, 8, -6, 0.2f}, {0.26f, 3, -5, 0.65f}, {0.70f, 3, -5, 0.6f}}, DG, false);
    K[MV_AXE1] = fromArcs({{0, 100, 65, 0.45f}, {0.13f, 65, 45, 0.55f}, {0.26f, -45, -40, 0.55f}, {0.85f, -50, -42, 0.5f}}, AX, false);
    K[MV_AXE2] = fromArcs({{0, -95, 55, 0.45f}, {0.16f, -60, 40, 0.55f}, {0.30f, 55, -35, 0.55f}, {0.90f, 60, -37, 0.5f}}, AX, false);
    K[MV_FIST1] = fromArcs({{0, -10, -5, 0.10f}, {0.06f, -8, -2, 0.15f}, {0.11f, -5, 0, 0.70f}, {0.42f, -5, 0, 0.62f}}, FI, true);
    K[MV_FIST2] = fromArcs({{0, 10, -5, 0.10f}, {0.06f, 8, -2, 0.15f}, {0.11f, 5, 0, 0.72f}, {0.42f, 5, 0, 0.65f}}, FI, false);
    K[MV_FIST3] = fromArcs({{0, -100, 5, 0.45f}, {0.08f, -80, 5, 0.6f}, {0.15f, 25, 5, 0.65f}, {0.58f, 30, 5, 0.55f}}, FI, true);
    K[MV_TOOL1] = fromArcs({{0, 80, 65, 0.45f}, {0.10f, 50, 45, 0.55f}, {0.20f, -30, -35, 0.55f}, {0.60f, -35, -37, 0.5f}}, TL, false);
    K[MV_TOOL2] = fromArcs({{0, -75, 55, 0.45f}, {0.10f, -40, 40, 0.55f}, {0.20f, 40, -30, 0.55f}, {0.62f, 45, -32, 0.5f}}, TL, false);
  }
  return K[id < MV_COUNT ? id : 0];
}

// the segment p0..p1 (inflated by r) against the box lo..hi
bool segBox(const vec3& p0, const vec3& p1, vec3 lo, vec3 hi, float r) {
  lo -= vec3(r); hi += vec3(r);
  const vec3 d = p1 - p0;
  float t0 = 0, t1 = 1;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(d[a]) < 1e-6f) { if (p0[a] < lo[a] || p0[a] > hi[a]) return false; continue; }
    float ta = (lo[a] - p0[a]) / d[a], tb = (hi[a] - p0[a]) / d[a];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    if (t0 > t1) return false;
  }
  return true;
}
}  // namespace

const AttackMove& attackMove(u8 id) { return MOVES[id < MV_COUNT ? id : 0]; }

Moveset movesetOf(const std::string& toolType, const std::string& key) {
  if (toolType.empty()) return Moveset::Fist;
  if (toolType == "sword")
    return key.find("dagger") != std::string::npos || key.find("knife") != std::string::npos ? Moveset::Dagger : Moveset::Sword;
  if (toolType == "axe") return Moveset::Axe;
  return Moveset::Tool;
}

int comboLength(Moveset m) { return m == Moveset::Axe || m == Moveset::Tool ? 2 : 3; }

u8 attackMoveFor(Moveset m, int n, bool dash, bool air) {
  const int k = ((n % comboLength(m)) + comboLength(m)) % comboLength(m);
  switch (m) {
    case Moveset::Sword: return air ? MV_SWORD_AIR : dash ? MV_SWORD_DASH : (u8)(MV_SWORD1 + k);
    case Moveset::Dagger: return air ? MV_SWORD_AIR : dash ? MV_DAGGER3 : (u8)(MV_DAGGER1 + k);
    case Moveset::Axe: return air ? MV_SWORD_AIR : dash ? MV_AXE2 : (u8)(MV_AXE1 + k);
    case Moveset::Fist: return dash || air ? MV_FIST2 : (u8)(MV_FIST1 + k);
    case Moveset::Tool: return dash || air ? MV_TOOL1 : (u8)(MV_TOOL1 + k);
  }
  return MV_NONE;
}

void bladeAt(u8 id, float t, vec3& hilt, vec3& tip) {
  const auto& K = keysOf(id);
  if (K.empty()) { hilt = tip = vec3(SHOULDER_X, SHOULDER_Y, 0); return; }
  if (t <= K.front().t) { hilt = K.front().hilt; tip = K.front().tip; return; }
  for (size_t i = 0; i + 1 < K.size(); i++) {
    const BladeKey &a = K[i], &b = K[i + 1];
    if (t > b.t) continue;
    const float w = (t - a.t) / std::max(1e-5f, b.t - a.t);
    // (swung about the shoulder: the blade's direction turns, its length stays)
    const vec3 sh(a.hilt.x < 0 && b.hilt.x < 0 ? -SHOULDER_X : SHOULDER_X, SHOULDER_Y, 0);
    const vec3 ha = a.hilt - sh, hb = b.hilt - sh;
    const float la = glm::length(ha), lb = glm::length(hb), ba = glm::length(a.tip - a.hilt), bb = glm::length(b.tip - b.hilt);
    vec3 dh = glm::normalize(ha * (1 - w) / std::max(la, 1e-4f) + hb * w / std::max(lb, 1e-4f));
    vec3 db = glm::normalize((a.tip - a.hilt) * (1 - w) / std::max(ba, 1e-4f) + (b.tip - b.hilt) * w / std::max(bb, 1e-4f));
    hilt = sh + dh * (la + (lb - la) * w);
    tip = hilt + db * (ba + (bb - ba) * w);
    return;
  }
  hilt = K.back().hilt; tip = K.back().tip;
}

vec3 attackFrameToWorld(const vec3& l, const vec3& feet, float yaw, float pitch) {
  const float p = std::clamp(pitch, -1.2f, 1.2f), cp = std::cos(p), sp = std::sin(p);
  const float u = l.y - SHOULDER_Y, f = l.z;
  const float u2 = u * cp + f * sp, f2 = f * cp - u * sp;
  const vec3 fwd(-std::sin(yaw), 0, -std::cos(yaw)), right(std::cos(yaw), 0, -std::sin(yaw));
  return feet + right * l.x + vec3(0, SHOULDER_Y + u2, 0) + fwd * f2;
}

bool bladeSweepHits(u8 id, float t0, float t1, const vec3& feet, float yaw, float pitch, const vec3& lo, const vec3& hi,
                    float reach, float* when) {
  const AttackMove& m = attackMove(id);
  const float a = std::max(t0, m.hitFrom), b = std::min(t1, m.hitTo);
  if (id == MV_NONE || id >= MV_COUNT || b < a) return false;
  const int n = std::max(1, (int)std::ceil((b - a) * 240));
  for (int i = 0; i <= n; i++) {
    const float t = a + (b - a) * i / n;
    vec3 h, tp; bladeAt(id, t, h, tp);
    tp = h + (tp - h) * reach;
    // (from the shoulder out: the arm is in the blow too, so a foe close in is struck by the hilt end of it)
    const vec3 sh(h.x < 0 ? -SHOULDER_X : SHOULDER_X, SHOULDER_Y, 0);
    const vec3 S = attackFrameToWorld(sh + (h - sh) * 0.4f, feet, yaw, pitch), T = attackFrameToWorld(tp, feet, yaw, pitch);
    // (Soulrend's blades: 0.3-0.4 thick boxes)
    if (segBox(S, T, lo, hi, 0.15f)) { if (when) *when = t; return true; }
  }
  return false;
}

float lungeAt(u8 id, float t) {
  const AttackMove& m = attackMove(id);
  if (m.lunge <= 0) return 0;
  const float end = m.hitTo + 0.03f, u = std::clamp(t / end, 0.f, 1.f);
  return m.lunge * u * u * (3 - 2 * u);
}

}  // namespace dq
