// sim/boss_brains.cpp — how the great foes fight that had no fight of their own (they wandered and never struck: the
// Hollow Archmage, the Lich King, the Hex Matron, the Grave Warden, the Minotaur, the Valkyrie Queen, the Pirate Captain,
// the Rot King, the Slider, the Broodmother, the Ember Lord, the Sun Spirit). Three ways of fighting:
//   caster: keeps its distance and casts (arcane bolts, frost bolts, hex flasks or arrows), blinks away when you close
//           in, throws you back with a blast when you stand on it, and calls a few servants;
//   brute:  closes in and swings, charges, leaps or slams the ground, and calls servants if it has any;
//   brood:  pounces, bites, spits webs that slow you, and calls its brood.
// Every one: a second phase at half health (faster, harder, more), a shot at you when you've climbed out of its reach,
// its servants kept few, and it never wanders off or heals by being left.
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/mob_combat.h"
#include "sim/content.h"
#include "sim/level.h"
#include "core/registry.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <cstdio>
#include <cstdlib>

namespace dq {

namespace {
enum class Style { Caster, Brute, Brood };
struct Kit {
  Style style;
  std::string shot = "arcane";       // caster: arcane | frost | flask | arrow
  bool charge = false, leap = false, slam = false;
  std::string minion; int minionCap = 0;
  bool fire = false;                 // brute: its slam and charge leave fire
};
const std::map<std::string, Kit>& kits() {
  static const std::map<std::string, Kit> K = {
    {"hollow_archmage", {Style::Caster, "arcane", false, false, false, "skeleton", 2}},
    {"lich_king", {Style::Caster, "frost", false, false, true, "skeleton", 3}},
    {"hex_matron", {Style::Caster, "flask", false, false, false, "ghoul", 3}},
    {"grave_warden", {Style::Caster, "arrow", false, false, true, "skeleton", 3}},
    {"minotaur", {Style::Brute, "", true, false, true, "", 0}},
    {"valkyrie_queen", {Style::Brute, "", true, true, false, "", 0}},
    {"pirate_captain", {Style::Brute, "", false, true, false, "", 0}},
    {"rot_king", {Style::Brute, "", false, false, true, "ghoul", 3}},
    {"slider", {Style::Brute, "", true, false, true, "", 0}},
    {"broodmother", {Style::Brood, "", false, true, false, "cave_spider", 4}},
    {"ember_lord", {Style::Brute, "", true, false, true, "", 0, true}},
    {"sun_spirit", {Style::Brute, "", false, true, true, "", 0, true}},
  };
  return K;
}

float wrapA(float a) { while (a > PI) a -= TAU; while (a < -PI) a += TAU; return a; }
float yawTo(const Creature& m, vec3 to) { return std::atan2(-(to.x - m.pos.x), -(to.z - m.pos.z)); }
void shake(Level& lv, const Creature& m, float s) { for (Entity* q : pawnsOf(lv, m.realm)) if (glm::distance(q->pos, m.pos) < 24) lv.fx->shake(q->id, s); }
std::string nameOf(const Creature& m) { std::string n = m.T && !m.T->label.empty() ? m.T->label : m.type; if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]); return n; }

// its blows: what it hits (in front, or all round), every pawn in reach (a party all feels a slam)
int strike(Level& lv, Creature& m, float reach, float dmg, bool radial, float knock, const char* element = "") {
  int n = 0;
  for (Entity* q : pawnsOf(lv, m.realm)) {
    const vec3 d = q->pos - m.pos;
    const float hd = std::hypot(d.x, d.z);
    if (hd > reach + m.w || std::fabs(d.y) > 2.6f) continue;
    if (!radial && std::fabs(wrapA(std::atan2(-d.x, -d.z) - m.yaw)) > 1.2f) continue;
    DamageSrc s; s.knock = knock; if (*element) s.element = element;
    const float got = damageEntity(lv, *q, dmg, &m, "mob", s);
    if (std::getenv("DQ_BOSS_TRACE")) fprintf(stderr, "BOSS %s strike %.1f -> %.1f\n", m.type.c_str(), dmg, got);
    n++;
  }
  return n;
}

