// sim/deepdark.cpp — the living parts of game/deepdark.js and game/deephalls.js (their halls and cities are built by
// the structures workstream): the sculk listening (sensors twitch at footsteps, landings and blocks broken or set
// down near someone; shriekers scream, darken your sight and, the fourth time, call the Warden), the Warden itself
// (blind: it knows where you are only from what it hears and smells, its sonic boom through stone, digging back
// down when left in peace), sculk catalysts spreading the sculk where something dies, and the Deep Halls / Twin
// Vault at work: dart traps, the keys in their gates, the lever puzzle, the Gaoler, the Lich King's vault, the twin
// plates and the Sun and Moon Wardens.
#include "sim/endstory_int.h"
#include "sim/creature_entities.h"

namespace dq::es {

static int deepListen() { return dimBase(DIM_OVERWORLD) + vmap::owUnder(48); }    // DEEP_TOP + 14
static int deepScanTop() { return vmap::owUnder(54); }                           // DEEP_TOP + 20 (band-local)

// ------------------------------------------------------------------ the sculk in a chunk, found once
static const std::vector<ivec4>& sculkCells(EndStory& S, World& w, int cx, int cz) {
  static const std::vector<ivec4> none;
  u64 key = ((u64)(u32)cx << 32) | (u32)cz;
  auto it = S.sculkCells.find(key);
  if (it != S.sculkCells.end()) return it->second;
  Chunk* c = w.chunk(DIM_OVERWORLD, cx, cz);
  if (!c) return none;
  BlockId sen = B("sculk_sensor"), shr = B("sculk_shrieker"), cat = B("sculk_catalyst");
  std::vector<ivec4> out;
  int base = dimBase(DIM_OVERWORLD), top = std::min(DIM_H - 1, deepScanTop());
  for (int y = 1; y <= top; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    BlockId id = w.getBlock(cx * 16 + x, base + y, cz * 16 + z);
    if (id == sen || id == shr || id == cat) out.push_back(ivec4(cx * 16 + x, base + y, cz * 16 + z, id | (w.getMeta(cx * 16 + x, base + y, cz * 16 + z) << 16)));
  }
  return S.sculkCells[key] = std::move(out);
}
static std::vector<ivec4> sculkNear(EndStory& S, World& w, vec3 p, float r) {
  std::vector<ivec4> out;
  for (int cz = ifloor(p.z - r) >> 4; cz <= ifloor(p.z + r) >> 4; cz++) for (int cx = ifloor(p.x - r) >> 4; cx <= ifloor(p.x + r) >> 4; cx++)
    for (const ivec4& c : sculkCells(S, w, cx, cz)) if (glm::distance(vec3(c.x + 0.5f, c.y + 0.5f, c.z + 0.5f), p) <= r) out.push_back(c);
  return out;
}

static bool silentCharm(const Entity* e) {
  if (!e || !e->isPlayer()) return false;
  const json& c = e->data.contains("charm") ? e->data["charm"] : json();
  if (c.is_array()) for (auto& s : c) if (s.is_object() && s.value("id", "") == "echo_charm") return true;
  if (c.is_object() && c.value("id", "") == "echo_charm") return true;
  return false;
}

static bool summonWarden(Level& lv, EndStory& S, int x, int y, int z) {
  World& w = lv.world(REALM_MAIN);
  for (int t = 0; t < 80; t++) {
    // roomy spots first; then anywhere it can stand (it shoulders its way out)
    bool loose = t >= 40; float a = rnd() * TAU, d = 2 + rnd() * 8;
    int wx = ifloor(x + std::cos(a) * d), wz = ifloor(z + std::sin(a) * d);
    for (int yy = y + 3; yy >= y - 4; yy--) {
      if (!solid(w.getBlock(wx, yy - 1, wz))) continue;
      bool room = true; int R = loose ? 0 : 1;
      for (int dy = 0; dy < 3 && room; dy++) for (int dz = -R; dz <= R && room; dz++) for (int dx = -R; dx <= R; dx++) {
        BlockId b = w.getBlock(wx + dx, yy + dy, wz + dz); if (solid(b) || liquid(b)) { room = false; break; }
      }
      if (!room) continue;
      Creature* e = spawnMob(lv, REALM_MAIN, "warden", vec3(wx + 0.5f, yy, wz + 0.5f));
      if (!e) return false;
      e->kept = true; e->persistent = true;
      Mem& M = S.mem(*e); M.emergeT = 4.5f; M.y0 = (float)yy;
      e->yaw = std::atan2(-(float)(x - wx), -(float)(z - wz));
      lv.fx->sound("warden_emerge", vec3(wx + 0.5f, yy, wz + 0.5f), 60);
      return true;
    }
  }
  return false;
}

static void shriek(Level& lv, EndStory& S, int x, int y, int z, bool summoner, u32 src) {
  std::string key = "sh" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
  auto it = S.sculkCd.find(key);
  if (it != S.sculkCd.end() && it->second > S.now - 10) return;
  S.sculkCd[key] = S.now;
  vec3 at(x + 0.5f, y + 1, z + 0.5f);
  lv.fx->sound("sculk_shriek", at, 48);
  for (int k = 0; k < 4; k++) S.later(k * 0.18, [at, k](Level& l) { particles(l, "sculk_soul", at + vec3(0, 0.2f + k * 0.4f, 0), 6, 0.8f, -1.5f); });
  for (Player* l : livePlayers(lv, REALM_MAIN)) if (glm::distance(l->pos, vec3(x, y, z)) < 40) l->data["darkT"] = std::max((float)num(l->data, "darkT"), 12.f);
  json& W = sub(lv.info.data, "deepWarn");
  double t = playTime(lv);
  int lvl = std::max(0, jint(W, "lvl") - (int)std::floor((t - num(W, "t", t)) / 600));
  W["t"] = t;
  lvl++;
  W["lvl"] = lvl;
  auto sayIt = [&](const std::string& m, const std::string& k) { sayNear(lv, REALM_MAIN, vec3(x, y, z), 40, m, k); };
  if (lvl == 1) sayIt("The sculk shrieks. Something far below stirs\xE2\x80\xA6", "warn");
  else if (lvl == 2) sayIt("Another shriek. It is listening\xE2\x80\xA6", "warn");
  else if (lvl == 3) sayIt("It is coming closer\xE2\x80\xA6", "warn");
  else if (summoner) {
    // (next tick: a block change may have brought us here from inside a loop over the entities)
    S.later(0, [x, y, z](Level& l) {
      EndStory* S2 = endStory(l); if (!S2) return;
      for (auto& u : l.entities()) if (u->type == "warden" && !u->removed && std::hypot(u->pos.x - x, u->pos.z - z) < 48) return;
      if (summonWarden(l, *S2, x, y, z)) { sub(l.info.data, "deepWarn")["lvl"] = 3; sayNear(l, REALM_MAIN, vec3(x, y, z), 40, "It is here.", "epic"); }
    });
  }
  (void)src;
}

}  // namespace dq::es

