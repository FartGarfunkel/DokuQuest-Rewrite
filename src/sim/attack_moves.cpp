// sim/attack_moves.cpp — the moves' timings and the blade's path (see the header). Owner: claude (timings, path);
// the rules using them: grok.
#include "sim/attack_moves.h"
#include "core/registry.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace dq {

namespace {
// The attack clips start cocked and blend into that from whatever you were doing (its "convert" time is the
// wind-up), strike in about a tenth of a second, hold the follow-through and take the next press at recovery. Our
// times count from the press, so the wind-up is in them: hitFrom..hitTo = the drawing's blend in + the clip's contact
// (render/attack_poses_data.inc), so the blade is live exactly while it is drawn passing, trimmed to where it is drawn in
// front of you (not while it is still up over the shoulder, nor once the chop is down by the feet); dagger_2's live part
// is the forehand it draws back across the front after its backhand. Rounded from the measurements (docs/ANIMATION_STUDY.md);
// targets per blow: swords 2, daggers and fists 1.
//                 name           len   lock  hitFrom hitTo recov lunge dmg  impact strikes left
const AttackMove MOVES[MV_COUNT] = {
  {"none",         0,    0,    0,    0,    0,    0,    0,   0,   0, false},
  {"sword_1",      0.60f, 0.05f, 0.13f, 0.20f, 0.40f, 0.25f, 1.0f, 1.0f, 2, false},
  {"sword_2",      0.62f, 0.08f, 0.20f, 0.25f, 0.42f, 0.60f, 1.0f, 1.0f, 2, false},
  {"sword_3",      0.80f, 0.08f, 0.16f, 0.25f, 0.62f, 0.55f, 1.3f, 1.6f, 3, false},
  {"sword_dash",   0.75f, 0.10f, 0.25f, 0.35f, 0.60f, 1.30f, 1.1f, 1.4f, 2, false},
  {"sword_air",    0.70f, 0.10f, 0.25f, 0.36f, 0.52f, 0.20f, 1.5f, 1.2f, 2, false},
  {"dagger_1",     0.42f, 0.03f, 0.10f, 0.20f, 0.24f, 0.45f, 0.8f, 0.6f, 1, false},
  {"dagger_2",     0.45f, 0.04f, 0.13f, 0.20f, 0.26f, 0.40f, 0.8f, 0.6f, 1, false},
  {"dagger_3",     0.70f, 0.08f, 0.25f, 0.30f, 0.45f, 0.90f, 1.2f, 1.0f, 1, false},
  {"axe_1",        0.85f, 0.08f, 0.16f, 0.25f, 0.70f, 0.45f, 1.0f, 1.4f, 2, false},
  {"axe_2",        0.90f, 0.10f, 0.16f, 0.23f, 0.78f, 0.45f, 1.1f, 1.5f, 2, false},
  {"fist_1",       0.42f, 0.03f, 0.13f, 0.23f, 0.25f, 0.40f, 1.0f, 0.6f, 1, true},
  {"fist_2",       0.42f, 0.03f, 0.13f, 0.23f, 0.25f, 0.40f, 1.0f, 0.7f, 1, false},
  {"fist_3",       0.58f, 0.05f, 0.13f, 0.23f, 0.45f, 0.40f, 1.3f, 1.2f, 1, true},
  {"tool_1",       0.60f, 0.06f, 0.15f, 0.18f, 0.40f, 0.10f, 1.0f, 1.0f, 1, false},
  {"tool_2",       0.62f, 0.06f, 0.15f, 0.18f, 0.42f, 0.10f, 1.0f, 1.0f, 1, false},
};

constexpr float SHOULDER_Y = 1.35f, SHOULDER_X = 0.30f;

// ---- the blade's path: (time, hilt, tip) keys per move in the attacker's frame ----
struct BladeKey { float t; vec3 hilt, tip; };
// The keys are the drawn blade's own (render/attack_poses.cpp through the rig), sampled every 1/120 s by
// `test_anim --dump-blades src/sim/attack_blades_data.inc`; test_anim fails if the drawing and this table drift apart.
struct BladeRow { u8 id; float t, hx, hy, hz, tx, ty, tz; };
const BladeRow BLADE_ROWS[] = {
#include "sim/attack_blades_data.inc"
};
const std::vector<BladeKey>& keysOf(u8 id) {
  static std::vector<BladeKey> K[MV_COUNT];
  static bool built = false;
  if (!built) {
    built = true;
    for (const BladeRow& r : BLADE_ROWS) if (r.id < MV_COUNT) K[r.id].push_back({r.t, vec3(r.hx, r.hy, r.hz), vec3(r.tx, r.ty, r.tz)});
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

Moveset movesetOf(const ItemDef* it) {
  if (!it) return Moveset::Fist;
  const std::string ms = it->str("moveset");
  if (ms == "greatsword") return Moveset::Greatsword;
  if (ms == "spear") return Moveset::Spear;
  if (ms == "mace") return Moveset::Mace;
  return movesetOf(it->hasTool ? it->tool.type : std::string(), it->key);
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
    case Moveset::Greatsword:
    case Moveset::Spear:
    case Moveset::Mace: break;   // no cuts yet
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
    // (the samples are close: the hilt goes straight between them; the blade turns, its length kept)
    const float ba = glm::length(a.tip - a.hilt), bb = glm::length(b.tip - b.hilt);
    const vec3 db = glm::normalize((a.tip - a.hilt) * (1 - w) / std::max(ba, 1e-4f) + (b.tip - b.hilt) * w / std::max(bb, 1e-4f));
    hilt = a.hilt + (b.hilt - a.hilt) * w;
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
    // (the blades: 0.3-0.4 thick boxes)
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