int servants(Level& lv, const Creature& m) {
  int n = 0;
  for (auto& up : lv.entities()) if (!up->removed && up->hp > 0 && up->data.value("bossOf", 0u) == m.id) n++;
  return n;
}
void summon(Level& lv, Creature& m, const std::string& type, int n, int cap) {
  const int room = cap - servants(lv, m);
  for (int k = 0; k < std::min(n, room); k++) {
    const float a = TAU * (k + frand()) / std::max(1, n);
    if (Creature* c = spawnCreature(lv, m.realm, type, m.pos + vec3(std::cos(a) * 2.2f, 0.1f, std::sin(a) * 2.2f))) {
      c->data["bossOf"] = m.id; c->data["noDrops"] = true; c->persistent = false;
      lv.fx->particles("poof", c->pos + vec3(0, 1, 0), 12);
    }
  }
  if (room > 0) lv.fx->sound("spell_ward", m.pos, 32);   // (calling its servants)
}

bool inHomeBox(const Creature& m, vec3 p) {
  if (!m.data.contains("homeBox") || !m.data["homeBox"].is_array() || m.data["homeBox"].size() < 6) return true;
  const json& b = m.data["homeBox"];
  return p.x >= b[0].get<float>() + 0.7f && p.x <= b[3].get<float>() - 0.7f
      && p.z >= b[2].get<float>() + 0.7f && p.z <= b[5].get<float>() - 0.7f
      && p.y >= b[1].get<float>() - 0.5f && p.y <= b[4].get<float>() + 0.5f;
}
// a free spot on the hall floor, around its home (never through the wall or the floor)
bool spotNear(World& w, const Creature& m, float r, vec3& out) {
  vec3 about = m.pos;
  if (m.data.contains("home") && m.data["home"].is_array() && m.data["home"].size() >= 3) {
    const json& h = m.data["home"];
    about = vec3(h[0].get<float>(), h[1].get<float>(), h[2].get<float>());
  }
  r = std::min(r, 4.2f);
  for (int tries = 0; tries < 16; tries++) {
    const float a = frand() * TAU;
    const float rr = 1.4f + frand() * std::max(0.4f, r - 1.4f);
    const int x = ifloor(about.x + std::cos(a) * rr), z = ifloor(about.z + std::sin(a) * rr);
    for (int dy = 1; dy >= -1; dy--) {
      const int y = ifloor(about.y) + dy;
      if (!w.isLoaded(x, y, z)) continue;
      const Registry& R = reg();
      if (R.tSolid[w.getBlock(x, y - 1, z)] && !R.tSolid[w.getBlock(x, y, z)] && !R.tSolid[w.getBlock(x, y + 1, z)] && !R.tSolid[w.getBlock(x, y + 2, z)] && !R.tLiquid[w.getBlock(x, y, z)]) {
        const vec3 cand(x + 0.5f, (float)y, z + 0.5f);
        if (!inHomeBox(m, cand)) continue;
        out = cand; return true;
      }
    }
  }
  return false;
}
void blink(Level& lv, Creature& m, vec3 to) {
  lv.fx->particles("portal", m.pos + vec3(0, 1, 0), 24);
  m.pos = to; m.prevPos = to; m.vel = vec3(0);
  lv.fx->particles("portal", m.pos + vec3(0, 1, 0), 24); lv.fx->sound("ender_tp", m.pos, 32);
}

// its shot at you (from its hand, leading you a little)
void shoot(Level& lv, Creature& m, Entity& p, const std::string& kind, float dmg, float spread = 0) {
  const vec3 e = m.pos + vec3(-std::sin(m.yaw) * 0.5f, std::min(m.h * 0.75f, 2.2f), -std::cos(m.yaw) * 0.5f);
  const vec3 aim = p.pos + vec3(p.vel.x * 0.3f, 1.0f, p.vel.z * 0.3f) + vec3(frand2(), 0, frand2()) * spread;
  vec3 d = aim - e; const float hd = std::hypot(d.x, d.z);
  if (kind == "flask" || kind == "boss_rock" || kind == "web") {   // thrown: an arc
    const float sp = kind == "web" ? 13.f : 12.f, t = std::max(0.25f, hd / sp), g = kind == "boss_rock" ? 16.f : kind == "web" ? 12.f : 20.f;
    const vec3 v(d.x / t, d.y / t + 0.5f * g * t, d.z / t);
    if (kind == "flask") spawnHexFlask(lv, m.realm, e, v, &m, frand() < 0.6f ? "poison" : "slow");
    else if (Entity* b = spawnProjectile(lv, m.realm, e, v, kind, &m)) b->data["dmg"] = dmg;
  } else if (kind == "arrow") {
    const float sp = 24, t = hd / sp; d.y += 0.5f * 20 * t * t;
    spawnArrow(lv, m.realm, e, glm::normalize(d) * sp, &m, dmg, false);
  } else {   // arcane or frost: straight
    if (Entity* b = spawnProjectile(lv, m.realm, e, glm::normalize(d) * 16.f, "arcane", &m)) { b->data["dmg"] = dmg; if (kind == "frost") b->data["frost"] = true; }
  }
  m.swingT = 0.3f;
  lv.fx->sound(kind == "arrow" ? "bow" : kind == "flask" || kind == "boss_rock" || kind == "web" ? "throw" : kind == "frost" ? "spell_frost" : "spell_fire", e, 28);
}