namespace dq {
void vibrate(Level& lv, int realm, vec3 at, u32 src, const std::string& kind) {
  using namespace es;
  EndStory* S = endStory(lv);
  if (!S || lv.isRemote() || realm != REALM_MAIN || dimOfY(ifloor(at.y)) != DIM_OVERWORLD) return;
  if (silentCharm(lv.entity(src))) return;
  S->vibes.push_back({at, src, S->now, kind, realm});
  if (S->vibes.size() > 48) S->vibes.pop_front();
  if (at.y > deepListen()) return;
  World& w = lv.world(REALM_MAIN);
  BlockId sen = B("sculk_sensor"), shr = B("sculk_shrieker");
  for (const ivec4& c : sculkNear(*S, w, at, 8)) {
    BlockId id = (BlockId)(c.w & 0xFFFF); int meta = c.w >> 16;
    if (id == sen) {
      std::string key = std::to_string(c.x) + "," + std::to_string(c.y) + "," + std::to_string(c.z);
      auto it = S->sculkCd.find(key);
      if (it != S->sculkCd.end() && it->second > S->now - 1) continue;
      S->sculkCd[key] = S->now;
      lv.fx->sound("sculk_click", vec3(c.x + 0.5f, c.y + 0.6f, c.z + 0.5f), 16);
      particles(lv, "sculk_glow", vec3(c.x + 0.5f, c.y + 0.8f, c.z + 0.5f), 6, 0.6f, -0.5f);
      // a sensor's twitch carries to the shriekers round it
      if (src) for (const ivec4& h : sculkNear(*S, w, vec3(c.x + 0.5f, c.y + 0.5f, c.z + 0.5f), 8)) if ((BlockId)(h.w & 0xFFFF) == shr) shriek(lv, *S, h.x, h.y, h.z, (h.w >> 16) & 1, src);
    } else if (id == shr && src) shriek(lv, *S, c.x, c.y, c.z, meta & 1, src);
  }
}
}  // namespace dq

