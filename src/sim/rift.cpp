// sim/rift.cpp — game/riftarc.js (the Temporal Incursion: the players' story) and riftlore.js (what the villagers make
// of it). Phase 1: he watches (sim/haunt.cpp). Phase 2: a while after the first village is found a rift tears open in
// its square, a window onto his realm. Phase 3: the keepers of the deep places drop Realm Fragments; every one thins
// the boundary, and he grows bolder (doorways, little rifts, things coming through, time standing still). Phase 4:
// four fragments given, the tear steadies into a gateway into his realm (REALM_HIS, gen/realms.cpp RealmGen): hall
// after hall of rift stone, corrupted creatures, and at the end, him. Beaten, the rift is sealed.
// The tears are virtual portals (sim/portals.h setVirtual): true windows, walked through when they are gateways.
// The villagers workstream talks to this through sim/endstory.h (riftAsk ...) and the villageEnter / villageRep events.
#include "sim/endstory_int.h"
#include "sim/portals.h"
#include "sim/commands.h"
#include "gen/dimgens.h"

namespace dq::es {

static constexpr int HALL = RealmGen::HALL, HALLS = RealmGen::HALLS;
static float realmFloor() { return (float)(dimBase(DIM_OVERWORLD) + RealmGen::FLOOR); }
static bool rulesHaunting(Level& lv) { const json& R = lv.info.rules; return !(R.is_object() && R.contains("haunting") && R["haunting"] == false); }

static void riftChanged(Level& lv) { if (lv.events) lv.events->emit("riftChanged", json::array({riftRec(lv)})); }
static void riftSay(Level& lv, const std::string& msg, const std::string& kind = "info", const vec3* near = nullptr) {
  for (Player* l : levelPlayers(lv)) if (!near || (l->realm == REALM_MAIN && std::hypot(l->pos.x - near->x, l->pos.z - near->z) < 64)) say(lv, l->id, msg, kind);
}

// ------------------------------------------------------------------ villages (the structures workstream's plans, or the content hooks)
struct Vil { std::string id, name; vec3 c{0}; json blds; bool ok = false; };
static Vil fromPlan(const Plan& p) {
  Vil v; v.ok = true; v.id = p.data.value("id", p.id); v.name = p.data.value("name", p.name);
  int base = dimBase(DIM_OVERWORLD);
  v.c = vec3(jint(p.data, "x", p.cx), jint(p.data, "y", p.y - base) + base, jint(p.data, "z", p.cz));
  if (p.data.contains("blds")) v.blds = p.data["blds"];
  return v;
}
static Vil villageAtPos(Level& lv, vec3 pos, int pad) {
  Vil best; float bd = 1e9f;
  for (const Plan& p : plansNear(lv, DIM_OVERWORLD, "village", ifloor(pos.x), ifloor(pos.z), pad)) {
    float d = std::hypot(p.cx - pos.x, p.cz - pos.z); if (d < bd) { bd = d; best = fromPlan(p); }
  }
  if (!best.ok && contentHooks().villageNear) if (auto v = contentHooks().villageNear(lv, pos, (float)pad)) { best.ok = true; best.id = v->id; best.name = v->name; best.c = vec3(v->x, v->y, v->z); }
  return best;
}
static Vil villageById(Level& lv, const std::string& id, vec3 near) {
  for (const Plan& p : plansNear(lv, DIM_OVERWORLD, "village", ifloor(near.x), ifloor(near.z), 900)) if (p.data.value("id", p.id) == id) return fromPlan(p);
  if (contentHooks().villageById) if (auto v = contentHooks().villageById(lv, id)) { Vil r; r.ok = true; r.id = v->id; r.name = v->name; r.c = vec3(v->x, v->y, v->z); return r; }
  return Vil();
}

// ------------------------------------------------------------------ the tears (virtual portals) this level shows
static void syncTears(Level& lv, EndStory& S) {
  PortalSystem* ps = portalSystem(lv);
  if (!ps) return;
  json& R = riftRec(lv);
  struct T { std::string id; VirtualPortal v; };
  std::vector<T> want;
  int stage = jint(R, "stage"), frags = std::min(RIFT_NEED, jint(R, "frags"));
  bool sealed = truthy(R, "sealed");
  vec3 exitAt(3.5f, realmFloor() + 1, 0.5f);
  if (stage >= 1 && R.contains("at")) {
    // (0.9.0.24) sealed, the gateway stays: his realm can be visited again, quiet
    VirtualPortal v; v.realm = REALM_MAIN; v.bottom = glm::dvec3(jvec(R["at"])); v.yaw = (float)num(R, "yaw");
    bool gate = stage >= 3;
    v.w = gate ? 2.6f : 1 + frags * 0.22f; v.h = gate ? 4.2f : 2.6f + frags * 0.2f;
    v.destRealm = REALM_HIS; v.destBottom = glm::dvec3(exitAt); v.destYaw = PI / 2; v.walk = gate; v.torn = true;
    want.push_back({"rift_square", v});
    // and its other side, in his realm
    VirtualPortal x; x.realm = REALM_HIS; x.bottom = glm::dvec3(exitAt); x.yaw = PI / 2; x.w = 2.6f; x.h = 4.2f;
    x.destRealm = REALM_MAIN; x.destBottom = v.bottom; x.destYaw = v.yaw; x.walk = gate; x.torn = true;
    want.push_back({"rift_exit", x});
    if (sealed) {
      VirtualPortal y = x; y.bottom = glm::dvec3((HALLS - 1) * HALL + 48.5f, realmFloor() + 2, 0.5f); y.walk = true;
      want.push_back({"rift_home", y});
    }
  }
  std::string sig;
  for (auto& t : want) sig += t.id + ":" + std::to_string(t.v.w) + "," + std::to_string(t.v.walk) + "," + std::to_string(t.v.bottom.x) + ";";
  if (S.tearSig == sig) return;
  S.tearSig = sig;
  for (const char* id : {"rift_square", "rift_exit", "rift_home"}) ps->removeVirtual(id);
  for (auto& t : want) ps->setVirtual(t.id, t.v);
}

// ------------------------------------------------------------------ travel
void travel(Level& lv, Player& p, bool toRealm) {
  json& R = riftRec(lv);
  if (toRealm && jint(R, "stage") < 3) return;
  if (p.riding) if (auto* m = dynamic_cast<Creature*>(lv.entity(p.riding))) dismount(lv, *m, p);
  if (toRealm) {
    lv.world(REALM_HIS);
    p.realm = REALM_HIS; teleport(lv, p, vec3(10.5f, realmFloor() + 1, 0.5f)); p.yaw = -PI / 2;
  } else {
    vec3 at = R.contains("at") ? jvec(R["at"]) : vec3(lv.info.spawn.x, dimBase(DIM_OVERWORLD) + 90, lv.info.spawn.z);
    p.realm = REALM_MAIN; teleport(lv, p, vec3(at.x + 2.5f, at.y + 0.2f, at.z + 0.5f));
  }
}
static void enteredRealm(Level& lv, Player& p) {
  const json& R = riftRec(lv);
  say(lv, p.id, truthy(R, "sealed") ? "His realm, quiet now" : "His realm. Somewhere far down these halls, he waits", truthy(R, "sealed") ? "info" : "warn");
  if (!truthy(p.stats, "realmSeen")) { p.stats["realmSeen"] = 1; grantXP(lv, p, 80, p.pos + vec3(0, 2, 0)); }
}

// ------------------------------------------------------------------ his doings
static void riftMobs(Level& lv, int realm, vec3 at) {
  static const char* T[3] = {"rift_ghoul", "rift_skeleton", "rift_spider"};
  for (int k = 0, n = 2 + (int)(rnd() * 2); k < n; k++) if (Creature* m = spawnMob(lv, realm, T[(int)(rnd() * 3) % 3], at + vec3(rnd() - 0.5f, 0, rnd() - 0.5f))) m->huntT = 30;
}
static void forceSighting(Level& lv, Player& p) { hauntForceSighting(lv, p); }
// in a doorway of the village you're in, or somewhere at the edge of sight
static void riftDoorway(Level& lv, EndStory& S, Player& p) {
  if (inOverworld(p)) {
    Vil v = villageAtPos(lv, p.pos, 16);
    if (v.ok && v.blds.is_array()) {
      int base = dimBase(DIM_OVERWORLD);
      std::vector<vec3> doors;
      for (auto& b : v.blds) if (jint(b, "H") > 3 && b.contains("door")) {
        vec3 d = jvec(b["door"]); d.y += base - 1;   // the door's foot
        float dd = std::hypot(d.x - p.pos.x, d.z - p.pos.z);
        if (dd > 10 && dd < 34 && std::fabs(d.y - p.pos.y) < 8) doors.push_back(d);   // (0.9.0.49) not a doorway far above you
      }
      if (!doors.empty()) {
        vec3 d = doors[(size_t)(rnd() * doors.size()) % doors.size()];
        spawnWatcher(lv, p, vec3(d.x + 0.5f, d.y, d.z + 0.5f), "peek", 0, true);
        // and now and then, a door left open
        if (rnd() < 0.4f) {
          const json& b = v.blds[(size_t)(rnd() * v.blds.size()) % v.blds.size()];
          if (jint(b, "H") > 3 && b.contains("door")) {
            vec3 q = jvec(b["door"]); q.y += base - 1;
            World& w = lv.world(REALM_MAIN);
            BlockId id = w.getBlock((int)q.x, (int)q.y, (int)q.z);
            u8 m = w.getMeta((int)q.x, (int)q.y, (int)q.z);
            if (def(id).has("door") && !(m & 4)) {
              w.setBlock((int)q.x, (int)q.y, (int)q.z, id, m | 4); w.setBlock((int)q.x, (int)q.y + 1, (int)q.z, id, m | 4 | 16);
              lv.fx->sound("door_open", q + vec3(0, 1, 0));
            }
          }
        }
        (void)S;
        return;
      }
    }
  }
  forceSighting(lv, p);
}
// a little rift opens in front of you; he steps out and stares, or they come through
static void riftMicro(Level& lv, EndStory& S, Player& p, bool monsters) {
  World& w = lv.world(p.realm);
  for (int i = 0; i < 28; i++) {
    bool wide = i >= 14; float ang = p.yaw + (rnd() - 0.5f) * (wide ? 2.4f : 0.9f), d = wide ? 5 + rnd() * 6 : 9 + rnd() * 5;
    float x = std::floor(p.pos.x - std::sin(ang) * d) + 0.5f, z = std::floor(p.pos.z - std::cos(ang) * d) + 0.5f;
    if (!w.isLoaded(ifloor(x), ifloor(p.pos.y), ifloor(z))) continue;
    int y = standY(w, x, z, p.pos.y);
    if (y < 0) continue;
    vec3 eye = p.pos + vec3(0, EYE, 0), head(x, y + 1.6f, z);
    if (!lineOfSight(lv, p.realm, eye, head, glm::distance(eye, head))) continue;
    vec3 at(x, y, z);
    if (PortalSystem* ps = portalSystem(lv)) {
      VirtualPortal v; v.realm = p.realm; v.bottom = glm::dvec3(at); v.yaw = headingTo(at, p.pos.x, p.pos.z) + PI / 2;
      v.w = 0.9f; v.h = 2.2f; v.walk = false; v.torn = true; v.destRealm = REALM_HIS; v.destBottom = glm::dvec3(HALL + 48.5, realmFloor() + 1, 0.5); v.destYaw = 0;
      std::string id = "rift_micro" + std::to_string(p.id);
      ps->setVirtual(id, v);
      S.later(6, [id](Level& l) { if (PortalSystem* q = portalSystem(l)) q->removeVirtual(id); });
    }
    lv.fx->sound("rift_open", at + vec3(0, 1, 0), 30);
    u32 pid = p.id; int realm = p.realm;
    S.later(0.9, [pid, realm, at, monsters](Level& l) {
      auto* q = dynamic_cast<Player*>(l.entity(pid));
      if (!q || q->realm != realm) return;   // (0.9.0.54) not into a world left since
      if (monsters) { riftMobs(l, realm, at); riftSay(l, "Something comes through the rift!", "warn"); }
      else {
        Entity* wt = spawnWatcher(l, *q, at, "peek", 2.6f, true);
        q->data["dreadT"] = 2.8; l.fx->soundUi(pid, "rift_whisper");
        u32 wid = wt->id;
        if (EndStory* S2 = endStory(l)) S2->later(2.8, [wid](Level& l2) { if (Entity* e = l2.entity(wid); e && !e->removed) watcherVanish(l2, *e); });
      }
    });
    return;
  }
  forceSighting(lv, p);
}
// a sound behind you, and him standing where it happened
static void riftPhantom(Level& lv, Player& p) {
  float ang = p.yaw + PI + (rnd() - 0.5f), d = 7 + rnd() * 6;
  float x = p.pos.x - std::sin(ang) * d, z = p.pos.z - std::cos(ang) * d;
  static const char* snd[4] = {"chest", "break_stone", "explode", "door_open"};
  lv.fx->sound(snd[(int)(rnd() * 4) & 3], vec3(x, p.pos.y + 1, z));
  int y = standY(lv.world(p.realm), x, z, p.pos.y);
  if (y < 0) { forceSighting(lv, p); return; }   // nowhere to stand there (a wall behind you): some other way
  spawnWatcher(lv, p, vec3(std::floor(x) + 0.5f, y, std::floor(z) + 0.5f), "behind", 0, true);
}
// time stops; he is right in front of you
static void riftFreeze(Level& lv, EndStory& S, Player& p) {
  float d = 3.2f, x = p.pos.x - std::sin(p.yaw) * d, z = p.pos.z - std::cos(p.yaw) * d;
  if (solid(lv.world(p.realm).getBlock(ifloor(x), ifloor(p.pos.y), ifloor(z)))) { riftPhantom(lv, p); return; }
  Entity* wt = spawnWatcher(lv, p, vec3(x, p.pos.y, z), "peek", 2, true);
  S.frozenT = 2; p.data["dreadT"] = 2.4;
  lv.fx->soundUi(p.id, "rift_static");
  u32 wid = wt->id, pid = p.id;
  S.later(2, [wid, pid](Level& l) { if (Entity* e = l.entity(wid); e && !e->removed) watcherVanish(l, *e); l.fx->soundUi(pid, "rift_whisper"); });
}
static void riftEvent(Level& lv, EndStory& S, Player& p, const std::string& ev, bool mon) {
  if (p.realm == REALM_POCKET) { forceSighting(lv, p); return; }   // (0.9.0.50) in a pocket dimension he only ever comes through its portal
  if (ev == "freeze") riftFreeze(lv, S, p);
  else if (ev == "phantom") riftPhantom(lv, p);
  else if (ev == "micro") riftMicro(lv, S, p, mon);
  else riftDoorway(lv, S, p);
}

// ------------------------------------------------------------------ the realm: the corrupted ones, and him at the end
static void realmTick(Level& lv, EndStory& S) {
  json& R = riftRec(lv);
  json& halls = sub(R, "halls");
  auto ps = livePlayers(lv, REALM_HIS);
  bool sealed = truthy(R, "sealed");
  for (Player* p : ps) {
    int hi = (int)std::floor((p->pos.x + 2) / HALL);
    std::string hk = std::to_string(hi);
    // each hall has its waiting crowd, woken when someone walks in
    if (hi >= 1 && hi <= 3 && !truthy(halls, hk.c_str()) && !sealed) {
      halls[hk] = 1;
      static const char* T[3] = {"rift_ghoul", "rift_skeleton", "rift_spider"};
      for (int k = 0; k < 3 + hi * 2; k++) if (Creature* m = spawnMob(lv, REALM_HIS, T[(int)(rnd() * 3) % 3], vec3(hi * HALL + 20 + rnd() * 60, realmFloor() + 1, -30 + rnd() * 60))) m->huntT = 40;
    }
    // the last hall: him
    bool there = false;
    for (auto& u : lv.entities()) if (u->type == "herobrine" && !u->removed && u->realm == REALM_HIS) if (auto* c = dynamic_cast<Creature*>(u.get()); c && c->alive()) there = true;
    if (hi == HALLS - 1 && !sealed && !there && p->pos.x > (HALLS - 1) * HALL + 12) {
      float cx = (HALLS - 1) * HALL + 48.5f;
      Creature* m = spawnMob(lv, REALM_HIS, "herobrine", vec3(cx, realmFloor() + 2, 0.5f));
      if (!m) continue;
      m->huntT = 999; m->kept = true; m->persistent = true; S.mem(*m).calls = 0;
      for (Player* q : ps) lv.fx->soundUi(q->id, "rift_boom");
      // (0.9.0.31) he keeps most of the wounds he took last time
      float hp0 = (float)num(R, "heroHp");
      if (hp0 > 0 && hp0 < m->maxHp) {
        m->hp = std::round(std::min(m->maxHp, hp0 + (m->maxHp - hp0) * 0.25f));
        float f = m->hp / m->maxHp; S.mem(*m).calls = f < 0.33f ? 2 : f < 0.66f ? 1 : 0;
        S.later(2.2, [](Level& l) { riftSay(l, "He still bears the wounds you gave him.", "warn"); });
      }
      riftSay(lv, "Herobrine", "epic");
      S.dreadT = 2;
    }
  }
  if (ps.empty()) R["halls"] = json::object();
}

class RiftBolt : public Entity {
public:
  RiftBolt() { type = "rift_bolt"; w = 0.2f; h = 0.3f; noGravity = true; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    if (age > 3) { removed = true; return; }
    World& wd = lv.world(realm);
    Entity* owner = lv.entity(data.value("owner", 0u));
    for (int s = 0; s < 3; s++) {
      pos += vel * (dt / 3);
      for (Entity* p : pawnsOf(lv, realm)) if (std::fabs(p->pos.x - pos.x) < 0.5f && std::fabs(p->pos.z - pos.z) < 0.5f && pos.y > p->pos.y - 0.2f && pos.y < p->pos.y + 1.9f) {
        hurtPawn(lv, *p, 5, owner, "magic", 2); burst(lv); return;
      }
      if (solid(wd.getBlock(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z)))) { burst(lv); return; }
    }
    sfx::burst(lv, pos, "mana_arcane", 3, 0.3f, json{{"life", 0.4f}, {"size", 0.1f}});
  }
  void burst(Level& lv) { removed = true; sfx::burst(lv, pos, "mana_arcane", 30, 2.4f, json{{"life", 0.5f}, {"size", 0.1f}}); lv.fx->sound("spell_hit", pos); }
};

