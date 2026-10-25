// sim/dragons.cpp — game/dragons.js: the seven wild dragons asleep in their lairs (the lairs themselves are the
// structures workstream's "dragon_lair" plans: data {type, at, egg}), their flight (sleep → takeoff → circle →
// breath / dive / land), their breath, crashing through treetops, their eggs (kept warm they hatch a drake that is
// yours), drakes growing up and flying them; the wing-beating dragon rig (RIG_ANIM.dragon) and the end crystal's
// spin (RIG_ANIM.crystal). The Ender Dragon's own brain is in sim/end_fight.cpp; it shares the breath and rig.
#include "sim/endstory_int.h"
#include "sim/creature_rig.h"
#include "sim/physics.h"

namespace dq {
using namespace es;

// ------------------------------------------------------------------ what each dragon breathes
struct DragonKind { const char* key; const char* name; const char* breath; const char* fx; };
static const DragonKind KINDS[] = {
  {"verdant", "Verdant", "poison", "mana_heal"}, {"sand", "Sand", "fire", "mana_fire"}, {"frost", "Frost", "frost", "mana_frost"},
  {"bog", "Bog", "poison", "mana_heal"}, {"ember", "Ember", "fire", "mana_fire"}, {"storm", "Storm", "lightning", "mana_light"},
  {"tide", "Tide", "water", "mana_frost"}, {"ender", "Ender", "void", "mana_arcane"}};
static const DragonKind& kindOf(const std::string& k) { for (auto& d : KINDS) if (k == d.key) return d; return KINDS[0]; }
static const DragonKind& kindOf(const Creature& c) { return kindOf(c.T->j.value("dragon", std::string("verdant"))); }
static std::vector<std::string> breathTiles(const std::string& b) {
  if (b == "fire") return {"mana_fire", "mana_ember", "mana_fire", "mana_light"};
  if (b == "frost") return {"mana_frost", "mana_frost", "cloud_puff"};
  if (b == "poison") return {"mana_heal", "mana_heal", "smoke"};
  if (b == "lightning") return {"mana_light", "mana_arcane", "mana_light"};
  if (b == "water") return {"mana_frost", "cloud_puff", "mana_frost", "cloud_puff"};
  return {"mana_arcane", "end_mote", "mana_arcane", "end_mote"};   // void
}
static const float FINGER[3][3] = {{-0.06f, 17, 6}, {-0.55f, 15, 7}, {-1.05f, 12, 8}};

bool isDragon(const Creature& c) { return c.T->j.contains("dragon"); }

void dragonFace(Creature& e, float tx, float tz, float dt, float k) { e.yaw = turnTo(e.yaw, headingTo(e.pos, tx, tz), dt * k); }

// where its mouth is: a good guess from its size and heading (the server never poses the body)
static vec3 dragonMouth(const Creature& e) {
  const RigModel* M = rigModel(e.modelKey());
  float s = M ? M->scale : 2.3f, f = 28 * s / 16;
  return vec3(e.pos.x - std::sin(e.yaw) * f, e.pos.y + e.h * 0.8f, e.pos.z - std::cos(e.yaw) * f);
}

void dragonBreath(Level& lv, Creature& e, Entity& target, float dt) {
  const DragonKind& D = kindOf(e);
  vec3 m = dragonMouth(e), t = target.pos + vec3(0, 1, 0) - m;
  float l = std::max(1e-3f, glm::length(t)); vec3 u = t / l;
  // a roaring stream: its own colour at the heart, billowing out as it goes
  int n = std::min(14, (int)std::ceil(dt * 200));
  auto tiles = breathTiles(D.breath);
  for (size_t i = 0; i < tiles.size(); i++) {
    int k = n / (int)tiles.size() + ((int)i < n % (int)tiles.size() ? 1 : 0);
    if (k <= 0) continue;
    sfx::burst(lv, m + u * 0.3f, tiles[i], k, 1.6f, json{{"vx", u.x * 14}, {"vy", u.y * 14}, {"vz", u.z * 14}, {"life", 0.7f}, {"size", 0.17f}, {"grow", 3.2f}, {"spread", 0.3f}});
  }
  if (rnd() < dt * 20) sfx::spark(lv, m, std::string(D.breath) == "frost" ? "cloud_puff" : "smoke", json{{"vx", u.x * 6}, {"vy", u.y * 6 + 1}, {"vz", u.z * 6}, {"life", 1.2f}, {"size", 0.3f}, {"grow", 2}});
  if (std::string(D.breath) == "lightning" && rnd() < dt * 8) {
    // forks of lightning crackling through it
    vec3 p = m;
    for (int k = 0; k < 14; k++) { p += u * 0.7f + vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) * 0.6f; sfx::spark(lv, p, "mana_light", json{{"life", 0.15f}, {"size", 0.12f}}); }
  }
  if (e.ghost) return;
  EndStory* S = endStory(lv); if (!S) return;
  Mem& M = S->mem(e);
  M.breathHitT -= dt;
  if (M.breathHitT > 0) return;
  M.breathHitT = 0.45f;
  for (Entity* q : pawnsOf(lv, e.realm)) {
    vec3 pp = q->pos + vec3(0, 1, 0) - m; float d = glm::length(pp);
    if (d > 12 || glm::dot(pp, u) / std::max(d, 1e-3f) < 0.86f) continue;
    std::string br = D.breath;
    hurtPawn(lv, *q, br == "lightning" ? 4 : 3, &e, "magic", 1, br == "frost" ? "frost" : br == "fire" ? "fire" : "");
    if (!q->isPlayer()) continue;
    if (br == "fire") igniteEntity(*q, 3);
    else if (br == "poison") setPlayerEffect(*q, "poison", std::max(playerEffect(*q, "poison"), 5.f));
    else if (br == "frost") { setPlayerEffect(*q, "chill", std::max(playerEffect(*q, "chill"), 4.f)); setPlayerEffect(*q, "slow", std::max(playerEffect(*q, "slow"), 3.f)); }
    else if (br == "water") { setPlayerEffect(*q, "burn", 0); setPlayerEffect(*q, "slow", std::max(playerEffect(*q, "slow"), 2.f)); q->vel.x += u.x * 9; q->vel.z += u.z * 9; q->vel.y = std::max(q->vel.y, 3.f); }
  }
}