namespace dq::es {

// ------------------------------------------------------------------ the Warden
// (0.9.0.46) how much rock lies between two points (solid blocks along the line, every half block)
static int wardenRock(World& w, vec3 a, vec3 b) {
  float L = glm::distance(a, b); int n = std::max(1, (int)std::ceil(L / 0.5f)), c = 0;
  ivec3 last(INT32_MIN);
  for (int i = 1; i < n; i++) {
    vec3 p = a + (b - a) * ((float)i / n); ivec3 q(ifloor(p.x), ifloor(p.y), ifloor(p.z));
    if (q == last) continue;
    last = q;
    BlockId id = w.getBlock(q.x, q.y, q.z);
    if (solid(id) && !def(id).has("door")) c++;
  }
  return c;
}
static bool wardenQuiet(const Entity& q) { auto* P = dynamic_cast<const Player*>(&q); return P && (P->sneaking || P->crawling); }
static bool wardenNoisy(const Entity& q) { auto* P = dynamic_cast<const Player*>(&q); return P && P->onGround && !P->sneaking && !P->crawling && std::hypot(q.vel.x, q.vel.z) > 1.2f; }

static OptMove wardenAI(Level& lv, Creature& m, Entity*, float, float, float, float dt) {
  EndStory* S = endStory(lv); if (!S) return std::nullopt;
  Mem& M = S->mem(m);
  const CreatureType& T = *m.T;
  World& w = lv.world(m.realm);
  m.kept = true;
  // rising out of the ground
  if (M.emergeT > 0) {
    M.emergeT -= dt; m.noGravity = true; m.hasFlyVy = true; m.flyVy = 0; m.vel = vec3(0);
    m.pos.y = M.y0 - 2.9f * std::max(0.f, M.emergeT / 4.5f);
    if (rnd() < dt * 30) particles(lv, "sculk", vec3(m.pos.x + rnd() - 0.5f, M.y0 + 0.1f, m.pos.z + rnd() - 0.5f), 2, 1.5f);
    if (M.emergeT <= 0) { m.noGravity = false; m.pos.y = M.y0; lv.fx->sound("warden_roar", m.pos + vec3(0, 2, 0), 64); M.calmT = 0; }
    return Move{0, m.yaw};
  }
  // and sinking back into it
  if (M.digT > 0) {
    M.digT -= dt; m.noGravity = true; m.hasFlyVy = true; m.flyVy = 0; m.vel = vec3(0); m.pos.y -= dt * 0.75f;
    if (rnd() < dt * 30) particles(lv, "sculk", vec3(m.pos.x + rnd() - 0.5f, m.pos.y + 2.9f, m.pos.z + rnd() - 0.5f), 2, 1.5f);
    if (M.digT <= 0) m.removed = true;
    return Move{0, m.yaw};
  }
  // (0.9.0.46) it's blind: it knows where you are only from what it hears (roughly, the farther and the more rock
  // between, the rougher) or smells (close by, exactly)
  vec3 head = m.pos + vec3(0, 2.2f, 0);
  auto fix = [&](u32 q, vec3 at, bool exact) {
    float d = std::hypot(at.x - head.x, at.z - head.z), err = exact ? 0.3f : std::min(7.f, 0.6f + d * 0.09f + wardenRock(w, head, at + vec3(0, 1, 0)) * 0.9f);
    float a = rnd() * TAU, r = rnd() * err;
    M.known[q] = {vec3(at.x + std::cos(a) * r, at.y, at.z + std::sin(a) * r), S->now};
  };
  // it hears
  for (auto& v : S->vibes) {
    if (v.t <= M.heardT || v.realm != m.realm) continue;
    M.heardT = v.t;
    if (glm::distance(v.p, m.pos) > 18) continue;
    M.target = v.p; M.hasTarget = true; M.calmT = 0;
    if (v.src) { float& a = M.anger[v.src]; a = std::min(150.f, a + (v.kind == "block" ? 45 : v.kind == "land" ? 40 : 30)); fix(v.src, v.p, false); }
    if (rnd() < 0.5f) lv.fx->sound("warden_sniff", m.pos + vec3(0, 2.5f, 0), 24);
  }
  // footsteps of anyone walking or running about, and whoever's close, however quiet
  for (Entity* q : pawnsOf(lv, m.realm)) {
    float dq = glm::distance(q->pos, m.pos), smell = wardenQuiet(*q) ? 4.f : 6.f;
    if (dq < smell) { float& a = M.anger[q->id]; a = std::min(150.f, a + dt * 14); fix(q->id, q->pos, dq < 3); continue; }
    if (dq < 16 && wardenNoisy(*q) && rnd() < dt * 2.5f) { float& a = M.anger[q->id]; a = std::min(150.f, a + 6); fix(q->id, q->pos, false); }
  }
  u32 foe = 0; float fa = 0;
  for (auto it = M.anger.begin(); it != M.anger.end();) {
    auto ki = M.known.find(it->first);
    bool quiet = ki == M.known.end() || S->now - ki->second.t > 6;   // nothing from them for a while: it loses interest faster
    float n = it->second - dt * (quiet ? 4 : 1.2f);
    Entity* k = lv.entity(it->first);
    bool gone = !k || k->removed || (k->isPlayer() && static_cast<Player*>(k)->dead) || (dynamic_cast<Creature*>(k) && !static_cast<Creature*>(k)->alive());
    if (n <= 0 || gone) { M.known.erase(it->first); it = M.anger.erase(it); continue; }
    it->second = n; if (n > fa) { fa = n; foe = it->first; }
    ++it;
  }
  // the heartbeat in its chest quickens as it closes in
  if ((M.beatT -= dt) <= 0) { M.beatT = 1.6f - std::min(1.f, fa / 120) * 1.0f; lv.fx->sound("warden_heart", m.pos + vec3(0, 1.8f, 0), 24); }
  auto ki = foe ? M.known.find(foe) : M.known.end();
  Entity* F = foe ? lv.entity(foe) : nullptr;
  if (F && fa >= 80 && ki != M.known.end()) {
    M.calmT = 0;
    vec3 info = ki->second.p; double ageK = S->now - ki->second.t;
    float d = std::hypot(info.x - m.pos.x, info.z - m.pos.z), tp = headingTo(m.pos, info.x, info.z);
    // the sonic boom: a gathering roar, then a blast of sound at where it thinks you are
    if (M.boomCharge > 0) {
      M.boomCharge -= dt;
      if (M.boomCharge > 0.35f && ageK < 0.6) { M.aim = info; M.hasAim = true; }
      vec3 aim = M.hasAim ? M.aim : info;
      m.yaw = turnTo(m.yaw, headingTo(m.pos, aim.x, aim.z), dt * 6);
      if (rnd() < dt * 30) particles(lv, "sculk_soul", m.pos + vec3(0, 1.8f, 0), 1, 1.2f, 0);
      if (M.boomCharge <= 0) {
        vec3 s0 = m.pos + vec3(0, 1.8f, 0); int walls = wardenRock(w, s0, aim + vec3(0, 1, 0));
        // farther, and through rock, the wider it goes: scattered across the blast (sideways and up or down)
        float hd = std::max(1e-3f, std::hypot(aim.x - s0.x, aim.z - s0.z)), spread = 0.5f + walls * 0.5f + hd * 0.07f, a = rnd() * TAU, r = std::sqrt(rnd()) * spread;
        float px = -(aim.z - s0.z) / hd, pz = (aim.x - s0.x) / hd;
        vec3 e(aim.x + px * std::cos(a) * r, aim.y + 1 + std::sin(a) * r, aim.z + pz * std::cos(a) * r);
        float L0 = std::max(1e-3f, glm::distance(e, s0)), L = std::max(L0, std::min(18.f, L0 + 3));   // the blast carries a little past
        e = s0 + (e - s0) * (L / L0);
        for (float s2 = 0; s2 < L; s2 += 0.8f) particles(lv, "sculk_ring", s0 + (e - s0) * (s2 / L), 1, 0.2f, 0);
        lv.fx->sound("warden_boom", s0, 64);
        // anyone in the blast's path takes it (rock it went through softens it)
        for (Entity* q : pawnsOf(lv, m.realm)) {
          vec3 qc = q->pos + vec3(0, 1, 0);
          float tt = clampf(glm::dot(qc - s0, e - s0) / (L * L), 0, 1);
          float off = glm::distance(s0 + (e - s0) * tt, qc);
          if (off < 1.25f && std::hypot(q->pos.x - s0.x, q->pos.z - s0.z) < 18) hurtPawn(lv, *q, std::max(3.f, std::round(8 * std::max(0.4f, 1 - walls * 0.2f))), &m, "magic", 7);
        }
        M.hasAim = false;
      }
      return Move{0, tp};
    }
    if ((M.boomCd -= dt) <= 0 && ageK < 3 && d > 3.5f && d < 15) { M.boomCd = 7; M.boomCharge = 1.7f; M.aim = info; M.hasAim = true; lv.fx->sound("warden_boom_charge", m.pos + vec3(0, 2, 0), 40); return Move{0, tp}; }
    // a swipe at whoever is really there, within reach
    float dr = std::hypot(F->pos.x - m.pos.x, F->pos.z - m.pos.z);
    if (dr < 1.7f + m.w && std::fabs(F->pos.y - m.pos.y) < 2.5f && m.attackCd <= 0) {
      m.attackCd = 1.8f; m.swingT = 0.4f;
      hurtPawn(lv, *F, T.damage, &m, "mob", 5);
      lv.fx->sound("warden_hit", m.pos + vec3(0, 1.5f, 0), 30);
    }
    // to where it last made them out; there, with nothing new to go on, it stops and sniffs
    if (d < 1.4f && ageK > 1.5) { if (rnd() < dt * 0.8f) lv.fx->sound("warden_sniff", m.pos + vec3(0, 2.5f, 0), 24); return Move{0, m.yaw + std::sin((float)S->now * 1.3f) * 0.02f}; }
    std::optional<float> nd; if (d > 2) nd = m.navDir(lv, info, dt);
    return Move{d < 1.2f ? 0 : T.speed * (ageK < 1.5 ? 1.75f : 1.3f), nd ? *nd : tp};
  }
  // after a sound: over to where it came from, slowly
  if (M.hasTarget) {
    vec3 t = M.target; float d = std::hypot(t.x - m.pos.x, t.z - m.pos.z);
    if (d < 1.5f) M.hasTarget = false;
    else { auto nd = m.navDir(lv, t, dt); return Move{T.speed * 0.7f, nd ? *nd : headingTo(m.pos, t.x, t.z)}; }
  }
  // left in peace a minute, it digs back down
  M.calmT += dt;
  if (M.calmT > 60) { M.digT = 4; lv.fx->sound("warden_dig", m.pos + vec3(0, 1, 0), 40); }
  return Move{0, m.yaw};
}

// ------------------------------------------------------------------ the Deep Halls and the Twin Vault at work
static json& structRec(Level& lv, const std::string& id) { return sub(sub(lv.info.data, "structs"), id.c_str()); }
static vec3 at(const json& j) { vec3 v = jvec(j); v.y += dimBase(DIM_OVERWORLD); return v; }   // plan data: band-local y
static void dhOpen(Level& lv, const json& d) {
  World& w = lv.world(REALM_MAIN);
  int base = dimBase(DIM_OVERWORLD);
  BlockId gate = B("dungeon_gate"), ward = B("ward_stone");
  int x0 = jint(d, "x0"), x1 = jint(d, "x1"), z0 = jint(d, "z0"), z1 = jint(d, "z1"), y0 = jint(d, "y0") + base, y1 = jint(d, "y1") + base;
  for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) for (int y = y0; y <= y1; y++) {
    BlockId id = w.getBlock(x, y, z);
    if (id == gate || id == ward) { w.setBlock(x, y, z, AIR, 0); particles(lv, "smoke", vec3(x + 0.5f, y + 0.5f, z + 0.5f), 4, 0.5f); }
  }
  lv.fx->sound("piston_out", vec3((x0 + x1) * 0.5f, y0 + 1, (z0 + z1) * 0.5f), 40);
}
static bool onPlate(Level& lv, vec3 p) {
  int x = ifloor(p.x), z = ifloor(p.z);
  for (auto& u : lv.entities()) {
    Entity& e = *u;
    if (e.removed || e.realm != REALM_MAIN || ifloor(e.pos.x) != x || ifloor(e.pos.z) != z || std::fabs(e.pos.y - p.y) >= 0.6f) continue;
    if (auto* pl = dynamic_cast<Player*>(&e)) { if (!pl->dead) return true; continue; }
    if (e.type == "item") return true;
    if (auto* c = dynamic_cast<Creature*>(&e)) if (c->alive()) return true;
  }
  return false;
}
static bool inBox(const Entity& q, const json& b, int base, float pad = 1) {
  if (!b.is_array() || b.size() < 6) return false;
  return q.pos.x >= b[0].get<float>() && q.pos.x <= b[3].get<float>() + pad && q.pos.z >= b[2].get<float>() && q.pos.z <= b[5].get<float>() + pad &&
         q.pos.y >= b[1].get<float>() + base - 1 && q.pos.y <= b[4].get<float>() + base;
}