static OptMove herobrineAI(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  EndStory* S = endStory(lv); if (!S) return std::nullopt;
  Mem& M = S->mem(m);
  const CreatureType& T = *m.T;
  if (!p) return Move{0, m.yaw};
  float frac = m.hp / m.maxHp;
  riftRec(lv)["heroHp"] = m.hp;
  M.hbTp -= dt; M.hbBolt -= dt; M.hbFreeze -= dt;
  if (rnd() < dt * 10) sfx::spark(lv, m.pos + vec3(rnd() - 0.5f, rnd() * 1.8f, rnd() - 0.5f) * vec3(0.5f, 1, 0.5f), "mana_arcane", json{{"vy", 0.5f}, {"life", 0.5f}, {"size", 0.05f}});
  // calls his corrupted at two thirds and one third
  if ((frac < 0.66f && M.calls < 1) || (frac < 0.33f && M.calls < 2)) {
    M.calls++;
    static const char* TT[3] = {"rift_ghoul", "rift_skeleton", "rift_spider"};
    for (int k = 0; k < 3 + M.calls; k++) { float a = rnd() * TAU; if (Creature* c = spawnMob(lv, m.realm, TT[k % 3], m.pos + vec3(std::cos(a) * 4, 0.2f, std::sin(a) * 4))) c->huntT = 60; }
    for (Player* q : livePlayers(lv, m.realm)) lv.fx->soundUi(q->id, "rift_open");
    riftSay(lv, "He calls them through!", "warn");
  }
  World& w = lv.world(m.realm);
  // steps through space to somewhere near you
  if (M.hbTp <= 0) {
    M.hbTp = (frac < 0.33f ? 2.2f : 3.5f) + rnd() * 2;
    float a = rnd() * TAU, d = 4 + rnd() * 4, x = p->pos.x + std::cos(a) * d, z = p->pos.z + std::sin(a) * d;
    if (!solid(w.getBlock(ifloor(x), ifloor(m.pos.y), ifloor(z))) && solid(w.getBlock(ifloor(x), ifloor(m.pos.y) - 1, ifloor(z)))) {
      sfx::burst(lv, m.pos + vec3(0, 1, 0), "mana_arcane", 30, 1.6f, json{{"life", 0.6f}, {"spread", 0.8f}});
      m.pos.x = x; m.pos.z = z; m.vel.x = m.vel.z = 0;
      sfx::burst(lv, m.pos + vec3(0, 1, 0), "mana_arcane", 30, 1.6f, json{{"life", 0.6f}, {"spread", 0.8f}});
      lv.fx->sound("spell_blink", m.pos + vec3(0, 1, 0));
    }
  }
  // bolts of his realm
  if (M.hbBolt <= 0 && dist < 30) {
    M.hbBolt = frac < 0.33f ? 1.1f : 2;
    vec3 s = m.pos + vec3(0, 1.5f, 0), t = p->pos + vec3(0, 1, 0) - s; float l = std::max(1e-3f, glm::length(t));
    auto b = std::make_unique<RiftBolt>(); b->realm = m.realm; b->pos = s; b->vel = t / l * 16.f; b->data = {{"owner", m.id}};
    lv.spawn(std::move(b));
    m.swingT = 0.3f;
  }
  // the stare: time stops for whoever he looks at
  if (frac < 0.5f && M.hbFreeze <= 0 && dist < 20 && p->isPlayer()) { M.hbFreeze = 14; S->frozenT = 1.4; p->data["dreadT"] = 1.8; lv.fx->soundUi(p->id, "rift_static"); }
  else if (M.hbFreeze <= 0) M.hbFreeze = 6;
  if (dist < 1.8f + m.w && std::fabs(dyp) < 2 && m.attackCd <= 0) { m.attackCd = 1; m.swingT = 0.3f; hurtPawn(lv, *p, T.damage, &m, "mob", 3); }
  std::optional<float> nd; if (dist > 2.5f) nd = m.navDir(lv, p->pos, dt);
  return Move{dist < 1.2f ? 0 : T.speed * 0.8f, nd ? *nd : toPlayer};
}