static bool boxClear(World& w, vec3 lo, vec3 hi) {
  for (int y = ifloor(lo.y); y <= ifloor(hi.y - 1e-3f); y++) for (int z = ifloor(lo.z); z <= ifloor(hi.z - 1e-3f); z++) for (int x = ifloor(lo.x); x <= ifloor(hi.x - 1e-3f); x++)
    if (solid(w.getBlockPhys(x, y, z))) return false;
  return true;
}
// a dragon in the air crashes through the treetops rather than getting caught in them
void dragonUnstick(Level& lv, Creature& e, Mem& M, float dt) {
  if (e.noGravity && (e.hitWall || e.hitCeiling)) M.stuckT += dt; else M.stuckT = std::max(0.f, M.stuckT - dt);
  if (M.stuckT < 0.2f) return;
  World& w = lv.world(e.realm);
  int n = 0;
  for (int y = ifloor(e.pos.y); y <= ifloor(e.pos.y + e.h + 1); y++) for (int z = ifloor(e.pos.z - e.w - 1); z <= ifloor(e.pos.z + e.w + 1); z++) for (int x = ifloor(e.pos.x - e.w - 1); x <= ifloor(e.pos.x + e.w + 1); x++) {
    BlockId id = w.getBlock(x, y, z);
    if (id && (isLeaves(id) || (!solid(id) && !liquid(id) && reg().tReplaceable[id]))) {
      std::string key = def(id).key;
      w.setBlock(x, y, z, AIR, 0);
      if (n++ < 6) lv.fx->particles("burst", vec3(x + 0.5f, y + 0.5f, z + 0.5f), 4, json{{"tile", key}, {"speed", 2}});
    }
  }
  // still wedged: up and out
  if (M.stuckT > 2 && boxClear(w, vec3(e.pos.x - e.w, e.pos.y + 1.5f, e.pos.z - e.w), vec3(e.pos.x + e.w, e.pos.y + 1.5f + e.h, e.pos.z + e.w))) { e.pos.y += 1.5f; M.stuckT = 0.5f; }
  else if (M.stuckT > 4 && e.data.contains("home")) { vec3 h = jvec(e.data["home"]); e.pos = h + vec3(0, 6, 0); M.stuckT = 0; }
}

