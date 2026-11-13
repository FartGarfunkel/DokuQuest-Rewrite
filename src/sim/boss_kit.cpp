// sim/boss_kit.cpp — an imported boss fights by its own attack table (tools/port/geo_boss_import.py -> data/local_models
// "kits": each move's clip and length, the phases and ranges it's used in, its weight, its hit windows (start, end, damage,
// radius, radial), and whether it teleports, leaps, or calls lightning). The brain: close in at a walk or a run; at the
// right range pick a move by weight and play it; while it plays, turn to the target early on, and in each hit window strike
// what's in reach (in front, or all round for a radial one). Below half health, phase two: a pause to change, then its
// phase-two moves and their lightning. A mob of mobs.json with "kit": "<model>" fights this way (sim/geo_anim.cpp plays
// the clips).
#include "sim/boss_kit.h"
#include "sim/creature.h"
#include "sim/geo_anim.h"
#include "sim/mob_combat.h"
#include "sim/weather.h"
#include "sim/level.h"
#include "core/registry.h"
#include <cstdio>
#include <cstdlib>

namespace dq {

namespace {
float wrapA(float a) { while (a > PI) a -= TAU; while (a < -PI) a += TAU; return a; }

const json* kitOf(const Creature& m) {
  const json& md = reg().mobs.contains(m.type) ? reg().mobs[m.type] : json();
  const std::string k = md.is_object() ? md.value("kit", std::string()) : std::string();
  if (k.empty() || !reg().kits.contains(k)) return nullptr;
  return &reg().kits[k];
}

void startClip(Creature& m, const std::string& clip) { m.data["clip"] = clip; m.data["clipT0"] = m.age; }

OptMove bossBrain(Level& lv, Creature& m, Entity* p, float dist, float dt) {
  if (std::getenv("DQ_BOSS_DEBUG")) { static float last = -9; if (m.age - last > 1) { last = m.age; fprintf(stderr, "boss %s age %.1f p %d dist %.1f atk %d\n", m.type.c_str(), m.age, p ? 1 : 0, dist, m.data.value("atk", -1)); } }
  const json* K = kitOf(m);
  if (!K || !K->is_array() || K->empty()) return std::nullopt;   // (no kit here: the type's own brain)
  const std::string model = m.modelKey();
  const float toTarget = p ? std::atan2(-(p->pos.x - m.pos.x), -(p->pos.z - m.pos.z)) : m.yaw;
  int phase = m.data.value("phase", 1);
  // phase two at half health: the change, once
  if (phase == 1 && m.hp < m.maxHp * 0.5f) {
    phase = 2; m.data["phase"] = 2; m.data["atk"] = -2; m.data["atkT0"] = m.age;
    startClip(m, geoClipLength(model, "phase_change") > 0 ? "phase_change" : "idle");
    lv.fx->sound("roar", m.pos, 48, 1, 0.7f);
  }
  const int atk = m.data.value("atk", -1);
  if (atk == -2) {   // changing phase: still
    if (m.age - m.data.value("atkT0", 0.f) >= std::max(1.f, geoClipLength(model, "phase_change"))) { m.data["atk"] = -1; m.data.erase("clip"); m.data["cd"] = m.age + 0.5f; }
    return Move{0, m.yaw};
  }
  if (!p || p->hp <= 0 || p->removed || dist > 40) { m.data["atk"] = -1; m.data.erase("clip"); return std::nullopt; }
  if (atk >= 0 && atk < (int)K->size()) {
    const json& A = (*K)[atk];
    const float t = m.age - m.data.value("atkT0", 0.f), len = A.value("len", 1.f);
    // turned to the target early in the move (the mods' rotation windows), then committed
    float dir = m.yaw;
    if (t < len * 0.35f) dir = toTarget;
    // a leap or a dive: carried at the target through the move's first part
    float want = 0;
    if (A.value("leap", false) && t > 0.1f && t < len * 0.4f && dist > 2.5f) want = std::min(14.f, dist * 2.2f);
    // its hit windows
    int done = m.data.value("hitMask", 0);
    if (A.contains("hits")) for (int i = 0; i < (int)A["hits"].size() && i < 16; i++) {
      const json& W = A["hits"][i];
      const float s = W[0], e = W[1], dmg = W[2], rad = W[3];
      const bool radial = W.size() > 4;
      if (t < s || (done >> i & 1)) continue;
      if (t > e + 0.05f) { done |= 1 << i; continue; }
      const vec3 d = p->pos - m.pos;
      const float hd = std::hypot(d.x, d.z), ang = std::fabs(wrapA(std::atan2(-d.x, -d.z) - m.yaw));
      if (hd <= rad + m.w && std::fabs(d.y) < 3.5f && (radial || ang < 1.3f)) {
        done |= 1 << i;
        DamageSrc src; src.type = "mob";
        damageEntity(lv, *p, dmg * (phase == 2 ? 1.15f : 1.f), &m, src);
        knockback(*p, glm::normalize(vec3(d.x, 0, d.z) + vec3(0.001f, 0, 0)) * (radial ? 9.f : 6.f) + vec3(0, 3.5f, 0));
        lv.fx->particles("crit", p->pos + vec3(0, 1, 0), 10);
      }
      // phase two: the move's lightning comes down on the target as it lands
      if (phase == 2 && A.value("lightning", false) && !(done >> (i + 16) & 1) && t >= s) {
        done |= 1 << (i + 16);
        if (WeatherSystem* ws = weatherOf(lv)) ws->strike(lv, (int)std::floor(p->pos.x), (int)std::floor(p->pos.y), (int)std::floor(p->pos.z));
      }
    }
    m.data["hitMask"] = done;
    if (t >= len) {
      m.data["atk"] = -1; m.data.erase("clip");
      m.data["cd"] = m.age + 0.4f + A.value("cooldown", 0.f) + (phase == 2 ? 0.f : 0.4f);
    }
    return Move{want, dir};
  }
  // between moves: close in, and pick one when the range is right
  const std::string range = dist < 4.5f ? "close" : dist < 11.f ? "mid" : "far";
  if (m.age >= m.data.value("cd", 0.f)) {
    double tot = 0; std::vector<std::pair<int, double>> cand;
    for (int i = 0; i < (int)K->size(); i++) {
      const json& A = (*K)[i];
      const double w = A.value("weight", 0.0);
      if (w <= 0 || A.value("moveset", std::string("soc")) != "soc") continue;
      bool ph = false; for (auto& x : A.value("phases", json::array({1, 2}))) if (x.get<int>() == phase) ph = true;
      bool rg = false; for (auto& x : A.value("ranges", json::array())) if (x.get<std::string>() == range) rg = true;
      if (!ph || !rg || geoClipLength(model, A.value("clip", std::string())) <= 0) continue;
      cand.push_back({i, w}); tot += w;
    }
    if (!cand.empty()) {
      double u = frand() * tot; int pick = cand.back().first;
      for (auto& [i, w] : cand) { u -= w; if (u <= 0) { pick = i; break; } }
      const json& A = (*K)[pick];
      m.data["atk"] = pick; m.data["atkT0"] = m.age; m.data["hitMask"] = 0;
      startClip(m, A.value("clip", std::string("idle")));
      if (A.value("teleport", false)) {   // in from the dark beside the target
        lv.fx->particles("portal", m.pos + vec3(0, 1, 0), 30);
        const float a = frand() * TAU;
        m.pos = p->pos + vec3(std::cos(a) * 3, 0.1f, std::sin(a) * 3); m.prevPos = m.pos;
        lv.fx->particles("portal", m.pos + vec3(0, 1, 0), 30); lv.fx->sound("teleport", m.pos, 32);
      }
      return Move{0, toTarget};
    }
  }
  const bool run = dist > 8;
  m.data["running"] = run;
  if (dist < 2.5f) return Move{0, toTarget};
  std::optional<float> nd = m.navDir(lv, p->pos, dt);
  return Move{m.T->speed * (run ? 1.6f : 1.f), nd ? *nd : toTarget};
}
} // namespace

void registerBossKits() {
  const json& mobs = reg().mobs;
  if (!mobs.is_object()) return;
  registerGeoAnim();
  for (auto& [type, d] : mobs.items()) {
    if (!d.is_object() || d.value("kit", std::string()).empty()) continue;
    CreatureHooks h;
    h.brain = [](Level& lv, Creature& m, Entity* p, float dist, float, float, float dt) -> OptMove { return bossBrain(lv, m, p, dist, dt); };
    setCreatureHooks(type, std::move(h));
  }
}

} // namespace dq