static void hallsTick(Level& lv, EndStory& S) {
  auto ps = livePlayers(lv, REALM_MAIN);
  if (ps.empty()) return;
  World& w = lv.world(REALM_MAIN);
  const int base = dimBase(DIM_OVERWORLD);
  std::set<std::string> seen;
  double now = S.now;
  for (Player* p : ps) {
    if (!inOverworld(*p)) continue;
    for (const char* kind : {"deep_halls", "twin_vault"}) for (const Plan& P : plansNear(lv, DIM_OVERWORLD, kind, ifloor(p->pos.x), ifloor(p->pos.z), 8)) {
      if (!seen.insert(P.id).second) continue;
      json& rec = structRec(lv, P.id);
      const json& D = P.data;
      if (std::string(kind) == "deep_halls") {
        // darts: someone on a plate, and the room's dispensers loose at them
        json& trapT = sub(rec, "trapT");
        const json& traps = D.contains("traps") ? D["traps"] : json::array();
        for (size_t i = 0; i < traps.size(); i++) {
          const json& t = traps[i]; std::string k = std::to_string(i);
          if (num(trapT, k.c_str()) > now) continue;
          Player* who = nullptr;
          for (Player* q : ps) for (auto& pl : t["plates"]) { vec3 a = at(pl); if (ifloor(q->pos.x) == (int)a.x && ifloor(q->pos.z) == (int)a.z && std::fabs(q->pos.y - a.y) < 0.6f) who = q; }
          if (!who) continue;
          trapT[k] = now + 2.2;
          const json& room = t["room"];
          for (auto& sh : t["shooters"]) {
            vec3 s = at(sh);
            if (w.getBlock((int)s.x, (int)s.y, (int)s.z) != B("dispenser")) continue;
            vec3 o(s.x + 0.5f, s.y + 0.5f, s.z + 0.5f + (room.is_array() && room.size() > 2 && (int)s.z == room[2].get<int>() ? 0.7f : -0.7f));
            vec3 d = who->pos + vec3(0, 1.2f, 0) - o; float l = std::max(1e-3f, glm::length(d)), sp = 22;
            spawnArrow(lv, REALM_MAIN, o, d / l * sp + vec3(0, 0.6f, 0), nullptr, 3, false);
          }
          lv.fx->sound("bow", who->pos + vec3(0, 1, 0), 20);
        }
        // the levers
        const json& Pz = D.contains("puzzle") ? D["puzzle"] : json();
        if (Pz.is_object() && Pz.contains("door") && !truthy(rec, "puzzle")) {
          bool ok = true; BlockId lever = B("lever");
          for (size_t k = 0; k < Pz["levers"].size() && ok; k++) {
            vec3 l = at(Pz["levers"][k]);
            if (w.getBlock((int)l.x, (int)l.y, (int)l.z) != lever || (((w.getMeta((int)l.x, (int)l.y, (int)l.z) & 8) != 0) != (Pz["combo"][k].get<int>() != 0))) ok = false;
          }
          if (ok) {
            rec["puzzle"] = 1; dhOpen(lv, Pz["door"]);
            sayNear(lv, REALM_MAIN, vec3(jint(Pz["door"], "x0"), 0, jint(Pz["door"], "z0")), 40, "Somewhere close, stone grinds against stone. A hidden way has opened!", "epic");
          }
        }
        // the Gaoler, with the silver key
        const json& cp = D.contains("captain") ? D["captain"] : json();
        if (cp.is_object() && cp.contains("at") && rec.value("captain", json()) != "dead") {
          bool up = false;
          for (auto& u : lv.entities()) if (!u->removed && u->data.value("dhCaptain", std::string()) == P.id) if (auto* c = dynamic_cast<Creature*>(u.get()); c && c->alive()) up = true;
          bool in = false; for (Player* q : ps) if (inBox(*q, cp["box"], base)) in = true;
          if (!up && in) if (Creature* m = spawnMob(lv, REALM_MAIN, "gaoler", at(cp["at"]) + vec3(0.5f, 0, 0.5f))) {
            m->data["dhCaptain"] = P.id; m->persistent = true; m->kept = true; m->huntT = 60;
            sayAll(lv, "The Gaoler! He has the key to the next floor down", "warn");
          }
        }
        // the Lich King down: his vault opens
        const json& v2 = D.contains("vault2") ? D["vault2"] : json();
        if (truthy(rec, "cleared") && !truthy(rec, "vault") && v2.is_object() && v2.contains("door")) { rec["vault"] = 1; dhOpen(lv, v2["door"]); sayAll(lv, "Behind the Lich King's hall, his vault stands open", "epic"); }
      } else {
        // the Twin Vault: both plates at once
        const json& plates = D.contains("plates") ? D["plates"] : json::array();
        bool all = !plates.empty(), any = false;
        for (auto& pl : plates) { bool on = onPlate(lv, at(pl)); all = all && on; any = any || on; }
        if (!truthy(rec, "door") && all) { rec["door"] = 1; dhOpen(lv, D["door"]); sayAll(lv, "Both plates held: the vault door opens!", "epic"); }
        else if (!truthy(rec, "door") && any && !(num(rec, "hintT") > now)) {
          rec["hintT"] = now + 8;
          sayNear(lv, REALM_MAIN, at(plates[0]), 30, "The plate sinks, but the door waits for its twin to be held too");
        }
        // the Wardens wake when someone comes into the arena
        if (truthy(rec, "door") && !truthy(rec, "cleared")) {
          int live = 0;
          for (auto& u : lv.entities()) if (!u->removed && u->data.value("twinVault", std::string()) == P.id) if (auto* c = dynamic_cast<Creature*>(u.get()); c && c->alive()) live++;
          const json& A = D["arena"]; bool inside = false;
          if (A.is_array() && A.size() >= 4) for (Player* q : ps) if (q->pos.x >= A[0].get<float>() && q->pos.x <= A[2].get<float>() + 1 && q->pos.z >= A[1].get<float>() && q->pos.z <= A[3].get<float>() + 1 && q->pos.y >= P.y && q->pos.y <= P.y + 10) inside = true;
          json& fell = sub(rec, "fell");
          const char* types[2] = {"warden_sun", "warden_moon"};
          if (!live && !truthy(rec, "up") && inside) {
            rec["up"] = 1; fell = json::object();
            for (int i = 0; i < 2; i++) if (Creature* m = spawnMob(lv, REALM_MAIN, types[i], at(D["wardens"][i]) + vec3(0.5f, 0, 0.5f))) { m->data["twinVault"] = P.id; m->persistent = true; m->kept = true; m->huntT = 60; }
            sayAll(lv, "The Sun and Moon Wardens rise! Fell them together", "warn");
          }
          // one down too long alone: it rises again
          if (truthy(rec, "up")) for (int i = 0; i < 2; i++) {
            const char* t = types[i]; const char* other = types[1 - i];
            if (!fell.contains(t) || fell.contains(other)) continue;
            if (playTime(lv) - fell[t].get<double>() > 15) {
              fell.erase(t);
              if (Creature* m = spawnMob(lv, REALM_MAIN, t, at(D["wardens"][i]) + vec3(0.5f, 0, 0.5f))) { m->data["twinVault"] = P.id; m->persistent = true; m->kept = true; m->huntT = 60; m->hp = std::round(m->maxHp / 2); }
              sayAll(lv, capLabel(mobType(t).value("label", std::string(t))) + " rises again!", "warn");
            }
          }
          if (truthy(rec, "up") && !live && !inside && fell.empty()) rec["up"] = 0;   // everyone left: back at their posts
        }
      }
    }
  }
}