// ------------------------------------------------------------------ the wild dragons
static OptMove lairDragonAI(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  EndStory* S = endStory(lv); if (!S) return std::nullopt;
  Mem& M = S->mem(m);
  const CreatureType& T = *m.T;
  m.hasFlyVy = true;
  if (m.hoverT > 0) m.hoverT -= dt;
  dragonUnstick(lv, m, M, dt);
  if (m.state == "idle") m.state = m.data.value("dstate", std::string("circle"));   // back from a save
  if (m.data.value("dstate", std::string()) != m.state) m.data["dstate"] = m.state;
  if (!p) { m.noGravity = false; return Move{0, m.yaw}; }
  const std::string st = m.state;
  if (st == "sleep") {
    m.noGravity = false; m.sleeping = true;
    if (rnd() < dt * 0.4f) { vec3 mo = dragonMouth(m); particles(lv, std::string(kindOf(m).breath) == "fire" ? "flame" : "smoke", mo - vec3(0, 1.2f, 0), 3, 0.25f, -1); }
    if ((dist < 22 && !(pawnSneaking(p) && dist > 10)) || m.hurtTime > 0) {
      m.state = "takeoff"; M.stT = 1.6f; m.noGravity = true; m.flyVy = 7; m.sleeping = false;
      lv.fx->sound("dragon_roar", m.pos + vec3(0, 2, 0), 90);
      sayNear(lv, m.realm, m.pos, 60, capLabel(T.label) + " wakes!", "warn");
    }
    return Move{0, m.yaw};
  }
  m.sleeping = false;
  if (st == "takeoff") {
    M.stT -= dt; m.flyVy = 7;
    if (M.stT <= 0) { m.state = "circle"; M.stT = 4 + rnd() * 3; M.orbit = rnd() * TAU; }
    return Move{2, m.yaw};
  }
  if (st == "circle") {
    m.noGravity = true; M.stT -= dt;
    M.orbit += dt * 0.45f;
    float gx = p->pos.x + std::cos(M.orbit) * 16, gz = p->pos.z + std::sin(M.orbit) * 16, alt = p->pos.y + 12 + std::sin(m.age * 0.7f) * 2;
    m.flyVy = clampf((alt - m.pos.y) * 1.2f, -6, 6);
    if ((M.flapT -= dt) <= 0) { M.flapT = 0.75f; lv.fx->sound("dragon_flap", m.pos + vec3(0, 2, 0), 40); }
    if (M.stT <= 0) {
      float r = rnd(); bool low = m.hp < m.maxHp * 0.7f;
      m.state = r < 0.45f ? "breath" : (r < 0.8f || !low) ? "dive" : "land"; M.stT = m.state == "land" ? 9 : 6;
      if (m.state == "breath") M.hasBreathPt = false;
    }
    return Move{T.speed * 2.2f, headingTo(m.pos, gx, gz)};
  }
  if (st == "breath") {
    m.noGravity = true; M.stT -= dt;
    if (!M.hasBreathPt) { float a = std::atan2(m.pos.z - p->pos.z, m.pos.x - p->pos.x); M.breathPt = vec3(p->pos.x + std::cos(a) * 9, p->pos.y + 6, p->pos.z + std::sin(a) * 9); M.hasBreathPt = true; }
    vec3 b = M.breathPt; float d = std::hypot(b.x - m.pos.x, b.z - m.pos.z);
    m.flyVy = clampf((b.y - m.pos.y) * 1.5f, -6, 6);
    if (d > 2.5f && !(m.breathT > 0)) return Move{T.speed * 2, headingTo(m.pos, b.x, b.z)};
    dragonFace(m, p->pos.x, p->pos.z, dt, 4); m.hoverT = 0.5f;
    if (!(m.breathT > 0) && !M.breathed) { m.breathT = 2.4f; M.breathed = true; lv.fx->sound("dragon_breath", m.pos + vec3(0, 2, 0), 50); }
    if (m.breathT > 0) { m.breathT -= dt; dragonBreath(lv, m, *p, dt); }
    else { M.breathed = false; m.state = "circle"; M.stT = 4 + rnd() * 3; }
    if (M.stT <= -4) { m.breathT = 0; M.breathed = false; m.state = "circle"; M.stT = 3; }
    return Move{0, m.yaw};
  }
  if (st == "dive") {
    m.noGravity = true; M.stT -= dt;
    float d3 = glm::distance(p->pos + vec3(0, 1, 0), m.pos);
    m.flyVy = clampf((p->pos.y + 0.6f - m.pos.y) * 2.5f, -11, 8);
    if (d3 < 3 + m.w && m.attackCd <= 0) {
      m.attackCd = 1.2f; m.swingT = 0.3f;
      hurtPawn(lv, *p, T.damage, &m, "mob", 6);
      lv.fx->sound("dragon_bite", m.pos + vec3(0, 1, 0));
      m.state = "climb"; M.stT = 1.6f;
    }
    if (M.stT <= 0) { m.state = "climb"; M.stT = 1.4f; }
    return Move{T.speed * 3, toPlayer};
  }
  if (st == "climb") {
    M.stT -= dt; m.flyVy = 8;
    if (M.stT <= 0) { m.state = "circle"; M.stT = 4 + rnd() * 3; }
    return Move{T.speed * 2, m.yaw};
  }
  if (st == "land") {
    M.stT -= dt;
    if (!m.onGround) { m.noGravity = true; m.flyVy = -5; return Move{dist > 6 ? T.speed * 1.5f : 0, toPlayer}; }
    m.noGravity = false;
    if (dist < 3.2f + m.w && std::fabs(dyp) < 3 && m.attackCd <= 0) {
      m.attackCd = 1.4f; m.swingT = 0.3f;
      hurtPawn(lv, *p, T.damage, &m, "mob", 5); lv.fx->sound("dragon_bite", m.pos + vec3(0, 1, 0));
    }
    if (M.stT <= 0) { m.state = "takeoff"; M.stT = 1.4f; m.noGravity = true; lv.fx->sound("dragon_roar", m.pos + vec3(0, 2, 0), 70); }
    std::optional<float> nd; if (dist > 4) nd = m.navDir(lv, p->pos, dt);
    return Move{dist < 3 ? 0 : T.speed, nd ? *nd : toPlayer};
  }
  m.state = "circle"; M.stT = 3;
  return Move{0, m.yaw};
}