static void riftSeal(Level& lv, Creature& boss) {
  json& R = riftRec(lv);
  if (truthy(R, "sealed")) return;
  R["sealed"] = 1; R["stage"] = 5; riftChanged(lv);
  for (Player* q : levelPlayers(lv)) lv.fx->soundUi(q->id, "rift_boom");
  sfx::burst(lv, boss.pos + vec3(0, 1.5f, 0), "mana_arcane", 80, 6, json{{"life", 1.6f}, {"size", 0.1f}, {"spread", 1.5f}});
  sfx::burst(lv, boss.pos + vec3(0, 1.5f, 0), "mana_light", 80, 6, json{{"life", 1.6f}, {"size", 0.1f}, {"spread", 1.5f}});
  vec3 at = boss.pos + vec3(0, 1, 0); int realm = boss.realm;
  if (EndStory* S = endStory(lv)) S->later(0, [at, realm](Level& l) {
    for (auto [id, n] : std::vector<std::pair<const char*, int>>{{"watcher_blade", 1}, {"rift_staff", 1}, {"heart_crystal", 2}, {"emerald", 32}, {"rift_crystal", 16}})
      if (reg().item(id)) dropItem(l, realm, ItemStack(id, n), at, true);
  });
  riftSay(lv, "Herobrine is defeated. The rift is sealed, and the watching is over.", "epic");
  if (EndStory* S = endStory(lv)) S->later(3.5, [](Level& l) { riftSay(l, "The villages will hear of this. A way home has opened in the arena.", "good"); });
  for (Player* l : levelPlayers(lv)) {
    grantXP(lv, *l, 400, at);
    if (R.contains("village") && lv.events) lv.events->emit("villageRep", json::array({l->name, R["village"], 60}));
  }
}