void deepTick(Level& lv, EndStory& S, double dt) {
  int listen = deepListen();
  for (Player* p : livePlayers(lv, REALM_MAIN)) {
    PlayerMem& P = S.pm(*p);
    if (!P.init) { P.init = true; P.last = p->pos; P.wasGround = p->onGround; }
    vec3 mv = p->pos - P.last; P.last = p->pos;
    float hs = dt > 0 ? std::hypot(mv.x, mv.z) / (float)dt : 0, vy = dt > 0 ? mv.y / (float)dt : 0;
    if (inOverworld(*p) && p->pos.y < listen) {
      // footsteps (not when sneaking), landings
      if (p->onGround && hs > 1.2f && !p->sneaking && !p->crawling && (P.stepT -= (float)dt) <= 0) { P.stepT = p->sprinting ? 0.3f : 0.5f; vibrate(lv, REALM_MAIN, p->pos, p->id, "step"); }
      if (!P.wasGround && p->onGround && P.lastVy < -6) vibrate(lv, REALM_MAIN, p->pos, p->id, "land");
    }
    P.wasGround = p->onGround; P.lastVy = std::min(vy, p->vel.y);
    // the darkness: near a Warden it never quite lifts
    float dk = (float)num(p->data, "darkT");
    if (dk > 0) dk = std::max(0.f, dk - (float)dt);
    if ((P.wdT -= (float)dt) <= 0) {
      P.wdT = 0.5f;
      for (auto& u : lv.entities()) if (u->type == "warden" && !u->removed && u->realm == p->realm && glm::distance(u->pos, p->pos) < 20) { if (auto* c = dynamic_cast<Creature*>(u.get()); c && c->alive()) dk = std::max(dk, 3.f); }
    }
    if (dk > 0 || p->data.contains("darkT")) { if (dk > 0) p->data["darkT"] = dk; else p->data.erase("darkT"); }
  }
  if ((S.deepT -= (float)dt) > 0) return;
  S.deepT = 0.5f;
  hallsTick(lv, S);
  // old vibrations fade
  while (!S.vibes.empty() && S.vibes.front().t < S.now - 30) S.vibes.pop_front();
}