// ------------------------------------------------------------------ drakes: growing up, and flying them
static const std::vector<std::string>& drakeFoods(const std::string& type) {
  static const std::vector<std::string> F = {"beef", "porkchop", "mutton", "chicken", "cod", "salmon", "cooked_beef", "cooked_porkchop", "cooked_mutton", "cooked_chicken", "rabbit", "cooked_rabbit"};
  static const std::vector<std::string> E = [] { auto v = F; v.push_back("chorus_fruit"); return v; }();
  return type == "ender_drake" ? E : F;
}
static OptMove drakeAI(Level& lv, Creature& m, Entity*, float, float, float, float dt) {
  if (m.baby && !m.ghost) {
    m.growT -= dt;
    if (m.growT <= 0) {
      m.grow(lv);
      if (Player* o = m.ownerPawn(lv)) say(lv, o->id, "Your " + (m.petName.empty() ? std::string("drake") : m.petName) + " is fully grown! Put a saddle on it to fly", "epic");
    }
  }
  auto* r = m.riddenBy ? dynamic_cast<Player*>(lv.entity(m.riddenBy)) : nullptr;
  if (!r || r->riding != m.id || r->dead) {
    if (m.riddenBy) { m.riddenBy = 0; }
    if (m.noGravity) m.noGravity = false;
    return std::nullopt;   // pets.js: follow / stay / defend
  }
  // in the saddle: forward goes where you look, jump to climb
  EndStory* S = endStory(lv);
  m.stay = false; m.sitting = false; m.hasFlyVy = true;
  const auto& in = m.rideIn;
  float f = in.fwd, s = in.strafe;
  if (in.jump && !m.noGravity) { m.noGravity = true; m.flyVy = 6; lv.fx->sound("dragon_flap", m.pos + vec3(0, 1, 0)); }
  bool air = m.noGravity;
  float sp = air ? (in.sprint ? 17 : 11) : (in.sprint ? 8 : 5.5f);
  if (air) {
    float climb = f > 0.1f ? std::sin(r->pitch) * sp * f : 0;
    m.flyVy = clampf(climb + (in.jump ? 6 : 0) - (in.descend ? 9 : 0), -14, 10);
    if (m.onGround && climb <= 0.5f && !in.jump) m.noGravity = false;
    if (S) { Mem& M = S->mem(m); if ((M.flapT -= dt) <= 0) { M.flapT = 0.8f; lv.fx->sound("dragon_flap", m.pos + vec3(0, 1, 0), 24); } }
  }
  float vx = -std::sin(r->yaw) * f + std::cos(r->yaw) * s, vz = -std::cos(r->yaw) * f - std::sin(r->yaw) * s, mm = std::hypot(vx, vz);
  if (mm < 0.1f) return Move{0, m.yaw};
  return Move{sp * std::min(1.f, mm), std::atan2(-vx, -vz)};
}
static bool drakeUse(Level& lv, Player& p, Creature& e, bool sneaking) {
  bool mine = e.isPetOf(&p);
  if (!mine) return false;
  ItemStack& held = p.held();
  const auto& foods = drakeFoods(e.type);
  // feeding a young one makes it grow faster
  if (e.baby && !held.empty() && std::find(foods.begin(), foods.end(), held.id) != foods.end()) {
    e.growT = std::max(0.f, e.growT - 150); if (!p.creative() && --held.n <= 0) held = ItemStack();
    lv.fx->soundUi(p.id, "eat"); hearts(lv, e.pos + vec3(0, e.h, 0), 3); p.swingT = 0.01f;
    say(lv, p.id, e.growT > 0 ? "It grows! (" + std::to_string((int)std::ceil(e.growT / 60)) + " min to go)" : "It's nearly grown!", "good");
    return true;
  }
  if (!held.empty() && held.id == "saddle") {
    if (e.baby) { say(lv, p.id, "A saddle won't fit that"); return true; }
    if (e.saddled) { say(lv, p.id, "It already has a saddle"); return true; }
    e.saddled = true; if (!p.creative() && --held.n <= 0) held = ItemStack(); lv.fx->soundUi(p.id, "equip"); say(lv, p.id, "Saddled \xE2\x80\x94 use it to ride", "good");
    return true;
  }
  if (!held.empty() && std::find(foods.begin(), foods.end(), held.id) != foods.end() && e.hp < e.maxHp) {
    e.hp = std::min(e.maxHp, e.hp + 4); if (!p.creative() && --held.n <= 0) held = ItemStack(); lv.fx->soundUi(p.id, "eat"); hearts(lv, e.pos + vec3(0, e.h, 0), 2); return true;
  }
  if (held.empty() && !sneaking && e.saddled && !e.baby && !p.riding) {
    if (e.riddenBy) { say(lv, p.id, "Someone is riding it"); return true; }
    e.riddenBy = p.id; p.riding = e.id; p.vel = vec3(0); p.yaw = e.yaw;
    say(lv, p.id, "WASD \xC2\xB7 jump to take off and climb \xC2\xB7 look up or down to fly that way \xC2\xB7 sprint to race \xC2\xB7 sneak to fly down, and to get off once landed");
    return true;
  }
  return false;
}