// ------------------------------------------------------------------ the story moving on (host)
void riftTick(Level& lv, EndStory& S, double dt) {
  json& R = riftRec(lv);
  // the Dread and time standing still
  if (S.dreadT > 0) S.dreadT -= dt;
  if (S.frozenT > 0) S.frozenT -= dt;
  bool frozen = S.frozenT > 0;
  for (Player* p : levelPlayers(lv)) {
    PlayerMem& P = S.pm(*p);
    if (P.riftCd > 0) P.riftCd -= (float)dt;
    double own = num(p->data, "dreadT"); if (own > 0) own = std::max(0.0, own - dt);
    double k = std::max(own, S.dreadT);
    if (k > 0) p->data["dreadT"] = own > 0 ? own : 0; else p->data.erase("dreadT");
    if (S.dreadT > 0) p->data["dreadAll"] = S.dreadT; else p->data.erase("dreadAll");
    if (frozen) { p->data["frozenT"] = S.frozenT; p->vel.x = p->vel.z = 0; } else p->data.erase("frozenT");
  }
  // the freeze: nothing moves
  if (frozen) {
    for (auto& u : lv.entities()) if (auto* c = dynamic_cast<Creature*>(u.get())) if (!c->calm && c->alive()) { c->calm = true; c->vel = vec3(0, std::min(0.f, c->vel.y), 0); S.frozenCalm.push_back(c->id); }
  } else if (!S.frozenCalm.empty()) {
    for (u32 id : S.frozenCalm) if (auto* c = dynamic_cast<Creature*>(lv.entity(id))) c->calm = false;
    S.frozenCalm.clear();
  }
  syncTears(lv, S);
  if ((S.riftT -= (float)dt) > 0) return;
  S.riftT = 1;
  auto all = livePlayers(lv);
  // ---- the catalyst: a while after the first village is found, the rift opens in its square ----
  if (!jint(R, "stage") && playTime(lv) > 900) {
    // (0.9.0.24) the story begins even with the haunting off, with any village found by anyone here; failing that,
    // after a while, the village nearest anyone playing
    std::string vid;
    for (Player* l : all) { const json& vs = l->stats.contains("vseen") ? l->stats["vseen"] : json(); if (vs.is_object() && !vs.empty()) { vid = vs.begin().key(); break; } }
    if (vid.empty()) vid = lv.info.data.value("vfound", std::string());
    Vil v;
    vec3 near = all.empty() ? vec3(lv.info.spawn) : all[0]->pos;
    if (!vid.empty()) v = villageById(lv, vid, near);
    if (!v.ok) for (Player* l : all) if (inOverworld(*l)) { Vil a = villageAtPos(lv, l->pos, 8); if (a.ok) { v = a; lv.info.data["vfound"] = a.id; break; } }
    if (!v.ok && playTime(lv) > 1500) for (Player* l : all) if (inOverworld(*l)) { Vil a = villageAtPos(lv, l->pos, 600); if (a.ok) { v = a; break; } }
    if (v.ok) {
      bool close = false; for (Player* l : all) if (l->realm == REALM_MAIN && std::hypot(l->pos.x - v.c.x, l->pos.z - v.c.z) < 90) close = true;
      riftOpenAt(lv, v.id, v.name, v.c + vec3(0.5f, 1, 5.5f), 0);
      if (close) { lv.fx->sound("rift_open", v.c + vec3(0, 2, 0), 80); riftSay(lv, "Something tears open in the square of " + v.name + "\xE2\x80\xA6", "warn"); }
      else riftSay(lv, "Far away, in " + v.name + ", the air tears with a sound like cloth. The elder will know more.", "warn");
    }
  }
  // his realm
  if (!livePlayers(lv, REALM_HIS).empty() || R.contains("halls")) realmTick(lv, S);
  if (!jint(R, "stage") || truthy(R, "sealed") || !rulesHaunting(lv)) return;
  // ---- the gauntlet: his doings, bolder as the boundary thins (every land but his own) ----
  std::vector<Player*> live;
  for (Player* p : all) if (!p->ghost && hauntDimOk(lv, *p)) live.push_back(p);
  if (live.empty()) return;
  int st = riftStability(lv);
  if (st >= 75) return;
  S.riftEventT -= 1 * (st < 15 ? 2.2 : st < 40 ? 1.6 : 1);
  Entity* act = S.haunt.active ? lv.entity(S.haunt.active) : nullptr;
  if (S.riftEventT > 0 || hauntBossFight(lv) || (act && !act->removed)) return;
  S.riftEventT = 160 + rnd() * 220;
  float r = rnd();
  Player* who = live[(size_t)(rnd() * live.size()) % live.size()];
  std::string ev = st < 15 && r < 0.3f ? "freeze" : st < 15 && r < 0.55f ? "phantom" : st < 40 && r < 0.75f ? "micro" : "door";
  int dim = dimHere(*who);
  bool mon = ev == "micro" && rnd() < (envDay(lv, dim < 0 ? DIM_OVERWORLD : dim) < 0.45f ? 0.45f : 0.2f);
  riftEvent(lv, S, *who, ev, mon);
}