void deepOnDeath(Level& lv, EndStory& S, Creature& c, Entity*) {
  // the keepers of the halls
  std::string cap = c.data.value("dhCaptain", std::string());
  if (!cap.empty()) {
    structRec(lv, cap)["captain"] = "dead";
    vec3 p = c.pos + vec3(0, 1, 0);
    S.later(0, [p](Level& l) { dropItem(l, REALM_MAIN, ItemStack("key_silver", 1), p, true); });
    sayAll(lv, "The Gaoler falls, and his silver key with him", "epic");
  }
  std::string tv = c.data.value("twinVault", std::string());
  if (!tv.empty()) {
    json& rec = structRec(lv, tv);
    json& fell = sub(rec, "fell"); fell[c.type] = playTime(lv);
    if (fell.contains("warden_sun") && fell.contains("warden_moon")) {
      rec["cleared"] = 1; rec["up"] = 0;
      for (const Plan& P : plansNear(lv, DIM_OVERWORLD, "twin_vault", ifloor(c.pos.x), ifloor(c.pos.z), 30)) if (P.id == tv && P.data.contains("vdoor")) dhOpen(lv, P.data["vdoor"]);
      for (Player* l : levelPlayers(lv)) { say(lv, l->id, "The Twin Wardens are beaten! The vault is yours", "epic"); grantXP(lv, *l, 300, c.pos + vec3(0, 2, 0)); }
    } else sayNear(lv, c.realm, c.pos, 60, "One Warden falls... the other must follow within fifteen seconds!", "warn");
  }
  // the catalyst: where something dies close by, the sculk spreads
  if (c.type == "warden" || c.realm != REALM_MAIN || !inOverworld(c) || c.pos.y > deepListen()) return;
  World& w = lv.world(REALM_MAIN);
  BlockId cat = B("sculk_catalyst");
  ivec4 found(0); bool has = false;
  for (const ivec4& k : sculkNear(S, w, c.pos, 8)) if ((BlockId)(k.w & 0xFFFF) == cat) { found = k; has = true; break; }
  if (!has) return;
  int n = std::min(12, 2 + (c.T->xp ? c.T->xp : 2) / 3);
  vec3 cp(found.x + 0.5f, found.y + 1, found.z + 0.5f), at0 = c.pos;
  lv.fx->sound("sculk_bloom", cp, 24);
  particles(lv, "sculk_soul", cp + vec3(0, 0.1f, 0), 10, 0.5f, -1);
  S.later(0, [at0, n](Level& l) {
    World& W = l.world(REALM_MAIN);
    BlockId sc = B("sculk");
    std::vector<BlockId> ok; for (const char* k : {"stone", "slate", "dirt", "gravel", "cobbled_slate", "tuff", "grass"}) if (BlockId b = B(k)) ok.push_back(b);
    for (int k = 0, done = 0; k < n * 4 && done < n; k++) {
      int bx = ifloor(at0.x + (rnd() - 0.5f) * 6), bz = ifloor(at0.z + (rnd() - 0.5f) * 6);
      for (int by = ifloor(at0.y) + 1; by >= ifloor(at0.y) - 3; by--) {
        BlockId id = W.getBlock(bx, by, bz);
        if (solid(id) && !W.getBlock(bx, by + 1, bz) && std::find(ok.begin(), ok.end(), id) != ok.end()) { W.setBlock(bx, by, bz, sc, 0); done++; break; }
      }
    }
  });
}