// ------------------------------------------------------------------ the host's dragon keeping: lairs, eggs
static json& dragonRec(Level& lv, const std::string& id) { return sub(sub(lv.info.data, "dragons"), id.c_str()); }

static bool eggHeat(BlockId id) {
  static std::vector<BlockId> H;
  if (H.empty()) for (const char* k : {"fire", "soul_fire", "lava", "campfire", "soul_campfire", "magma_block", "lit_furnace", "lit_smoker", "lit_blast_furnace"}) if (BlockId b = B(k)) H.push_back(b);
  return std::find(H.begin(), H.end(), id) != H.end();
}
static Player* nearestPlayer(Level& lv, int realm, vec3 at, float maxD) {
  Player* best = nullptr; float bd = maxD;
  for (Player* p : livePlayers(lv, realm)) { float d = std::hypot(p->pos.x - at.x, p->pos.z - at.z); if (d < bd) { bd = d; best = p; } }
  return best;
}
static void eggTick(Level& lv, EndStory& S, double dt) {
  json& eggs = sub(lv.info.data, "eggs", true);
  if (eggs.empty()) return;
  if ((S.eggT -= (float)dt) > 0) return;
  S.eggT = 1;
  for (size_t i = 0; i < eggs.size();) {
    json& e = eggs[i];
    int realm = jint(e, "realm"), x = jint(e, "x"), y = jint(e, "y"), z = jint(e, "z");
    World* w = lv.worldIfOpen(realm);
    if (!w || !w->isLoaded(x, y, z)) { i++; continue; }
    BlockId id = w->getBlock(x, y, z);
    std::string kind = def(id).str("dragonEgg");
    if (kind.empty()) { eggs.erase(eggs.begin() + i); continue; }
    vec3 at(x + 0.5f, y + 0.5f, z + 0.5f);
    // a sea dragon's egg wants the sea; the rest want warmth
    bool sea = kind == "tide", warm = false;
    for (int dy = -1; dy <= 1 && !warm; dy++) for (int dz = -2; dz <= 2 && !warm; dz++) for (int dx = -2; dx <= 2; dx++) {
      BlockId b = w->getBlock(x + dx, y + dy, z + dz);
      if (sea ? reg().tLiquid[b] == 1 : eggHeat(b)) { warm = true; break; }
    }
    e["warm"] = warm;   // (0.9.0.79) the egg's tag: hatching or paused
    if (!warm) {
      if (!truthy(e, "coldSaid")) { e["coldSaid"] = 1; if (Player* o = nearestPlayer(lv, realm, at, 10)) say(lv, o->id, sea ? "The egg is drying out. Set it in or beside water" : "The egg is cold. Set it near a fire, lava or a campfire to keep it warm"); }
      i++; continue;
    }
    int t = jint(e, "t") + 1; e["t"] = t;
    if (t == 1) if (Player* o = nearestPlayer(lv, realm, at, 12)) say(lv, o->id, sea ? "The egg glistens in the water. It stirs\xE2\x80\xA6" : "The egg is warming. It stirs\xE2\x80\xA6", "good");
    if (rnd() < 0.15f + t / 300.f * 0.5f) { lv.fx->sound("egg_crack", at); particles(lv, "smoke", at + vec3(0, 0.2f, 0), 3, 0.3f); }
    if (t >= 300) {
      std::string ownerName = e.value("owner", std::string());
      eggs.erase(eggs.begin() + i);
      w->setBlock(x, y, z, AIR, 0);
      Creature* m = spawnMob(lv, realm, kind + "_drake", vec3(x + 0.5f, y, z + 0.5f));
      if (!m) continue;
      m->makeBaby(); m->growT = 1200;
      Player* owner = ownerName.empty() ? nullptr : findPlayer(lv, ownerName);
      if (!owner || owner->dead) owner = nearestPlayer(lv, realm, at, 1e9f);
      if (owner) { m->owner = owner->name; m->tamed = true; m->kept = true; }
      sfx::burst(lv, at + vec3(0, 0.1f, 0), kindOf(kind).fx, 30, 3, json{{"life", 0.9f}, {"size", 0.08f}});
      lv.fx->sound("dragon_roar", at + vec3(0, 0.5f, 0), 20);
      std::string nm = kindOf(kind).name; for (auto& c : nm) c = (char)std::tolower((unsigned char)c);
      for (Player* l : livePlayers(lv, realm)) if (std::hypot(l->pos.x - at.x, l->pos.z - at.z) < 30)
        say(lv, l->id, "A " + nm + " drake has hatched" + (owner ? std::string(" \xE2\x80\x94 it's ") + (owner == l ? "yours" : owner->name + "'s") : std::string()) + "! Feed it meat to help it grow.", "epic");
      continue;
    }
    i++;
  }
}