// ------------------------------------------------------------------ fragments: the keepers carry them; the rift takes them
void riftOnDeath(Level& lv, EndStory& S, Creature& c, Entity*) {
  json& R = riftRec(lv);
  const json& dg = c.data.contains("dungeon") ? c.data["dungeon"] : json();
  bool keeper = !dg.is_null() && dg != false && dg != "" && dg != 0;
  vec3 at = c.pos + vec3(0, 1, 0); int realm = c.realm;
  if (keeper && !truthy(R, "sealed") && jint(R, "stage") >= 3) { S.later(0, [at, realm](Level& l) { if (reg().item("rift_crystal")) dropItem(l, realm, ItemStack("rift_crystal", 2), at, true); }); }   // (0.9.0.31) the gate is open
  else if (keeper && !truthy(R, "sealed")) {
    S.later(0, [at, realm](Level& l) { dropItem(l, realm, ItemStack("realm_fragment", 1), at, true); });
    int frags = jint(R, "frags") + 1; R["frags"] = frags; riftChanged(lv);
    int stage = jint(R, "stage");
    if (stage >= 1 && stage < 3) S.later(2.5, [frags](Level& l) {
      riftSay(l, frags >= RIFT_NEED ? "The boundary is paper-thin. Take the fragments to the rift." : "The world shudders. (" + std::to_string(std::min(RIFT_NEED, frags)) + " of " + std::to_string(RIFT_NEED) + " fragments)", "warn"); });
  }
  if (c.type == "herobrine") riftSeal(lv, c);
}

static int removeItems(Player& p, const std::string& id, int n) {
  int left = n;
  for (auto& s : p.inv) if (left > 0 && s.id == id) { int k = std::min(left, s.n); s.n -= k; left -= k; if (s.n <= 0) s = ItemStack(); }
  return n - left;
}
// using the rift with the fragments in hand
bool riftItemUse(Level& lv, EndStory&, Player& p, const std::string&, ivec3, int) {
  json& R = riftRec(lv);
  int stage = jint(R, "stage");
  if (p.realm != REALM_MAIN || stage < 1 || stage >= 3 || !R.contains("at")) return false;
  vec3 at = jvec(R["at"]);
  if (std::hypot(p.pos.x - at.x, p.pos.z - at.z) >= 3.5f || std::fabs(p.pos.y - at.y) >= 3) return false;
  int have = countItem(p, "realm_fragment");
  if (have >= RIFT_NEED) { removeItems(p, "realm_fragment", RIFT_NEED); riftOpenGate(lv); }
  else say(lv, p.id, have ? "The rift pulls at the fragment in your hand, but it wants " + std::to_string(RIFT_NEED) + ". You have " + std::to_string(have) + "." : "The rift hums. Beyond it, something is looking back.");
  p.swingT = 0.01f;
  return true;
}