// a key in the lock
bool deepItemUse(Level& lv, EndStory&, Player& p, const std::string& item, ivec3 hit, int face) {
  if (face < 0 || p.realm != REALM_MAIN) return false;
  World& w = lv.world(REALM_MAIN);
  if (w.getBlock(hit.x, hit.y, hit.z) != B("dungeon_gate")) return false;
  const int base = dimBase(DIM_OVERWORLD);
  for (const Plan& P : plansNear(lv, DIM_OVERWORLD, "deep_halls", hit.x, hit.z, 2)) {
    if (!P.data.contains("floors")) continue;
    const json* door = nullptr;
    for (auto& F : P.data["floors"]) for (auto& o : F["doors"])
      if (o.value("kind", "") == "gate" && hit.x >= jint(o, "x0") && hit.x <= jint(o, "x1") && hit.z >= jint(o, "z0") && hit.z <= jint(o, "z1") && hit.y >= jint(o, "y0") + base && hit.y <= jint(o, "y1") + base) door = &o;
    if (!door) continue;
    std::string key = door->value("key", "");
    const ItemDef* kd = reg().item(key);
    std::string name = kd ? kd->name : key;
    ItemStack& held = p.held();
    if (held.empty() || held.id != key) { say(lv, p.id, "Locked. It wants a " + name); return true; }
    p.swingT = 0.01f;
    for (auto& F : P.data["floors"]) for (auto& o : F["doors"]) if (o.value("kind", "") == "gate" && o.value("key", "") == key) dhOpen(lv, o);
    if (--held.n <= 0) held = ItemStack();
    say(lv, p.id, "The " + name + " turns, and the gate opens", "good");
    return true;
  }
  return false;
}