void es::dragonsTick(Level& lv, EndStory& S, double dt) {
  // (0.9.0.79) eggs set down without being noticed (older saves, a missed place) are found by looking around
  static double scan = 0;
  if ((scan -= dt) <= 0) {
    scan = 3;
    json& eggs = sub(lv.info.data, "eggs", true);
    for (Player* l : livePlayers(lv)) {
      World& w = lv.world(l->realm);
      int px = ifloor(l->pos.x), py = ifloor(l->pos.y), pz = ifloor(l->pos.z);
      for (int dy = -3; dy <= 3; dy++) for (int dz = -6; dz <= 6; dz++) for (int dx = -6; dx <= 6; dx++) {
        int x = px + dx, y = py + dy, z = pz + dz;
        if (def(w.getBlock(x, y, z)).str("dragonEgg").empty()) continue;
        bool have = false;
        for (auto& e : eggs) if (jint(e, "x") == x && jint(e, "y") == y && jint(e, "z") == z && jint(e, "realm") == l->realm) { have = true; break; }
        if (!have) eggs.push_back(json{{"x", x}, {"y", y}, {"z", z}, {"realm", l->realm}, {"t", 0}, {"owner", l->name}});
      }
    }
  }
  eggTick(lv, S, dt);
  if ((S.dragT -= (float)dt) > 0) return;
  S.dragT = 1.5f;
  for (Player* p : livePlayers(lv, REALM_MAIN)) {
    int dim = dimHere(*p);
    if (dim == DIM_END) continue;
    for (const Plan& L : plansNear(lv, dim, "dragon_lair", ifloor(p->pos.x), ifloor(p->pos.z), 50)) {
      if (truthy(dragonRec(lv, L.id), "slain")) continue;
      bool up = false;
      for (auto& u : lv.entities()) if (!u->removed && u->data.value("lairId", std::string()) == L.id) { if (auto* c = dynamic_cast<Creature*>(u.get()); c && c->alive()) up = true; }
      if (up) continue;
      vec3 at = jvec(L.data["at"], vec3(L.cx, L.y - dimBase(dim), L.cz)); at.y += dimBase(dim);
      World& w = lv.world(REALM_MAIN);
      if (!w.isLoaded(ifloor(at.x), ifloor(at.y), ifloor(at.z))) continue;
      std::string type = L.data.value("type", std::string("verdant")) + "_dragon";
      if (Creature* e = spawnMob(lv, REALM_MAIN, type, at + vec3(0.5f, 2.1f, 0.5f))) {
        e->data["lair"] = true; e->data["lairId"] = L.id; e->data["home"] = vj(at + vec3(0.5f, 2, 0.5f));
        e->state = "sleep"; e->sleeping = true; e->yaw = rnd() * TAU; e->kept = true; e->persistent = true;
      }
    }
  }
}

void es::dragonsOnDeath(Level& lv, EndStory&, Creature& c, Entity*) {
  std::string lair = c.data.value("lairId", std::string());
  if (lair.empty()) return;
  dragonRec(lv, lair)["slain"] = 1;
  for (Player* l : livePlayers(lv, c.realm)) if (std::hypot(l->pos.x - c.pos.x, l->pos.z - c.pos.z) < 80) {
    say(lv, l->id, capLabel(c.T->label) + " is slain! Its egg lies in the nest.", "epic");
    grantXP(lv, *l, 250, c.pos + vec3(0, 2, 0));
  }
  c.noGravity = false;
}

// setting an egg down
void es::dragonsBlockChanged(Level& lv, World& w, int x, int y, int z, BlockId o, BlockId n) {
  if (def(n).str("dragonEgg").empty()) return;
  json& eggs = sub(lv.info.data, "eggs", true);
  for (auto& e : eggs) if (jint(e, "x") == x && jint(e, "y") == y && jint(e, "z") == z && jint(e, "realm") == w.realm) return;
  Player* near = nearestPlayer(lv, w.realm, vec3(x, y, z), 1e9f);
  eggs.push_back(json{{"x", x}, {"y", y}, {"z", z}, {"realm", w.realm}, {"t", 0}, {"owner", near ? json(near->name) : json(nullptr)}});
  (void)o;
}

