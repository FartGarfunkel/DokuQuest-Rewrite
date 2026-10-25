// sim/end_fight.cpp — the End's gameplay (game/theend.js) and its creatures (game/endmobs.js): endermen (meeting
// their eyes, blinking about, arrows never touching them, water burning them), the end crystals on their pillars,
// the Ender Dragon (circling, strafing with void breath, charging, perching on the exit portal's pillar), the fight
// (crystals set out, the dragon roused, the exit portal, the egg and a gateway when it falls), the gateways round
// the main island, thrown ender pearls and eyes, chorus fruit, snowballs, and chorus plants coming down when cut.
#include "sim/endstory_int.h"
#include "sim/explosion.h"
#include "sim/creature_entities.h"
#include "sim/seamless.h"
#include "sim/weather.h"
#include "gen/dimgens.h"
#include "gen/overworld.h"

namespace dq::es {

static const EndGen* endGen(Level& lv) { return lv.gen ? dynamic_cast<const EndGen*>(lv.gen->forDim(DIM_END)) : nullptr; }
static int fountainY(Level& lv) { const EndGen* g = endGen(lv); return dimBase(DIM_END) + (g ? g->fountainY() : 64 + vmap::END_DY + 1); }
static bool rulesOn(Level& lv, const char* k) { const json& R = lv.info.rules; return !(R.is_object() && R.contains(k) && R[k] == false); }

// ------------------------------------------------------------------ endermen
// a blink: somewhere around (cx, cz), within r, on solid ground with room to stand
static bool enderBlink(Level& lv, Creature& e, float cx, float cz, float r, bool near) {
  World& w = lv.world(e.realm);
  for (int t = 0; t < 16; t++) {
    float a = rnd() * TAU, d = near ? 2 + rnd() * (r - 2) : rnd() * r;
    int x = ifloor(cx + std::cos(a) * d), z = ifloor(cz + std::sin(a) * d);
    if (!w.isLoaded(x, ifloor(e.pos.y), z)) continue;
    for (int y = ifloor(e.pos.y) + 6; y >= ifloor(e.pos.y) - 8; y--) {
      if (solid(w.getBlock(x, y - 1, z)) && !solid(w.getBlock(x, y, z)) && !solid(w.getBlock(x, y + 1, z)) && !solid(w.getBlock(x, y + 2, z)) &&
          !liquid(w.getBlock(x, y, z)) && !liquid(w.getBlock(x, y - 1, z))) {
        lv.fx->particles("end_mote", e.pos + vec3(0, 1.4f, 0), 14, json{{"speed", 0.8f}, {"grav", 0}, {"spread", 0.6f}});
        lv.fx->sound("ender_tp", e.pos + vec3(0, 1.5f, 0), 24);
        e.pos = e.prevPos = vec3(x + 0.5f, y, z + 0.5f); e.vel = vec3(0);
        lv.fx->particles("end_mote", e.pos + vec3(0, 1.4f, 0), 14, json{{"speed", 0.8f}, {"grav", 0}, {"spread", 0.6f}});
        lv.fx->sound("ender_tp", e.pos + vec3(0, 1.5f, 0), 24);
        return true;
      }
    }
  }
  return false;
}

static OptMove endermanAI(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  EndStory* S = endStory(lv); if (!S) return std::nullopt;
  Mem& M = S->mem(m);
  m.voiceTick(lv, dt, dist, 7, 10);
  // water burns: off it goes
  bool wet = m.inWater;
  if (!wet && dimHere(m) == DIM_OVERWORLD) if (WeatherSystem* W = weatherOf(lv); W && W->rain > 0.3f && m.light(lv, 2.5f).x >= 12) wet = true;
  if (wet && (M.wetCd -= dt) <= 0) { M.wetCd = 0.7f; DamageSrc s; s.type = "drown"; m.hurt(lv, 1, s); if (!m.alive()) return Move{0, m.yaw}; enderBlink(lv, m, m.pos.x, m.pos.z, 18, false); }
  // looked in the eye
  auto* P = dynamic_cast<Player*>(p);
  if (P && !(m.angryT > 0) && dist < 48) {
    bool masked = !P->armor.empty() && P->armor[0].id == "carved_pumpkin";
    vec3 lk = lookVec(*P), eye = P->pos + vec3(0, EYE, 0);
    vec3 dd = vec3(m.pos.x, m.pos.y + m.h - 0.3f, m.pos.z) - eye; float d = std::max(1e-3f, glm::length(dd));
    float dot = glm::dot(lk, dd) / d, lim = std::cos(std::atan(0.4f / d) + 0.025f);
    if (!masked && dot > lim && m.lineOfSight(lv, *P)) {
      M.stareT += dt;
      if (M.stareT > 0.3f) { m.angryT = 30; m.foe = P->id; M.stareT = 0; lv.fx->sound("enderman_stare", m.pos + vec3(0, 2.5f, 0), 40); M.lookT = 1.2f; }
    } else M.stareT = 0;
  }
  if (m.angryT > 0) {
    m.angryT -= dt;
    if (!p) return Move{0, m.yaw};
    m.senses(lv, p, dist, dyp, dt, 40);
    // it stares back a moment, then comes, blinking closer when you're far
    if (M.lookT > 0) { M.lookT -= dt; return Move{0, toPlayer}; }
    if ((M.tpT -= dt) <= 0) { M.tpT = 2.5f + rnd() * 4; if (dist > 9 || rnd() < 0.3f) enderBlink(lv, m, p->pos.x, p->pos.z, dist > 9 ? 4 : 7, true); }
    m.meleeAt(lv, *p, dist, dyp);
    return m.chase(lv, *p, dist, toPlayer, dt, 1.7f);
  }
  if (dist > 80 && !m.kept) { m.removed = true; return Move{0, m.yaw}; }
  // otherwise: an aimless amble, now and then a blink
  if ((m.wanderT -= dt) <= 0) {
    m.wanderT = 3 + rnd() * 6; m.wanderDir = rnd() * TAU; m.wanderOn = rnd() < 0.55f;
    if (rnd() < 0.05f) enderBlink(lv, m, m.pos.x, m.pos.z, 14, false);
  }
  return Move{m.wanderOn ? m.T->speed * 0.3f : 0, m.wanderDir};
}

// ------------------------------------------------------------------ the crystals and the dragon
static OptMove crystalAI(Level& lv, Creature& m, Entity*, float, float, float, float) {
  m.noGravity = true; m.hasFlyVy = true; m.flyVy = 0; m.vel = vec3(0); m.kept = true;
  return Move{0, m.yaw};
}
static Creature* crystalFor(Level& lv, Creature& e) {
  Creature* best = nullptr; float bd = 64;
  for (auto& u : lv.entities()) if (auto* c = dynamic_cast<Creature*>(u.get()))
    if (c->type == "end_crystal" && c->alive() && !c->removed && c->realm == e.realm) { float d = glm::distance(c->pos, e.pos); if (d < bd) { bd = d; best = c; } }
  return best;
}
static int crystalsAlive(Level& lv, const Creature* except) {
  int n = 0;
  for (auto& u : lv.entities()) if (auto* c = dynamic_cast<Creature*>(u.get())) if (c != except && c->type == "end_crystal" && c->alive() && !c->removed) n++;
  return n;
}

}  // namespace dq::es
namespace dq {
// dragons.cpp shares these with the Ender Dragon
void dragonUnstick(Level& lv, Creature& e, es::Mem& M, float dt);
void dragonFace(Creature& e, float tx, float tz, float dt, float k);
void dragonBreath(Level& lv, Creature& e, Entity& target, float dt);
}
namespace dq::es {

static OptMove enderDragonAI(Level& lv, Creature& m, Entity*, float, float, float, float dt) {
  EndStory* S = endStory(lv); if (!S) return std::nullopt;
  Mem& M = S->mem(m);
  const CreatureType& T = *m.T;
  float fy = (float)fountainY(lv);
  m.kept = true; m.noGravity = true; m.hasFlyVy = true;
  if (m.hoverT > 0) m.hoverT -= dt;
  dragonUnstick(lv, m, M, dt);
  // healed by the crystals: a beam of light from the nearest
  Creature* C = crystalFor(lv, m);
  if (C && m.hp < m.maxHp) m.hp = std::min(m.maxHp, m.hp + dt * 2.5f);
  if (C && rnd() < dt * 18) {
    float k = rnd(); vec3 a = C->pos + vec3(0, 1.2f, 0), b = m.pos + vec3(0, 1.5f, 0);
    sfx::spark(lv, a + (b - a) * k, "end_mote", json{{"life", 0.5f}, {"size", 0.22f}});
  }
  if ((M.flapT -= dt) <= 0) { M.flapT = 0.9f; lv.fx->sound("dragon_flap", m.pos + vec3(0, 2, 0), 70); }
  Entity* p = nearestPawn(lv, m.realm, m.pos, 200);
  float dist = p ? std::hypot(p->pos.x - m.pos.x, p->pos.z - m.pos.z) : 999, dyp = p ? p->pos.y - m.pos.y : 0;
  float toPlayer = p ? headingTo(m.pos, p->pos.x, p->pos.z) : m.yaw;
  M.stT -= dt;
  auto toPt = [&](float x, float y, float z, float sp) { m.flyVy = clampf((y - m.pos.y) * 1.2f, -9, 9); return Move{sp, headingTo(m.pos, x, z)}; };
  const std::string st = m.state;
  if (st == "strafe") {
    if (!p) m.state = "circle";
    else {
      if (!M.hasBreathPt) { float a = std::atan2(m.pos.z - p->pos.z, m.pos.x - p->pos.x); M.breathPt = vec3(p->pos.x + std::cos(a) * 10, p->pos.y + 7, p->pos.z + std::sin(a) * 10); M.hasBreathPt = true; }
      vec3 b = M.breathPt; float d = std::hypot(b.x - m.pos.x, b.z - m.pos.z);
      if (d > 3 && !(m.breathT > 0) && M.stT > -6) return toPt(b.x, b.y, b.z, T.speed * 2.4f);
      m.flyVy = clampf((b.y - m.pos.y) * 1.5f, -6, 6);
      dragonFace(m, p->pos.x, p->pos.z, dt, 4); m.hoverT = 0.5f;
      if (!(m.breathT > 0) && !M.breathed) { m.breathT = 2.6f; M.breathed = true; lv.fx->sound("dragon_breath", m.pos + vec3(0, 2, 0), 70); }
      if (m.breathT > 0) { m.breathT -= dt; dragonBreath(lv, m, *p, dt); return Move{0, m.yaw}; }
      M.breathed = false; M.hasBreathPt = false; m.state = "circle"; M.stT = 5 + rnd() * 4;
    }
  } else if (st == "charge") {
    if (!p) m.state = "circle";
    else {
      float d3 = glm::distance(p->pos + vec3(0, 1, 0), m.pos);
      m.flyVy = clampf((p->pos.y + 0.8f - m.pos.y) * 2.5f, -12, 9);
      if (d3 < 3.5f + m.w && m.attackCd <= 0) {
        m.attackCd = 1.4f; m.swingT = 0.3f;
        hurtPawn(lv, *p, T.damage, &m, "mob", 9);
        p->vel.y = std::max(p->vel.y, 7.f);
        lv.fx->sound("dragon_bite", m.pos + vec3(0, 1, 0), 60);
        m.state = "climb"; M.stT = 1.8f;
      }
      if (M.stT <= 0) { m.state = "climb"; M.stT = 1.5f; }
      return Move{T.speed * 3.2f, toPlayer};
    }
  } else if (st == "climb") {
    m.flyVy = 9;
    if (M.stT <= 0) { m.state = "circle"; M.stT = 5 + rnd() * 4; }
    return Move{T.speed * 2, m.yaw};
  } else if (st == "perch") {
    // down onto the portal's pillar, where you can reach it; it breathes on anyone close
    float px = 0.5f, pz = 0.5f, py = fy + 4.1f, d = std::hypot(px - m.pos.x, pz - m.pos.z);
    if (!M.perched) {
      if (d > 2) return toPt(px, std::max(py + 8, m.pos.y - 1), pz, T.speed * 2);
      m.flyVy = clampf((py - m.pos.y) * 2, -6, 4); m.hoverT = 0.5f;
      if (std::fabs(m.pos.y - py) < 0.6f) { M.perched = true; M.stT = 10 + rnd() * 5; lv.fx->sound("dragon_roar", m.pos + vec3(0, 2, 0), 120); }
      return Move{0, m.yaw};
    }
    float k = std::min(1.f, dt * 3);
    m.pos.x += (px - m.pos.x) * k; m.pos.z += (pz - m.pos.z) * k; m.flyVy = clampf((py - m.pos.y) * 3, -3, 3); m.hoverT = 0.5f;
    if (p) {
      dragonFace(m, p->pos.x, p->pos.z, dt, 2.5f);
      if (dist < 5 + m.w && std::fabs(dyp) < 4 && m.attackCd <= 0) { m.attackCd = 1.5f; m.swingT = 0.3f; hurtPawn(lv, *p, T.damage * 0.8f, &m, "mob", 7); lv.fx->sound("dragon_bite", m.pos + vec3(0, 1, 0), 50); }
      if (dist < 22 && !(m.breathT > 0) && (M.pbT -= dt) <= 0) { M.pbT = 4 + rnd() * 2; m.breathT = 2; lv.fx->sound("dragon_breath", m.pos + vec3(0, 2, 0), 60); }
      if (m.breathT > 0) { m.breathT -= dt; dragonBreath(lv, m, *p, dt); }
    }
    if (M.stT <= 0) { M.perched = false; m.state = "climb"; M.stT = 2; lv.fx->sound("dragon_roar", m.pos + vec3(0, 2, 0), 120); }
    return Move{0, m.yaw};
  }
  // circling the island, high over the pillars
  m.state = "circle";
  if (!M.hasOrbit) { M.orbit = std::atan2(m.pos.z, m.pos.x); M.hasOrbit = true; }
  M.orbit += dt * 0.32f;
  float gx = std::cos(M.orbit) * 55, gz = std::sin(M.orbit) * 55, alt = fy + 30 + std::sin(m.age * 0.4f) * 5;
  if (M.stT <= 0) {
    int crystals = crystalsAlive(lv, nullptr); float r = rnd();
    M.stT = 6 + rnd() * 5;
    if (p && dist < 150) {
      if (r < 0.32f) { m.state = "strafe"; M.hasBreathPt = false; M.stT = 8; }
      else if (r < 0.56f) { m.state = "charge"; M.stT = 6; }
      else if (r < 0.56f + (crystals ? 0.16f : 0.34f) && std::hypot(p->pos.x, p->pos.z) < 70) { m.state = "perch"; M.perched = false; M.stT = 20; }
    }
  }
  return toPt(gx, alt, gz, T.speed * 2.2f);
}

// ------------------------------------------------------------------ the gateways round the main island
vec3 gatewaySpot(int k) { float a = k * 2.399f + 0.6f; return vec3(jsRound(std::cos(a) * EndGen::GATE_R), dimBase(DIM_END) + 75 + vmap::END_DY, jsRound(std::sin(a) * EndGen::GATE_R)); }
static void placeGateway(World& w, ivec3 g) {
  w.setBlock(g.x, g.y, g.z, B("end_gateway"), 0);
  w.setBlock(g.x, g.y - 1, g.z, B("bedrock"), 0); w.setBlock(g.x, g.y + 1, g.z, B("bedrock"), 0);
}
static void gatewayOpen(Level& lv) {
  json& E = endRec(lv);
  E["gates"] = std::min(20, jint(E, "gates") + 1);
  vec3 s = gatewaySpot(jint(E, "gates") - 1);
  World& w = lv.world(REALM_MAIN);
  if (w.isLoaded((int)s.x, (int)s.y, (int)s.z)) placeGateway(w, ivec3(s));
  lv.fx->sound("end_gate", s, 120);
  lv.fx->particles("end_mote", s + vec3(0.5f), 40, json{{"speed", 3}, {"grav", 0}});
}
static void gatewayTick(Level& lv, EndStory& S, double dt) {
  World& w = lv.world(REALM_MAIN);
  json& E = endRec(lv);
  // gateways (and the far ends the gateways made) set down once their land is loaded
  int gates = jint(E, "gates");
  static double chk = 0;
  if ((chk -= dt) <= 0) {
    chk = 2;
    BlockId gw = B("end_gateway");
    for (int k = 0; k < gates; k++) { ivec3 g(gatewaySpot(k)); if (w.isLoaded(g.x, g.y, g.z) && w.getBlock(g.x, g.y, g.z) != gw) placeGateway(w, g); }
    json& far = sub(E, "farGates", true);
    for (size_t i = 0; i < far.size();) {
      ivec3 g(ivec3(jvec(far[i])));
      if (w.isLoaded(g.x, g.y, g.z)) { if (w.getBlock(g.x, g.y, g.z) != gw) placeGateway(w, g); far.erase(far.begin() + i); } else i++;
    }
  }
  BlockId gw = B("end_gateway");
  for (Player* p : livePlayers(lv, REALM_MAIN)) {
    if (!inEnd(*p)) continue;
    PlayerMem& P = S.pm(*p);
    if ((P.gateCd -= (float)dt) > 0) continue;
    BlockId id = w.getBlock(ifloor(p->pos.x), ifloor(p->pos.y + 0.5f), ifloor(p->pos.z)), id2 = w.getBlock(ifloor(p->pos.x), ifloor(p->pos.y + 1.5f), ifloor(p->pos.z));
    if (id != gw && id2 != gw) continue;
    P.gateCd = 3;
    float r = std::hypot(p->pos.x, p->pos.z), a = std::atan2(p->pos.z, p->pos.x);
    vec3 to;
    SeamlessSystem* SS = lv.system<SeamlessSystem>();
    if (r < 200) {
      // out to the far islands, in the direction of this gateway
      to = SS ? SS->endLanding(std::cos(a) * 900, std::sin(a) * 900) : vec3(endPlatform()) + vec3(0.5f, 1, 0.5f);
      ivec3 g(ifloor(to.x) + 2, ifloor(to.y) + 2, ifloor(to.z));
      if (w.isLoaded(g.x, g.y, g.z)) { if (w.getBlock(g.x, g.y, g.z) != gw) placeGateway(w, g); }
      else sub(E, "farGates", true).push_back(vj(vec3(g)));
      to.x -= 1;
    } else {
      // and back: to the nearest gateway round the main island
      int best = -1; float bd = 1e9f;
      for (int k = 0; k < gates; k++) {
        float ga = k * 2.399f + 0.6f, d = std::fabs(std::fmod(ga - a + PI * 3, TAU) - PI);
        if (d < bd) { bd = d; best = k; }
      }
      if (best >= 0) { vec3 sp = gatewaySpot(best); to = vec3(sp.x * 0.85f + 0.5f, sp.y, sp.z * 0.85f + 0.5f); }
      else { ivec3 P0 = endPlatform(); to = vec3(P0.x + 0.5f, P0.y + 1, P0.z + 0.5f); }
    }
    lv.fx->particles("end_mote", p->pos + vec3(0, 1, 0), 30, json{{"speed", 3}, {"grav", 0}});
    teleport(lv, *p, vec3(to.x, to.y + 0.2f, to.z));
    if (SS) if (auto* st = SS->state(p->id)) st->slowFallT = std::max(st->slowFallT, 6.f);
    lv.fx->soundUi(p->id, "end_gate");
    say(lv, p->id, r < 200 ? "The gateway flings you out to the far islands" : "Back towards the main island");
  }
}

// ------------------------------------------------------------------ the fight
static void dragonSlain(Level& lv, EndStory& S, Creature& e) {
  json& E = endRec(lv);
  int slain = jint(E, "slain") + 1;
  E["slain"] = slain; E["dragonHp"] = 0; E["again"] = 0;
  World& w = lv.world(REALM_MAIN);
  int fy = fountainY(lv);
  BlockId portal = B("end_portal");
  // the exit portal opens, and the egg comes to rest on the pillar
  for (int z = -3; z <= 3; z++) for (int x = -3; x <= 3; x++) { float d = std::hypot((float)x, (float)z); if (d <= 2.6f && (x || z)) w.setBlock(x, fy, z, portal, 0); }
  if (slain == 1) w.setBlock(0, fy + 4, 0, B("dragon_egg_ender"), 0);   // (0.9.0.33) one egg
  gatewayOpen(lv);
  lv.fx->sound("end_portal_open", vec3(0, fy, 0), 300);
  for (int i = 0; i < 2; i++) sfx::burst(lv, e.pos + vec3(0, 1.5f, 0), i ? "mana_arcane" : "end_mote", 30, 6, json{{"life", 1.6f}, {"size", 0.25f}, {"vy", 4}});
  for (Player* l : levelPlayers(lv)) {
    say(lv, l->id, "The Ender Dragon is slain! The exit portal is open, its egg rests on the pillar, and a gateway has opened", "epic");
    if (l->realm == REALM_MAIN && inEnd(*l) && std::hypot(l->pos.x, l->pos.z) < 200) grantXP(lv, *l, slain == 1 ? 1200 : 300, e.pos + vec3(0, 2, 0));
    l->stats["dragonsSlain"] = jint(l->stats, "dragonsSlain") + 1;
  }
  sub(lv.info.data, "flags")["enderDragon"] = true;
  // called back (the crystals, hearth.js): a fight for the fight's sake — hearts and scales come once, mostly
  if (truthy(e.data, "calledBack")) {
    struct D { const char* id; int lo, hi; float ch; };
    for (D d : {D{"dragon_scale", 5, 10, 1}, D{"emerald", 10, 20, 1}, D{"heart_crystal", 1, 1, 0.25f}, D{"diamond", 2, 4, 1}})
      if (rnd() < d.ch) dropItem(lv, e.realm, ItemStack(d.id, d.lo + (int)std::floor(rnd() * (d.hi - d.lo + 1))), e.pos + vec3(0, 0.5f, 0), true);
  }
  (void)S;
}

void endFightTick(Level& lv, EndStory& S, double dt) {
  gatewayTick(lv, S, dt);
  // endermen turning up (endmobs.js spawnMobs, once a second)
  if ((S.spawnT -= (float)dt) <= 0) {
    S.spawnT = 1;
    auto ps = livePlayers(lv, REALM_MAIN);
    if (!ps.empty() && rulesOn(lv, "mobSpawning")) {
      Player* p = ps[(size_t)(rnd() * ps.size()) % ps.size()];
      int dim = dimHere(*p);
      int count = 0;
      for (auto& u : lv.entities()) if (u->type == "enderman" && !u->removed && u->realm == p->realm && std::hypot(u->pos.x - p->pos.x, u->pos.z - p->pos.z) < 72) count++;
      int cap = dim == DIM_END ? 12 : 2;
      float chance = dim == DIM_END ? 0.7f : dim == DIM_NETHER ? 0.04f : dim == DIM_OVERWORLD && envDay(lv, dim) < 0.42f ? 0.05f : 0;
      if (count < cap && rnd() <= chance) {
        World& w = lv.world(REALM_MAIN);
        float a2 = rnd() * TAU, d = 18 + rnd() * 26;
        int x = ifloor(p->pos.x + std::cos(a2) * d), z = ifloor(p->pos.z + std::sin(a2) * d);
        int base = dimBase(dim);
        int from = dim == DIM_END ? base + 100 + vmap::END_DY : std::min(base + DIM_H - 4, ifloor(p->pos.y) + 12), to = dim == DIM_END ? base + 20 + vmap::END_DY : std::max(base + 2, ifloor(p->pos.y) - 16);
        if (w.isLoaded(x, from, z)) for (int y = from; y >= to; y--) {
          if (!solid(w.getBlock(x, y - 1, z)) || solid(w.getBlock(x, y, z)) || solid(w.getBlock(x, y + 1, z)) || solid(w.getBlock(x, y + 2, z)) || liquid(w.getBlock(x, y, z))) continue;
          if (dim == DIM_OVERWORLD && (w.getLight(x, y, z) & 15) > 0) break;
          spawnMob(lv, REALM_MAIN, "enderman", vec3(x + 0.5f, y, z + 0.5f));
          break;
        }
      }
    }
  }
  // the first time out here
  for (Player* p : livePlayers(lv, REALM_MAIN)) if (inEnd(*p) && !truthy(p->stats, "endSeen")) {
    p->stats["endSeen"] = 1; u32 id = p->id;
    S.later(4, [id](Level& l) { say(l, id, "Somewhere over the main island, a dragon keeps watch", "warn"); });
  }
  if ((S.endT -= (float)dt) > 0) return;
  S.endT = 1;
  const EndGen* gen = endGen(lv);
  if (!gen) return;
  json& E = endRec(lv);
  bool near = false;
  for (Player* p : livePlayers(lv, REALM_MAIN)) if (inEnd(*p) && std::hypot(p->pos.x, p->pos.z) < 170) near = true;
  Creature* dragon = nullptr;
  for (auto& u : lv.entities()) if (u->type == "ender_dragon" && !u->removed && u->realm == REALM_MAIN) { dragon = dynamic_cast<Creature*>(u.get()); break; }
  if (dragon && dragon->alive()) E["dragonHp"] = std::round(dragon->hp);
  if (!near || (jint(E, "slain") && !truthy(E, "again"))) return;   // (0.9.0.32) unless the crystals called it back (game/hearth.js)
  // the crystals on their pillars
  const auto& pillars = gen->pillars();
  std::vector<Creature*> spawned;
  for (size_t i = 0; i < pillars.size(); i++) {
    bool gone = false; for (auto& g : E["crystalsGone"]) if (g.is_number() && g.get<int>() == (int)i) gone = true;
    if (gone) continue;
    bool have = false;
    for (auto& u : lv.entities()) if (u->type == "end_crystal" && !u->removed && jint(u->data, "crystalIdx", -1) == (int)i) { have = true; break; }
    if (have) continue;
    const auto& pl = pillars[i];
    if (Creature* c = spawnMob(lv, REALM_MAIN, "end_crystal", vec3(pl.x + 0.5f, dimBase(DIM_END) + pl.h + 2, pl.z + 0.5f))) {
      c->data["crystalIdx"] = (int)i; c->kept = true; c->persistent = true; c->noGravity = true;
    }
  }
  // the dragon
  if (!dragon) {
    int fy = fountainY(lv);
    if (Creature* d = spawnMob(lv, REALM_MAIN, "ender_dragon", vec3(0.5f, fy + 34, -40.5f))) {
      d->kept = true; d->persistent = true; d->noGravity = true; d->state = "circle"; S.mem(*d).stT = 6; d->data["endBoss"] = true;
      if (num(E, "dragonHp") > 0) d->hp = std::min(d->maxHp, (float)num(E, "dragonHp"));
      if (jint(E, "slain")) { d->data["calledBack"] = true; d->data["noDrops"] = true; }
      if (!jint(E, "met")) {
        E["met"] = 1; lv.fx->sound("dragon_roar", vec3(0, fy + 30, 0), 300);
        sayAll(lv, "The Ender Dragon! Break the crystals on the pillars: they heal it", "epic");
      }
    }
  }
}

void endOnDeath(Level& lv, EndStory& S, Creature& c, Entity*) {
  if (c.type == "end_crystal") {
    json& E = endRec(lv);
    int idx = jint(c.data, "crystalIdx", -1);
    if (idx >= 0) { bool have = false; for (auto& g : E["crystalsGone"]) if (g.is_number() && g.get<int>() == idx) have = true; if (!have) E["crystalsGone"].push_back(idx); }
    vec3 at = c.pos; int realm = c.realm;
    int left = crystalsAlive(lv, &c);
    S.later(0, [at, realm](Level& l) { ExplodeOpts o; o.noBlocks = true; explode(l, realm, at + vec3(0, 1, 0), 3, o); });
    for (Player* l : livePlayers(lv, realm)) if (std::hypot(l->pos.x - at.x, l->pos.z - at.z) < 120)
      say(lv, l->id, left ? "A crystal shatters \xE2\x80\x94 " + std::to_string(left) + " left" : "The last crystal shatters! Nothing heals the dragon now", left ? "info" : "good");
  } else if (c.type == "ender_dragon" && inEnd(c)) dragonSlain(lv, S, c);
}

// ------------------------------------------------------------------ pearls and eyes in flight
class EnderOrb : public Entity {
public:
  EnderOrb() { type = "ender_orb"; w = 0.12f; h = 0.12f; noGravity = true; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    std::string kind = data.value("kind", "pearl");
    if (kind == "eye") { eyeStep(lv, dt); return; }
    auto* owner = dynamic_cast<Player*>(lv.entity(data.value("owner", 0u)));
    if (age > 10 || !owner || owner->dead || owner->realm != realm) { removed = true; return; }
    World& w = lv.world(realm);
    for (int s = 0; s < 4; s++) {
      float h4 = dt / 4; vel.y -= 18 * h4;
      vec3 n = pos + vel * h4;
      if (age > 0.15f) for (auto& u : lv.entities()) if (auto* c = dynamic_cast<Creature*>(u.get()))
        if (c->alive() && !c->removed && c->realm == realm && std::fabs(c->pos.x - n.x) < c->w + 0.2f && std::fabs(c->pos.z - n.z) < c->w + 0.2f && n.y > c->pos.y && n.y < c->pos.y + c->h) {
          DamageSrc d; d.knock = 1; damageEntity(lv, *c, 1, owner, "mob", d); land(lv, *owner, n, false, ivec3(0)); return;
        }
      BlockId id = w.getBlock(ifloor(n.x), ifloor(n.y), ifloor(n.z));
      if (solid(id) || liquid(id) || n.y < -40) { land(lv, *owner, pos, true, ivec3(ifloor(n.x), ifloor(n.y), ifloor(n.z))); return; }
      pos = n;
    }
    if (rnd() < dt * 30) sfx::spark(lv, pos, "end_mote", json{{"life", 0.5f}, {"size", 0.08f}});
  }
  void land(Level& lv, Player& p, vec3 at, bool hasHit, ivec3 hit) {
    removed = true;
    if (at.y < -30) return;
    World& w = lv.world(realm);
    particles(lv, "end_mote", at, 18, 2, 0);
    lv.fx->sound("ender_tp", at, 30);
    // find standing room at the spot it struck
    vec3 t = at;
    if (hasHit && !solid(w.getBlock(hit.x, hit.y + 1, hit.z)) && !solid(w.getBlock(hit.x, hit.y + 2, hit.z)) && std::fabs(hit.y + 1 - at.y) < 1.6f) t = vec3(hit.x + 0.5f, hit.y + 1, hit.z + 0.5f);
    if (p.riding) if (auto* m = dynamic_cast<Creature*>(lv.entity(p.riding))) dismount(lv, *m, p);
    particles(lv, "end_mote", p.pos + vec3(0, 1, 0), 20, 2, 0);
    teleport(lv, p, t);
    for (int k = 0; k < 4 && solid(w.getBlock(ifloor(p.pos.x), ifloor(p.pos.y + 0.1f), ifloor(p.pos.z))); k++) p.pos.y = std::floor(p.pos.y) + 1;   // unstick
    if (!p.creative()) { DamageSrc s; s.bypassArmor = true; damageEntity(lv, p, 3, nullptr, "fall", s); }
  }
  void eyeStep(Level& lv, float dt) {
    vec3 s0 = jvec(data["start"], pos);
    bool hasT = data.contains("target");
    vec3 T = jvec(data["target"]);
    float hover = (float)num(data, "hover", -1);
    if (hover < 0) {
      // up and away towards it, then a hang in the air
      float dx = hasT ? T.x - s0.x : std::cos(age * 3), dz = hasT ? T.z - s0.z : std::sin(age * 3), d = std::hypot(dx, dz); if (d < 1e-4f) d = 1;
      float far = std::min(12.f, d), gone = std::hypot(pos.x - s0.x, pos.z - s0.z);
      if (gone < far - 0.2f && age < 3) { pos.x += dx / d * 8 * dt; pos.z += dz / d * 8 * dt; pos.y += (d < 12 ? -1 : 2.2f) * dt; }
      else data["hover"] = 1.4f;
    } else {
      hover -= dt; data["hover"] = hover; pos.y += std::sin(age * 8) * 0.01f;
      if (hover <= 0) {
        removed = true;
        if (rnd() < 0.8f) dropItem(lv, realm, ItemStack("ender_eye", 1), pos - vec3(0, 0.2f, 0), false);
        else { particles(lv, "ender_eye", pos, 10, 1); lv.fx->sound("glass_break", pos); }
      }
    }
    if (rnd() < dt * 40) sfx::spark(lv, pos, "end_mote", json{{"life", 0.5f}, {"size", 0.08f}, {"vy", -0.4f}});
  }
};

// somewhere within eight blocks, on solid ground
}  // namespace dq::es
namespace dq {
bool chorusTeleport(Level& lv, Player& p) {
  using namespace es;
  World& w = lv.world(p.realm);
  for (int t = 0; t < 16; t++) {
    int x = ifloor(p.pos.x + (rnd() - 0.5f) * 16), z = ifloor(p.pos.z + (rnd() - 0.5f) * 16);
    int top = std::min(dimBase(dimHere(p) < 0 ? DIM_OVERWORLD : dimHere(p)) + DIM_H - 3, ifloor(p.pos.y) + 8);
    for (int y = top; y >= std::max(1, ifloor(p.pos.y) - 8); y--) {
      if (solid(w.getBlock(x, y - 1, z)) && !solid(w.getBlock(x, y, z)) && !solid(w.getBlock(x, y + 1, z)) && !liquid(w.getBlock(x, y, z))) {
        particles(lv, "end_mote", p.pos + vec3(0, 1, 0), 18, 2, 0);
        teleport(lv, p, vec3(x + 0.5f, y, z + 0.5f));
        lv.fx->soundUi(p.id, "ender_tp");
        particles(lv, "end_mote", p.pos + vec3(0, 1, 0), 18, 2, 0);
        return true;
      }
    }
  }
  return false;
}
vec3 endGatewaySpot(int k) { return es::gatewaySpot(k); }
}
namespace dq::es {

static ItemStack* heldOf(Player& p, const std::string& item) {
  if (p.held().id == item && p.held().n > 0) return &p.held();
  if (!p.offhand.empty() && p.offhand[0].id == item && p.offhand[0].n > 0) return &p.offhand[0];
  return nullptr;
}
static void take(Player& p, ItemStack& s) { if (p.creative()) return; if (--s.n <= 0) s = ItemStack(); }

bool endItemUse(Level& lv, EndStory& S, Player& p, const std::string& item, ivec3 hit, int face) {
  const ItemDef* d = reg().item(item);
  if (!d) return false;
  bool pearl = d->has("pearl"), eye = d->has("enderEye"), chorus = d->has("chorus"), snow = d->has("snowball");
  if (!pearl && !eye && !chorus && !snow) return false;
  World& w = lv.world(p.realm);
  if (eye && face >= 0) { BlockId b = w.getBlock(hit.x, hit.y, hit.z); if (b == B("end_portal_frame") || b == B("end_portal_frame_eye")) return false; }   // the portal system sets it in the frame
  ItemStack* held = heldOf(p, item);
  if (!held) return false;
  PlayerMem& P = S.pm(p);
  vec3 lk = lookVec(p), eyeP = p.pos + vec3(0, EYE, 0);
  if (pearl) {
    if (P.pearlCd > S.now) return true;
    P.pearlCd = S.now + 0.9;
    take(p, *held);
    auto e = std::make_unique<EnderOrb>(); e->realm = p.realm; e->pos = eyeP - vec3(0, 0.1f, 0) + lk * 0.5f; e->vel = lk * 19.f + vec3(0, 2, 0);
    e->data = {{"kind", "pearl"}, {"owner", p.id}};
    lv.spawn(std::move(e));
    p.swingT = 0.01f; lv.fx->soundUi(p.id, "bow");
    return true;
  }
  if (eye) {
    take(p, *held);
    bool hasT = false; vec3 T(0);
    if (inOverworld(p)) {
      float bd = 1e9f;
      for (auto& pl : plansNear(lv, DIM_OVERWORLD, "stronghold", ifloor(p.pos.x), ifloor(p.pos.z), 2048)) {
        float dd = std::hypot(pl.cx - p.pos.x, pl.cz - p.pos.z); if (dd < bd) { bd = dd; hasT = true; T = vec3(pl.cx, pl.y, pl.cz); }
      }
    }
    auto e = std::make_unique<EnderOrb>(); e->realm = p.realm; e->pos = eyeP + lk * 0.6f;
    e->data = {{"kind", "eye"}, {"owner", p.id}, {"start", vj(e->pos)}};
    if (hasT) e->data["target"] = vj(T);
    lv.spawn(std::move(e));
    p.swingT = 0.01f; lv.fx->soundUi(p.id, "ender_eye");
    if (!hasT) say(lv, p.id, inOverworld(p) ? "The eye spins, lost" : "The eye spins, lost: strongholds are in the Overworld");
    return true;
  }
  if (chorus) {
    take(p, *held);
    p.hp = std::min(p.maxHp, p.hp + 4); p.eatT = 0.45f; lv.fx->soundUi(p.id, "eat");
    particles(lv, "chorus_fruit", eyeP - vec3(0, 0.25f, 0) + lk * 0.4f, 6, 0.6f);
    chorusTeleport(lv, p);
    return true;
  }
  // (0.9.0.77) snowballs can be thrown
  if (P.snowCd > S.now) return true;
  P.snowCd = S.now + 0.25;
  take(p, *held);
  spawnProjectile(lv, p.realm, eyeP - vec3(0, 0.1f, 0) + lk * 0.5f, lk * 18.f + vec3(0, 2, 0), "snow", &p);
  p.swingT = 0.01f; lv.fx->soundUi(p.id, "bow");
  return true;
}

// ------------------------------------------------------------------ chorus plants: cut at the root, everything above comes down
void endBlockChanged(Level& lv, World& w, int x, int y, int z, BlockId o, BlockId n) {
  if (!def(o).has("chorus") || def(n).has("chorus")) return;
  EndStory* S = endStory(lv); if (!S) return;
  int realm = w.realm;
  S->later(0.09, [realm, x, y, z](Level& l) {
    World& W = l.world(realm);
    BlockId es = B("end_stone"), plant = B("chorus_plant");
    for (ivec3 q : {ivec3(x, y + 1, z), ivec3(x + 1, y, z), ivec3(x - 1, y, z), ivec3(x, y, z + 1), ivec3(x, y, z - 1)}) {
      BlockId id = W.getBlock(q.x, q.y, q.z); if (!def(id).has("chorus")) continue;
      BlockId under = W.getBlock(q.x, q.y - 1, q.z);
      if (def(under).has("chorus") || under == es) continue;
      W.setBlock(q.x, q.y, q.z, AIR, 0);
      if (id == plant && rnd() < 0.5f) dropItem(l, realm, ItemStack("chorus_fruit", 1), vec3(q) + vec3(0.5f), true);
      particles(l, "chorus_plant", vec3(q) + vec3(0.5f), 6, 1);
    }
  });
}

void registerEndCreatures() {
  CreatureHooks em;
  em.brain = endermanAI;
  em.beforeHurt = [](Level& lv, Creature& m, float&, const DamageSrc& src) {
    // nothing you shoot ever reaches it
    if (m.ghost) return true;
    if (src.tool == "arrow" || src.type == "arrow" || src.type == "projectile") { enderBlink(lv, m, m.pos.x, m.pos.z, 12, false); return false; }
    return true;
  };
  em.afterHurt = [](Level& lv, Creature& m, float dealt, const DamageSrc& src) {
    if (src.attacker && src.attacker != m.id) { m.angryT = 30; m.foe = src.attacker; if (EndStory* S = endStory(lv)) S->mem(m).lookT = 0; }
    if (dealt > 0 && m.alive() && rnd() < 0.3f) enderBlink(lv, m, m.pos.x, m.pos.z, 8, true);
  };
  setCreatureHooks("enderman", em);
  CreatureHooks cr; cr.brain = crystalAI; setCreatureHooks("end_crystal", cr);
  CreatureHooks ed; ed.brain = enderDragonAI; setCreatureHooks("ender_dragon", ed);
  registerEntityFactory("ender_orb", [](const std::string&) { return std::make_unique<EnderOrb>(); });
}

} // namespace dq::es