void deepBlockChanged(Level& lv, World& w, int x, int y, int z, BlockId o, BlockId n) {
  EndStory* S = endStory(lv); if (!S || w.realm != REALM_MAIN) return;
  static BlockId sen = B("sculk_sensor"), shr = B("sculk_shrieker"), cat = B("sculk_catalyst"), vein = B("sculk_vein");
  if (o == sen || o == shr || o == cat || n == sen || n == shr || n == cat) S->sculkCells.erase(((u64)(u32)(x >> 4) << 32) | (u32)(z >> 4));
  if (dimOfY(y) != DIM_OVERWORLD || y >= deepListen()) return;
  for (Player* p : livePlayers(lv, REALM_MAIN)) if (glm::distance(p->pos, vec3(x + 0.5f, y, z + 0.5f)) < 6) {
    if (!(p->sneaking && n == vein)) vibrate(lv, REALM_MAIN, vec3(x + 0.5f, y + 0.5f, z + 0.5f), p->id, "block");
    break;
  }
}

void registerDeepDark() {
  CreatureHooks wd;
  wd.brain = wardenAI;
  wd.beforeHurt = [](Level& lv, Creature& m, float&, const DamageSrc&) { EndStory* S = endStory(lv); if (!S) return true; Mem& M = S->mem(m); return !(M.emergeT > 0 || M.digT > 0); };
  wd.afterHurt = [](Level& lv, Creature& m, float, const DamageSrc& src) {
    // struck, it knows exactly who did it
    if (!src.attacker || src.attacker == m.id) return;
    if (EndStory* S = endStory(lv)) { Mem& M = S->mem(m); M.anger[src.attacker] = 150; M.calmT = 0; if (Entity* a = lv.entity(src.attacker)) M.known[src.attacker] = {a->pos, S->now}; }
  };
  setCreatureHooks("warden", wd);
  // the keepers: the Gaoler fights as an ogre, the Wardens of the twin vault as the Cinder Golem
  CreatureHooks g; g.brain = [](Level& lv, Creature& m, Entity* p, float dist, float dyp, float toPlayer, float dt) -> OptMove {
    std::string keep = m.type; m.type = "ogre"; Move r = m.specialAI(lv, p, dist, dyp, toPlayer, dt); m.type = keep; return r; };
  setCreatureHooks("gaoler", g);
  CreatureHooks tw; tw.brain = [](Level& lv, Creature& m, Entity* p, float dist, float, float toPlayer, float dt) -> OptMove { return m.bossAI(lv, p, dist, toPlayer, dt); };
  setCreatureHooks("warden_sun", tw); setCreatureHooks("warden_moon", tw);
}

} // namespace dq::es