// ------------------------------------------------------------------ hit volumes (0.9.0.76): body, neck and head
static std::vector<std::pair<vec3, vec3>> dragonBoxes(const Creature& e) {
  float k = (e.type == "ender_dragon" ? 3.3f : 2.3f) / 16, fx = -std::sin(e.yaw), fz = -std::cos(e.yaw);
  static const float S[5][4] = {{7, 9, 0, 24}, {-7, 9, 0, 24}, {-20, 4, 8, 22}, {21, 5, 12, 30}, {32, 6, 12, 32}};
  std::vector<std::pair<vec3, vec3>> out;
  for (auto& s : S) {
    float cx = e.pos.x + fx * s[0] * k, cz = e.pos.z + fz * s[0] * k, hw = s[1] * k;
    out.push_back({vec3(cx - hw, e.pos.y + s[2] * k, cz - hw), vec3(cx + hw, e.pos.y + s[3] * k, cz + hw)});
  }
  return out;
}
float dragonRayT(const Creature& c, vec3 o, vec3 d) {
  float best = -1;
  for (auto& [lo, hi] : dragonBoxes(c)) {
    float t0 = 0, t1 = 1e9f; bool ok = true;
    for (int a = 0; a < 3 && ok; a++) {
      if (std::fabs(d[a]) < 1e-8f) { if (o[a] < lo[a] || o[a] > hi[a]) ok = false; continue; }
      float u = (lo[a] - o[a]) / d[a], v = (hi[a] - o[a]) / d[a]; if (u > v) std::swap(u, v);
      t0 = std::max(t0, u); t1 = std::min(t1, v); if (t0 > t1) ok = false;
    }
    if (ok && (best < 0 || t0 < best)) best = t0;
  }
  return best;
}
bool dragonHas(const Creature& c, vec3 p) {
  for (auto& [lo, hi] : dragonBoxes(c)) if (p.x > lo.x - 0.1f && p.x < hi.x + 0.1f && p.y > lo.y && p.y < hi.y && p.z > lo.z - 0.1f && p.z < hi.z + 0.1f) return true;
  return false;
}