// ------------------------------------------------------------------ riftlore.js: what the villagers make of it
struct Clue { const char* id; int t, upto; std::vector<std::string> jobs; const char* text; };
static const std::vector<Clue>& LORE() {
  static const std::vector<Clue> L = {
    {"w1", 0, -1, {"farmer", "shepherd", "none"}, "Funny you should ask. There's a man stands at the edge of the north field some evenings. Doesn't move. When I wave, he isn't there."},
    {"w2", 0, -1, {"guard"}, "Third time this month someone's seen a figure up on the ridge. White eyes, they say. I tell them it's the moon in a puddle."},
    {"w3", 0, 5, {"librarian", "elder"}, "There's a story older than the village: a builder who worked alone in the dark, before anyone came. Some say he still looks in on what he made."},
    {"w4", 0, -1, {"any"}, "The dogs won't go past the old oak after dark. Won't even look at it."},
    {"w5", 0, -1, {"cartographer", "fisherman"}, "I mapped a little pillar of sand out on the shore. Next morning there was a torch on top of it. Nobody here makes torches like that."},
    {"r1", 1, 1, {"any"}, "It opened with a sound like someone tearing a sheet, right in the square. Nothing came out. Nothing's come out yet."},
    {"r2", 1, -1, {"farmer"}, "The wheat nearest the square all leans toward it now. Every stalk. Like it's listening."},
    {"r3", 1, -1, {"cleric"}, "I lit candles around it, to bless it. The flames burned purple. And they leaned in."},
    {"r4", 1, -1, {"guard"}, "I stood watch by it for an hour. Through it you see halls, all purple, going on and on. And very far off, something walking."},
    {"r5", 1, -1, {"librarian"}, "I've been through every book we have. One word keeps turning up in the oldest, beside drawings of a door in the air: incursion."},
    {"r6", 1, -1, {"innkeeper", "baker", "merchant"}, "Trade's dried up. Folk from the other villages won't come within sight of it. Can't say I blame them."},
    {"r7", 1, -1, {"elder", "librarian", "cartographer"}, "It opened the very week you first came to {v}. I'm not saying it's your doing. I'm saying it noticed you."},
    {"r8", 1, -1, {"mason", "toolsmith"}, "The paving round it's gone warm. Not hot. Warm, like something's been sitting on it."},
    {"f1", 2, -1, {"any"}, "Someone keeps opening doors at night. Not breaking in. Just opening them, and leaving them open."},
    {"f2", 2, -1, {"innkeeper", "merchant", "none"}, "I saw you up on the hill last night, standing very still. Except you were here, by my fire, the whole night. Weren't you?"},
    {"f3", 2, -1, {"librarian"}, "The keepers in the deep places weren't guarding treasure. The oldest pages say they were put there to hold something shut. And you've been\xE2\x80\xA6 well."},
    {"f4", 2, -1, {"cartographer"}, "Look at this map. That's {v}. And that, just beside it, is {v} again. I didn't draw the second one."},
    {"f5", 2, -1, {"cleric", "elder"}, "He never hurts anyone. Have you noticed? He only watches. That frightens me more than claws would."},
    {"f6", 2, -1, {"guard", "weaponsmith"}, "Put your ear near the rift. Not too near. You'll hear footsteps, a long way off. They stop when you stop."},
    {"f7", 2, -1, {"mason", "toolsmith", "armorer"}, "His side looks like ours. The stone, the trees. But wrong. Like a copy made by someone who'd only ever been told about the world."},
    {"f8", 2, -1, {"shepherd", "fisherman", "fletcher"}, "Every time you bring one of those shards back, the rift goes quieter. And everything else gets louder."},
    {"d1", 3, -1, {"any"}, "I counted the houses this morning. Then again at noon. There was one more at noon."},
    {"d2", 3, -1, {"farmer", "shepherd"}, "The sheep all face the square at night now. Every one. And they don't blink."},
    {"d3", 3, 5, {"librarian", "elder"}, "There's a name worn into the stone under the old bridge, almost smooth. It begins with an H. My grandfather would never say the rest."},
    {"d4", 3, -1, {"cleric", "baker"}, "Time stopped for me yesterday. Just for a breath. A man stood right where you're standing. His eyes had nothing in them. Then the bread fell off the table and he was gone."},
    {"d5", 3, 5, {"jeweller", "librarian"}, "In the old tongue, \"brine\" is the bitter sea. Salt that stings the eyes. I've wondered why anyone would choose that for a name."},
    {"d6", 3, -1, {"guard", "elder", "weaponsmith"}, "He isn't trying to get out. Think about it. Every door he opens, he leaves open. He wants someone to come in."},
    {"d7", 3, -1, {"cartographer", "merchant"}, "My compass points at the square now. Not north. The square."},
    {"g1", 4, -1, {"any"}, "The gate hums like a held breath. The children dream about it. They all dream the same hallway."},
    {"g2", 4, 5, {"librarian", "elder"}, "If the old pages are right, the realm beyond is his own world, the first one he built alone. Grown huge. Gone wrong. Like a garden nobody has tended for a thousand years."},
    {"g3", 4, -1, {"guard", "weaponsmith", "armorer"}, "Take your best steel. Take your friends. And don't look him in the eye any longer than you have to."},
    {"g4", 4, -1, {"cleric", "innkeeper", "baker"}, "Take something warm in with you. Food. A light. They say time runs wrong in there, and the cold gets in."},
    {"s1", 5, -1, {"any"}, "The nights are quiet again. My wife says too quiet. I think she misses being afraid."},
    {"s2", 5, -1, {"librarian", "elder"}, "I keep wondering whether he was ever wicked. Or only alone for far too long, watching other people build."},
    {"s3", 5, -1, {"cartographer"}, "The second {v} is gone from my maps. Mostly. Sometimes, at the very edge of the paper, there's a smudge I didn't make."},
    {"s4", 5, -1, {"farmer", "shepherd", "none", "cleric"}, "I still dream of the purple halls. Do you? I walk them, and at the end there's a chair. Empty. Still warm."},
  };
  return L;
}
static const std::map<int, std::vector<std::string>>& REACT() {
  static const std::map<int, std::vector<std::string>> M = {
    {0, {"Strange? No more than usual. Why \xE2\x80\x94 what have you seen?", "Keep a lamp lit and you'll be fine. That's what my mother said.", "Strange? Only the weather."}},
    {1, {"I don't go near the square any more.", "The elder knows more than I do.", "Stay away from it. That's all I know.", "Don't stare at it. It feels like it stares back."}},
    {2, {"I've told you what I know. I'd rather not think about it.", "Every time you come back from the deep places, the air gets heavier. Have you noticed?", "Please. Just make it stop."}},
    {3, {"Don't ask me again. He might be listening.", "\xE2\x80\xA6", "Did you hear that? No. Never mind.", "Not here. Not out loud."}},
    {4, {"Go, if you're going. Waiting makes it worse.", "We'll keep a light in the window for you.", "Come back. Promise me you'll come back."}},
    {5, {"It's over. Let's leave it over.", "Thank you. Truly. I sleep again.", "Some nights I still check the square. Habit."}}};
  return M;
}
static const std::map<int, std::vector<std::string>>& GREET() {
  static const std::map<int, std::vector<std::string>> M = {
    {1, {"Oh \xE2\x80\x94 it's you. Have you been to the square?", "Stay near the lamps tonight.", "Did you hear it, when it opened?"}},
    {2, {"You look tired. We all are.", "Back from the deep places? Was it worth it?", "Shut the door behind you. Please."}},
    {3, {"Is that really you?", "Quickly, in. Before it gets dark.", "Don't look at the square. Just don't."}},
    {4, {"So it's a door now.", "You're going in, aren't you."}},
    {5, {"There's our hero!", "The square's just a square again. Can you believe it?"}}};
  return M;
}
static const std::map<int, std::vector<std::string>>& NEWS() {
  static const std::map<int, std::vector<std::string>> M = {
    {1, {"The miller's boy swears the rift whispered his name.", "Nobody's drawn water from the square well since it opened.", "The guards drew lots for who watches it at night."}},
    {2, {"Old Hattie left her door open last night. She says she didn't.", "The candles in the chapel keep going out, all at once.", "Someone saw lights in the empty house by the field."}},
    {3, {"Three people saw the same man in three places, at the same moment.", "The bell rang last night. Nobody rang it.", "The cats are gone. All of them."}},
    {4, {"They say the gate hums louder at night.", "Half the village slept in the hall last night, all together."}},
    {5, {"They're talking about a feast. For you.", "The square well is sweet again.", "The children don't dream of the hallway any more. Mostly."}}};
  return M;
}
static const std::map<int, std::vector<std::string>>& CALL() {
  static const std::map<int, std::vector<std::string>> M = {
    {1, {"Thank goodness \xE2\x80\x94 you're back! Have you seen what's in the square?", "You! It opened! Right there, in the square!", "Did you hear it? It sounded like the sky tearing."}},
    {2, {"You're back from the deep places. Something followed you home. I can feel it.", "Don't go to the square tonight. Please."}},
    {3, {"It's worse. Every night it's worse.", "Is it really you? Say something. Say my name."}},
    {4, {"The gate! It's a gate now! You did that, didn't you?"}},
    {5, {"It's gone quiet! The rift \xE2\x80\x94 it's really gone!", "Hooray! Hooray for {p}!"}}};
  return M;
}
static const std::string& pick(const std::vector<std::string>& v) { return v[(size_t)(rnd() * v.size()) % v.size()]; }
static bool clueFits(const Clue& c, int tier) { return c.t <= tier && tier <= (c.upto >= 0 ? c.upto : c.t == 5 ? 5 : 4); }
static std::string fillV(Level& lv, std::string s) {
  std::string v = riftRec(lv).value("vname", std::string("the village"));
  for (size_t i; (i = s.find("{v}")) != std::string::npos;) s.replace(i, 3, v);
  return s;
}
static bool hasJob(const Clue& c, const std::string& j) { return std::find(c.jobs.begin(), c.jobs.end(), j) != c.jobs.end(); }