float num(const Creature& m, const char* k, float d = 0) { return m.data.value(k, d); }
void set(Creature& m, const char* k, float v) { m.data[k] = v; }

OptMove bossFight(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  auto it = kits().find(m.type);
  if (it == kits().end()) return std::nullopt;
  const Kit& K = it->second;
  m.persistent = true;   // (it waits where it is for you: never wanders off, never heals by being left)
  if (!p || p->hp <= 0 || dist > 48) { m.lockMove = false; return Move{0, m.yaw}; }
  World& w = lv.world(m.realm);
  const float now = m.age;
  const bool two = m.hp < m.maxHp * 0.5f;
  if (two && !m.data.value("phase2", false)) {   // the second phase, once
    m.data["phase2"] = true;
    lv.fx->sound("roar", m.pos, 48, 1, 0.8f); shake(lv, m, 0.5f);
    lv.fx->message(0, nameOf(m) + " rises in fury!", "boss");
    if (!K.minion.empty()) summon(lv, m, K.minion, 2, K.minionCap + 1);
    set(m, "bfNext", now + 1.2f);
  }
  const float pace = two ? 0.7f : 1.f;            // cooldowns in phase two
  const float dmg = (m.T ? m.T->damage : 5) * (two ? 1.25f : 1.f);
  // out of its reach (up a pillar, over a gap): it sees you there, and shoots
  const bool high = std::fabs(dyp) > 2.4f;
  float stuck = num(m, "bfStuck");
  stuck = high ? stuck + dt : std::max(0.f, stuck - dt * 2);
  set(m, "bfStuck", stuck);
  std::string state = m.data.value("bfState", std::string("chase"));
  float t0 = num(m, "bfT0"), next = num(m, "bfNext");
  auto begin = [&](const char* s) { if (std::getenv("DQ_BOSS_TRACE")) fprintf(stderr, "BOSS %s begin %s t=%.1f dist %.1f\n", m.type.c_str(), s, now, dist); state = s; t0 = now; m.data["bfState"] = s; set(m, "bfT0", now); m.data["bfHit"] = false; };
  auto finish = [&](float cd) { state = "chase"; m.data["bfState"] = "chase"; next = now + cd * pace; set(m, "bfNext", next); m.lockMove = false; };
  const float t = now - t0;

  // ---- an attack under way
  if (state == "volley") {   // casting: bolts one after another
    const int shots = two ? 5 : 3, done = m.data.value("bfShots", 0);
    if (t > 0.45f + done * 0.28f && done < shots) { shoot(lv, m, *p, K.shot.empty() ? "arcane" : K.shot, dmg * (K.shot == "arrow" ? 0.8f : 0.9f), 0.6f); m.data["bfShots"] = done + 1; }
    if (done >= shots) finish(1.6f);
    m.lockMove = true; m.vel.x *= 0.7f; m.vel.z *= 0.7f;
    return Move{0, toPlayer};
  }
  if (state == "nova") {   // a blast all round: off me
    m.lockMove = true; m.vel.x = m.vel.z = 0;
    if (frand() < dt * 40) lv.fx->particles("portal", m.pos + vec3(frand2() * 2, frand() * 2, frand2() * 2), 1);
    if (t > 0.7f && !m.data.value("bfHit", false)) {
      m.data["bfHit"] = true;
      strike(lv, m, 3.6f, dmg, true, 11, K.shot == "frost" ? "frost" : "magic");
      lv.fx->particles("explode", m.pos + vec3(0, 1, 0), 18); lv.fx->sound("explode", m.pos, 32); shake(lv, m, 0.35f);
    }
    if (t > 1.0f) { vec3 to; if (spotNear(w, m, 3.5f + frand() * 1.5f, to)) blink(lv, m, to); finish(1.2f); }
    return Move{0, m.yaw};
  }
  if (state == "swing") {   // a heavy blow: wound up, then down
    m.lockMove = t < 0.5f;
    if (t > 0.5f && !m.data.value("bfHit", false)) { m.data["bfHit"] = true; m.swingT = 0.35f; strike(lv, m, 2.6f, dmg * 1.3f, false, 7); lv.fx->sound("entity/player/attack/crit1", m.pos, 24); }
    if (t > 0.9f) finish(0.8f);
    return Move{0, t < 0.35f ? toPlayer : m.yaw};
  }
  if (state == "slam") {   // the ground struck: all round
    m.lockMove = true; m.vel.x = m.vel.z = 0;
    if (t > 0.8f && !m.data.value("bfHit", false)) {
      m.data["bfHit"] = true;
      strike(lv, m, 4.5f, dmg * 1.2f, true, 10, K.fire ? "fire" : "");
      lv.fx->particles("burst", m.pos + vec3(0, 0.2f, 0), 30, json{{"tile", "stone"}, {"speed", 2.f}}); lv.fx->sound("thud", m.pos, 40); shake(lv, m, 0.55f);
    }
    if (t > 1.4f) finish(1.4f);
    return Move{0, m.yaw};
  }
  if (state == "charge") {   // wound up, then a run straight at where you were
    const float yaw = num(m, "bfYaw", toPlayer);
    m.lockMove = true;
    if (t < 0.75f) { m.vel.x = m.vel.z = 0; if (t < 0.05f) lv.fx->sound("roar", m.pos, 40); return Move{0, toPlayer}; }
    if (t < 0.8f) set(m, "bfYaw", toPlayer);
    m.vel.x = -std::sin(yaw) * 12.5f; m.vel.z = -std::cos(yaw) * 12.5f;
    if (!m.data.value("bfHit", false) && strike(lv, m, 1.2f, dmg * 1.5f, true, 12, K.fire ? "fire" : "")) m.data["bfHit"] = true;
    if (t > 1.9f || (m.hitWall && t > 0.95f)) {
      if (m.hitWall) { lv.fx->sound("thud", m.pos, 40); shake(lv, m, 0.4f); begin("dazed"); m.lockMove = false; m.vel.x = m.vel.z = 0; return Move{0, yaw}; }
      finish(1.4f);
    }
    return Move{0, yaw};
  }
  if (state == "dazed") {   // ran into a wall: a moment to hit it back
    m.lockMove = true; m.vel.x = m.vel.z = 0;
    if (frand() < dt * 8) lv.fx->particles("crit", m.pos + vec3(0, m.h, 0), 2);
    if (t > 1.6f) finish(0.4f);
    return Move{0, m.yaw};
  }
  if (state == "leap") {   // up and onto you
    if (t < 0.4f) { m.lockMove = true; m.vel.x = m.vel.z = 0; return Move{0, toPlayer}; }
    if (!m.data.value("bfAir", false)) {
      m.data["bfAir"] = true;
      const vec3 d = p->pos - m.pos; const float hd = std::max(0.1f, std::hypot(d.x, d.z)), fly = 0.9f;
      m.vel = vec3(d.x / fly, 8.5f + std::max(0.f, d.y) * 1.2f, d.z / fly) * vec3(std::min(1.f, 14.f / hd * fly), 1, std::min(1.f, 14.f / hd * fly));
      m.onGround = false; lv.fx->sound("ws_whoosh", m.pos, 24);
    }
    m.lockMove = true;
    if (m.onGround && t > 0.6f) {
      m.data["bfAir"] = false;
      strike(lv, m, 3.2f, dmg * 1.2f, true, 9, K.fire ? "fire" : "");
      lv.fx->particles("burst", m.pos + vec3(0, 0.2f, 0), 20, json{{"tile", "stone"}, {"speed", 1.6f}}); lv.fx->sound("thud", m.pos, 32); shake(lv, m, 0.35f);
      finish(1.2f);
    }
    if (t > 2.5f) { m.data["bfAir"] = false; finish(0.8f); }
    return Move{0, toPlayer};
  }
  if (state == "throw") {   // you're out of reach: a stone, a web, a shot
    m.lockMove = true; m.vel.x *= 0.7f; m.vel.z *= 0.7f;
    if (t > 0.5f && !m.data.value("bfHit", false)) {
      m.data["bfHit"] = true;
      const std::string k = K.style == Style::Brood ? "web" : K.style == Style::Caster ? (K.shot.empty() ? "arcane" : K.shot) : "boss_rock";
      shoot(lv, m, *p, k, k == "web" ? 2.f : dmg);
    }
    if (t > 0.9f) finish(1.1f);
    return Move{0, toPlayer};
  }

  // ---- choosing what to do next
  if (now >= next) {
    m.data["bfShots"] = 0;
    if (stuck > 1.2f) { begin("throw"); return Move{0, toPlayer}; }
    if (!K.minion.empty() && now >= num(m, "bfSummon") && servants(lv, m) < K.minionCap) {
      set(m, "bfSummon", now + (two ? 10.f : 16.f));
      summon(lv, m, K.minion, two ? 3 : 2, K.minionCap);
      next = now + 0.8f; set(m, "bfNext", next);
      return Move{0, toPlayer};
    }
    if (K.style == Style::Caster) {
      if (dist < 3.4f) { begin("nova"); return Move{0, m.yaw}; }
      if (dist < 26) { begin("volley"); return Move{0, toPlayer}; }
    } else {
      const float r = frand();
      if (K.charge && dist > 5 && dist < 18 && r < 0.45f && !high) { begin("charge"); set(m, "bfYaw", toPlayer); return Move{0, toPlayer}; }
      if (high) { begin("throw"); return Move{0, toPlayer}; }   // (up out of reach: no leap or charge at a wall, a shot)
      if (K.leap && dist > 3.5f && dist < 14 && r < 0.75f) { begin("leap"); m.data["bfAir"] = false; return Move{0, toPlayer}; }
      if (K.slam && dist < 4.5f && r < 0.4f) { begin("slam"); return Move{0, m.yaw}; }
      if (dist < 3.2f + m.w) { begin("swing"); return Move{0, toPlayer}; }
      if (K.style == Style::Brood && dist < 12 && r < 0.3f) { begin("throw"); return Move{0, toPlayer}; }
    }
  }
  // ---- between moves
  m.lockMove = false;
  if (K.style == Style::Caster) {   // its distance: back off when close, come in when far, else circle
    if (now >= num(m, "bfBlink") && dist < 5) { set(m, "bfBlink", now + 7); vec3 to; if (spotNear(w, m, 3.8f, to)) { blink(lv, m, to); return Move{0, toPlayer}; } }
    if (dist > 12) {
      std::optional<float> nd = m.navDir(lv, p->pos, dt);
      const float yaw = nd ? *nd : toPlayer;
      const vec3 step = m.pos + vec3(-std::sin(yaw), 0, -std::cos(yaw)) * 0.9f;
      if (!inHomeBox(m, step)) return Move{0, toPlayer};
      return Move{m.T->speed, yaw};
    }
    if (dist < 6) return Move{m.T->speed * 0.9f, toPlayer + PI};
    return Move{m.T->speed * 0.45f, toPlayer + (m.data.value("bfSide", 1.f) > 0 ? PI / 2 : -PI / 2)};
  }
  if (dist < 1.6f + m.w) return Move{0, toPlayer};
  std::optional<float> nd = m.navDir(lv, p->pos, dt);
  return Move{m.T->speed * (two ? 1.2f : 1.f) * (dist > 8 ? 1.25f : 1.f), nd ? *nd : toPlayer};
}
} // namespace

void registerBossBrains() {
  for (auto& [type, k] : kits()) {
    if (!creatureType(type)) continue;
    CreatureHooks h;
    if (const CreatureHooks* old = creatureHooks(type)) h = *old;
    h.brain = [](Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) -> OptMove { return bossFight(lv, m, p, dist, dyp, toPlayer, dt); };
    setCreatureHooks(type, std::move(h));
  }
}

} // namespace dq