// ------------------------------------------------------------------ the rigs' movements (client and server alike)
namespace {
struct PW {
  const RigModel& M; RigAnimOut& o; vec3 scratch{0};
  vec3& at(const std::string& n) { int i = M.find(n); if (i < 0) { scratch = vec3(0); return scratch; } if (!o.has[i]) { o.has[i] = 1; o.rot[i] = vec3(0); } return o.rot[i]; }
  void set(const std::string& n, float x, float y, float z) { at(n) = vec3(x, y, z); }
  void add(const std::string& n, float x, float y, float z) { at(n) += vec3(x, y, z); }
};
}
static void animDragon(Creature& m, const RigModel& d, RigAnimOut& R) {
  PW P{d, R};
  float t = m.age, seed = m.id * 0.37f;
  float dt = m.life.sqAge < 0 ? 0 : clampf(t - m.life.sqAge, 0, 0.1f); m.life.sqAge = t;
  bool fly = m.noGravity && !m.onGround; float a = m.walkAmt, ph = m.walk;
  // banking into turns
  float last = m.life.sqYaw == 0 ? m.yaw : m.life.sqYaw;
  float dy = std::fmod(m.yaw - last + PI * 3, TAU) - PI; m.life.sqYaw = m.yaw;
  float bank = fly && dt > 0 ? clampf(dy / dt * 0.3f, -0.55f, 0.55f) : 0;
  m.life.sqRoll += (bank - m.life.sqRoll) * std::min(1.f, dt * 3);
  // the head follows what it looks at
  float hy = m.headYawRel * 0.45f, hp = m.headPitch * 0.5f;
  P.add("neck2", 0, hy, 0); P.add("head", -hp, hy, 0);
  if (fly) {
    bool hover = m.hoverT > 0 || m.breathT > 0, climb = m.vel.y > 2.5f || m.state == "takeoff" || m.state == "climb";
    bool glide = !hover && !climb && m.vel.y < -1.5f;
    float rate = hover ? 8 : climb ? 7.5f : glide ? 2.5f : 5.2f, amp = hover ? 1.0f : climb ? 0.95f : glide ? 0.07f : 0.72f;
    if (m.life.tilt < 0) m.life.tilt = seed * 7;
    m.life.tilt += dt * rate;
    float w = m.life.tilt;
    // a quick downstroke and a slower upstroke; the outer wing lags and whips through; on the way up the wing
    // sweeps back and the fingers half close, to slip through the air
    float down = std::sin(w + 0.45f * std::sin(w)), lag = std::sin(w - 0.9f), up = std::max(0.f, std::cos(w)) * (amp > 0.3f ? 1 : 0);
    float dihedral = glide ? 0.16f : 0.06f;
    for (int side = 0; side < 2; side++) {
      std::string S = side ? "L" : "R"; float s = side ? -1.f : 1.f;
      P.set("wing" + S, -0.08f * up, -s * (0.12f + 0.35f * up), s * (dihedral + amp * down));
      P.set("wing" + S + "2", 0, -s * 0.45f * up, s * amp * 0.6f * lag);
      for (int k = 0; k < 3; k++) P.set("wing" + S + "f" + std::to_string(k), 0, -s * FINGER[k][0] * up * 0.55f, s * amp * 0.3f * lag);
    }
    // legs tucked back in flight, hanging forward when hovering
    float legF = hover ? 0.25f : -0.7f, legB = hover ? 0.1f : -1.1f;
    P.set("legFR", legF + std::sin(t * 2 + 1) * 0.05f, 0, 0); P.set("legFL", legF + std::sin(t * 2) * 0.05f, 0, 0);
    P.set("legBR", legB, 0, 0); P.set("legBL", legB, 0, 0);
    for (const char* L : {"shinFR", "shinFL", "shinBR", "shinBL"}) P.set(L, hover ? -0.4f : 0.7f, 0, 0);
    R.pitch = clampf(m.vel.y * 0.045f, -0.45f, 0.45f) + (hover ? 0.18f : 0);
    R.roll = m.life.sqRoll;
    R.lift = -down * 0.14f * amp;
    // neck reaching forward; the tail a rudder, waving slowly
    P.add("neck1", hover ? -0.1f : -0.4f, 0, 0); P.add("neck2", hover ? 0.05f : 0.15f, 0, 0); P.add("head", hover ? 0.1f : 0.2f, 0, 0);
    float tw = t * 1.4f + seed;
    P.add("tail1", -R.pitch * 0.4f + down * 0.04f, std::sin(tw) * 0.08f - m.life.sqRoll * 0.4f, 0);
    P.add("tail2", 0, std::sin(tw - 0.7f) * 0.14f, 0); P.add("tail3", 0, std::sin(tw - 1.4f) * 0.2f, 0);
  } else {
    // on the ground: four-footed, wings folded in a Z along its sides (arm back, forearm forward, fingers back)
    float sw = std::sin(ph) * 0.5f * a, lift = std::max(0.f, -std::cos(ph)) * 0.6f * a, lift2 = std::max(0.f, std::cos(ph)) * 0.6f * a;
    P.set("legFR", sw, 0, 0); P.set("legBL", sw, 0, 0); P.set("legFL", -sw, 0, 0); P.set("legBR", -sw, 0, 0);
    P.set("shinFR", -lift, 0, 0); P.set("shinBL", -lift, 0, 0); P.set("shinFL", -lift2, 0, 0); P.set("shinBR", -lift2, 0, 0);
    float breathe = std::sin(t * 1.3f + seed);
    for (int side = 0; side < 2; side++) {
      std::string S = side ? "L" : "R"; float s = side ? -1.f : 1.f;
      P.set("wing" + S, 0.95f, -s * 1.42f, s * (0.42f + breathe * 0.02f));
      P.set("wing" + S + "2", 0, 0, s * 2.8f);
      for (int k = 0; k < 3; k++) P.set("wing" + S + "f" + std::to_string(k), 0, -s * FINGER[k][0] * 0.85f, -s * 2.75f);
    }
    float tw = t * 0.9f + seed;
    P.add("tail1", 0, std::sin(tw) * 0.1f * (1 + a), 0); P.add("tail2", 0, std::sin(tw - 0.6f) * 0.16f, 0); P.add("tail3", 0, std::sin(tw - 1.2f) * 0.22f, 0);
    P.add("neck1", std::sin(ph * 2) * 0.04f * a, 0, 0);
    R.lift = breathe * 0.015f - std::fabs(std::sin(ph)) * 0.03f * a;
    if (m.sleeping || m.state == "sleep") {
      // lying down: belly on the ground, forepaws out in front, hind legs folded under, the head down by them
      float px = d.scale / 16;
      P.add("neck1", -0.95f, 0.45f, 0); P.add("neck2", 0.15f, 0.35f, 0); P.add("head", 0.5f, 0.2f, 0);
      P.add("tail1", 0.15f, 0.55f, 0); P.add("tail2", 0, 0.5f, 0); P.add("tail3", 0, 0.4f, 0);
      P.set("legFR", 1.35f, 0.15f, 0); P.set("legFL", 1.35f, -0.15f, 0); P.set("shinFR", -0.2f, 0, 0); P.set("shinFL", -0.2f, 0, 0);
      P.set("legBR", 1.15f, 0.2f, 0); P.set("legBL", 1.15f, -0.2f, 0); P.set("shinBR", -2.45f, 0, 0); P.set("shinBL", -2.45f, 0, 0);
      R.lift = -9 * px + std::sin(t * 0.8f + seed) * 0.25f * px;
    }
  }
  // the jaw: open wide to breathe or bite, a slow idle working otherwise
  if (m.breathT > 0 || m.swingT > 0) { P.set("jaw", -0.6f, 0, 0); P.add("head", -0.15f, 0, 0); }
  else P.set("jaw", -0.04f - std::max(0.f, std::sin(t * 0.6f + seed)) * 0.06f, 0, 0);
}
static void animCrystal(Creature& m, const RigModel& d, RigAnimOut& R) {
  PW P{d, R};
  float t = m.age;
  P.set("frameA", t * 1.3f, t * 0.9f, t * 0.5f); P.set("frameB", -t * 1.1f, t * 1.7f, 0); P.set("core", t * 2.1f, t * 1.3f, t * 1.7f);
  R.lift = 0;
}

void es::registerDragons() {
  registerRigAnim("dragon", animDragon);
  registerRigAnim("crystal", animCrystal);
  for (auto& k : KINDS) {
    std::string key = k.key;
    if (key != "ender") { CreatureHooks h; h.brain = lairDragonAI; setCreatureHooks(key + "_dragon", h); }
    CreatureHooks d; d.brain = drakeAI; d.onUse = drakeUse; setCreatureHooks(key + "_drake", d);
  }
}

} // namespace dq