void riftVillageEnter(Level& lv, Player& p, const std::string& villageId, const std::string& folk) {
  if (!inOverworld(p) || villageId.empty() || folk.empty()) return;
  int tier = riftTier(lv); if (tier < 1) return;
  json& done = sub(p.stats, "riftCalls");
  std::string key = villageId + ":" + std::to_string(tier);
  if (truthy(done, key.c_str())) return;
  done[key] = 1;
  std::string line = pick(CALL().at(tier));
  for (size_t i; (i = line.find("{p}")) != std::string::npos;) line.replace(i, 3, p.name.empty() ? "friend" : p.name);
  say(lv, p.id, folk + ": \xE2\x80\x9C" + line + "\xE2\x80\x9D", tier >= 5 ? "good" : tier >= 3 ? "warn" : "info");
}

static OptMove riftMobAI(Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) {
  if (rnd() < dt * 6) sfx::spark(lv, m.pos + vec3((rnd() - 0.5f) * 0.6f, rnd() * m.h, (rnd() - 0.5f) * 0.6f), "mana_arcane", json{{"vy", 0.4f}, {"life", 0.6f}, {"size", 0.05f}});
  std::string base = m.T->j.value("base", std::string());
  if (base.empty()) return std::nullopt;
  std::string keep = m.type; m.type = base;
  Move r = m.specialAI(lv, p, dist, dyp, toPlayer, dt);
  m.type = keep;
  return r;
}

void registerRift() {
  for (const char* k : {"rift_ghoul", "rift_skeleton", "rift_spider"}) { CreatureHooks h; h.brain = riftMobAI; setCreatureHooks(k, h); }
  CreatureHooks hb; hb.brain = herobrineAI; setCreatureHooks("herobrine", hb);
  registerEntityFactory("rift_bolt", [](const std::string&) { return std::make_unique<RiftBolt>(); });
  registerChatCommand("rift", "Move the rift story along", [](CommandSource& src, const std::vector<std::string>& a) -> std::string {
    if (!src.level) return "";
    Level& lv = *src.level; json& R = riftRec(lv); Player* p = src.player;
    std::string c = a.empty() ? "" : a[0];
    if (c == "open") {
      if (!p) return "No village near enough";
      Vil v = villageAtPos(lv, p->pos, 200);
      if (!v.ok) return "No village near enough";
      riftOpenAt(lv, v.id, v.name, v.c + vec3(0.5f, 1, 5.5f), 0);
      return "The rift opens in " + v.name + " (x " + std::to_string((int)v.c.x) + ", z " + std::to_string((int)v.c.z + 5) + ")";
    }
    if (c == "frag") { if (p) giveOrDrop(lv, *p, ItemStack("realm_fragment", 1)); R["frags"] = jint(R, "frags") + 1; riftChanged(lv); return "Fragments: " + std::to_string(jint(R, "frags")) + " \xC2\xB7 stability " + std::to_string(riftStability(lv)); }
    if (c == "gate") { if (!jint(R, "stage")) return "Open it first"; riftOpenGate(lv); return "The gateway is open"; }
    if (c == "go") { if (p) travel(lv, *p, p->realm != REALM_HIS); if (p && p->realm == REALM_HIS) enteredRealm(lv, *p); return "Through the rift"; }
    if (c == "seal") { R["sealed"] = 1; R["stage"] = 5; riftChanged(lv); return "Sealed"; }
    if (c == "reset") { lv.info.data["rift"] = json{{"stage", 0}, {"frags", 0}}; riftChanged(lv); return "The story starts over"; }
    return "Stage " + std::to_string(jint(R, "stage")) + " \xC2\xB7 fragments " + std::to_string(jint(R, "frags")) + " \xC2\xB7 stability " + std::to_string(riftStability(lv)) + (truthy(R, "sealed") ? " \xC2\xB7 sealed" : "");
  }, true, "<open|frag|gate|go|seal|reset>");
}

} // namespace dq::es

// ------------------------------------------------------------------ the public side (sim/endstory.h)
namespace dq {
using namespace es;

void riftOpenAt(Level& lv, const std::string& villageId, const std::string& name, vec3 at, float yaw) {
  json& R = riftRec(lv);
  R["stage"] = std::max(1, jint(R, "stage")); R["frags"] = jint(R, "frags"); R["village"] = villageId; R["vname"] = name;
  R["at"] = vj(at); R["yaw"] = yaw; R["opened"] = lv.info.day;
  riftChanged(lv);
}
void riftOpenGate(Level& lv) {
  json& R = riftRec(lv); R["stage"] = 3; riftChanged(lv);
  vec3 at = R.contains("at") ? jvec(R["at"]) : vec3(0);
  lv.fx->sound("rift_boom", at + vec3(0, 2, 0), 120);
  sfx::burst(lv, at + vec3(0, 2.5f, 0), "mana_arcane", 120, 3, json{{"life", 1.4f}, {"size", 0.1f}, {"spread", 3}});
  riftSay(lv, "The fragments sink into the rift. It steadies into a gateway. Beyond it: his realm.", "epic");
}
void riftTravel(Level& lv, Player& p, bool toRealm) { travel(lv, p, toRealm); if (toRealm && p.realm == REALM_HIS) enteredRealm(lv, p); }

RiftLine riftAsk(Level& lv, Player& asker, const std::string& job, const std::vector<std::string>& jobsHere) {
  RiftLine out;
  int tier = riftTier(lv);
  if (tier < 0) return out;
  json& heard = sub(asker.stats, "riftClues");
  // something new from this one? the latest part of the story first
  std::vector<const Clue*> mine;
  for (const Clue& c : LORE()) if (clueFits(c, tier) && !heard.contains(c.id) && (hasJob(c, job) || hasJob(c, "any"))) mine.push_back(&c);
  std::stable_sort(mine.begin(), mine.end(), [&](const Clue* a, const Clue* b) { if (a->t != b->t) return a->t > b->t; return hasJob(*a, job) > hasJob(*b, job); });
  const Clue* c = nullptr;
  for (const Clue* x : mine) if (x->t == tier) { c = x; break; }
  if (!c && !mine.empty()) c = mine[0];
  if (c) {
    heard[c->id] = 1; asker.stats["riftCluesN"] = (int)heard.size();
    out.text = fillV(lv, c->text);
    out.toast = tier >= 1 ? "You learn something about the rift\xE2\x80\xA6" : "You hear something strange\xE2\x80\xA6";
    out.sound = tier >= 2 ? "rift_whisper" : "click";
    return out;
  }
  // nothing new: a feeling, and perhaps someone else to ask
  std::vector<std::string> here = jobsHere; here.push_back("guard");
  std::vector<std::pair<const Clue*, std::string>> others;
  for (const Clue& x : LORE()) {
    if (!clueFits(x, tier) || heard.contains(x.id) || hasJob(x, "any")) continue;
    for (const std::string& j : x.jobs) if (j != job && std::find(here.begin(), here.end(), j) != here.end()) { others.push_back({&x, j}); break; }
  }
  auto it = REACT().find(std::max(0, tier));
  std::string line = pick(it != REACT().end() ? it->second : REACT().at(1));
  if (!others.empty() && rnd() < 0.7f) {
    std::string title = others[(size_t)(rnd() * others.size()) % others.size()].second;
    for (auto& ch : title) if (ch == '_') ch = ' ';
    static const std::vector<std::string> A = {"You could ask the", "Talk to the", "The"}, Z = {" \xE2\x80\x94 they've been muttering about it.", ". They saw more than I did.", " won't stop talking about it."};
    line += " " + pick(A) + " " + title + pick(Z);
  }
  out.text = line;
  return out;
}
std::string riftAskLabel(Level& lv) { int t = riftTier(lv); return t < 0 ? "" : t == 0 ? "Seen anything strange lately?" : t >= 5 ? "About what happened\xE2\x80\xA6" : "What do you make of the rift?"; }
std::string riftGreeting(Level& lv) { int t = riftTier(lv); if (t < 1 || rnd() >= 0.45f) return ""; auto it = GREET().find(t); return it == GREET().end() ? "" : pick(it->second); }
std::string riftNews(Level& lv) { int t = riftTier(lv); auto it = NEWS().find(t); if (it == NEWS().end() || rnd() >= 0.55f) return ""; return pick(it->second); }
std::string riftElderLabel(Level& lv) { const json& R = riftRec(lv); if (!jint(R, "stage")) return ""; return truthy(R, "sealed") ? "About the rift\xE2\x80\xA6" : jint(R, "stage") >= 3 ? "The gateway\xE2\x80\xA6" : "The rift in the square\xE2\x80\xA6"; }
std::string riftElderLine(Level& lv) {
  const json& R = riftRec(lv);
  if (!jint(R, "stage")) return "";
  int n = std::min(RIFT_NEED, jint(R, "frags"));
  if (truthy(R, "sealed")) return "You did it. The square is quiet, the nights are only nights again. We will sing of this for a hundred winters.";
  if (jint(R, "stage") >= 3) return "The gateway stands open. He waits beyond it, at the end of his halls. Go together, go ready, and end this.";
  return std::string("It opened when you came. Something old is pressing on our world from the other side. The old tales say the keepers of the deep places hold the keys of time. Find the dungeons, defeat their keepers, bring their Realm Fragments to the rift. Four of them. ") +
         (n ? "You have found " + std::to_string(n) + " so far." : "");
}
std::string riftCorruptSay(Level& lv, const std::string& job) {
  if (riftStability(lv) >= 15 || job == "elder" || rnd() >= 0.18f) return "";
  static const std::vector<std::string> L = {"He sees you through our eyes.", "IT BREAKS. IT BREAKS. IT BREAKS.", "Why did you open it?", "There is no village. There never was."};
  return pick(L);
}

} // namespace dq
