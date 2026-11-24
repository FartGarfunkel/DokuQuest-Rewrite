// sim/villager_ai.cpp — the mind of a villager (game/villagers.js npcBrain, npcPath, npcGo, the doors, guards and
// golems, "accidents happen" — the warning and the sword; towns3.js: nobody stays stuck, the watch at the gates;
// hearth.js: the missing villager who waits, waves and follows you home).
#include "sim/villagers_internal.h"
#include "sim/mob_combat.h"
#include "core/registry.h"
#include "sim/physics.h"
#include "world/dims.h"
#include <queue>
#include <unordered_set>

namespace dq {

float turnTo(float a, float b, float k) { return a + wrapAngle(b - a) * std::min(1.f, k); }
static float rnd() { return frand(); }
static float hdg(vec3 from, vec3 to) { return std::atan2(-(to.x - from.x), -(to.z - from.z)); }

// ------------------------------------------------------------------ finding the way, doors and all
static std::vector<u8> g_door, g_danger, g_noStand;
static void doorTables() {
  const Registry& R = reg();
  if (g_door.size() == R.blocks.size()) return;
  g_door.assign(R.blocks.size(), 0); g_danger.assign(R.blocks.size(), 0); g_noStand.assign(R.blocks.size(), 0);
  for (auto& b : R.blocks) {
    const std::string& k = b.key;
    if ((R.tShape[b.id] == ShapeKind::Door && k.find("iron") == std::string::npos) || R.tShape[b.id] == ShapeKind::Gate) g_door[b.id] = 1;
    if (b.has("fire") || k == "lava" || k == "cactus" || k == "magma_block" || k == "sweet_berry_bush" || k == "wither_rose" || k == "campfire" || k == "soul_campfire" || k == "fire") g_danger[b.id] = 1;
    if (R.tShape[b.id] == ShapeKind::Fence || R.tShape[b.id] == ShapeKind::Wall) g_noStand[b.id] = 1;
  }
}
static bool isNpcDoor(BlockId id) { doorTables(); return id < g_door.size() && g_door[id]; }

NpcNavStats& npcNavStats() { static NpcNavStats s; return s; }

std::shared_ptr<const NpcReach> golemGround(Level& lv, const Village& v, const Creature& e) {
  struct Entry { std::shared_ptr<const NpcReach> r; double at = -1e9; };
  static std::unordered_map<std::string, Entry> cache;
  VillagerSystem* S = vsys(lv);
  const double now = S ? S->now : 0;
  Entry& E = cache[v.id + "/" + e.type];
  // (worked out again every few minutes: walls built and stalls taken down change it)
  if (E.r && now - E.at < 180 && now >= E.at) return E.r;
  World& w = lv.world(REALM_MAIN);
  if (!w.isLoaded(v.x, v.y, v.z)) return E.r;
  E.r = npcBroadReach(w, vec3(v.x + 0.5f, v.y + 1, v.z + 0.5f), e.w, e.h, std::max(24, v.r + 16)); E.at = now;
  return E.r;
}

static std::string doorSnd(const BlockDef& b, bool open) {
  const std::string& k = b.key;
  std::string kind = k.rfind("iron", 0) == 0 || k.find("_iron") != std::string::npos || k.rfind("copper", 0) == 0 || k.find("_copper") != std::string::npos ? "door_iron"
                   : reg().tShape[b.id] == ShapeKind::Trapdoor ? "trapdoor" : reg().tShape[b.id] == ShapeKind::Gate ? "gate" : "door";
  return kind + (open ? "_open" : "_close");
}
static void npcOpen(Level& lv, Creature& e, NpcMind& M, int x, int y, int z) {
  World& w = lv.world(e.realm);
  BlockId id = w.getBlock(x, y, z); const BlockDef& b = reg().block(id);
  u8 m = w.getMeta(x, y, z);
  VillagerSystem* S = vsys(lv);
  if (reg().tShape[id] == ShapeKind::Door) {
    int lower = m & 16 ? y - 1 : y; m = w.getMeta(x, lower, z);
    if (m & 4) return;
    w.setBlock(x, lower, z, id, (u8)((m | 4) & ~16)); w.setBlock(x, lower + 1, z, id, (u8)(m | 4 | 16));
    lv.fx->sound(doorSnd(b, true), vec3(x + 0.5f, lower + 1, z + 0.5f));
    M.doors.push_back({ivec3(x, lower, z), S->now});
  } else if (reg().tShape[id] == ShapeKind::Gate && !(m & 4)) {
    w.setBlock(x, y, z, id, (u8)(m | 4)); lv.fx->sound(doorSnd(b, true), vec3(x + 0.5f, y + 0.5f, z + 0.5f));
    M.doors.push_back({ivec3(x, y, z), S->now});
  }
}
void npcShutDoors(Level& lv, Creature& e, NpcMind& M, bool all) {
  if (M.doors.empty()) return;
  World& w = lv.world(e.realm);
  VillagerSystem* S = vsys(lv);
  auto pawns = pawnsOf(lv, e.realm);
  std::vector<NpcMind::Door> keep;
  for (auto& d : M.doors) {
    int x = d.at.x, y = d.at.y, z = d.at.z;
    if (!all && (S->now - d.t < 1.2 || (std::fabs(e.pos.x - x - 0.5f) < 1.3f && std::fabs(e.pos.z - z - 0.5f) < 1.3f && std::fabs(e.pos.y - y) < 2))) { keep.push_back(d); continue; }
    bool someone = false;
    for (Entity* p : pawns) if (std::fabs(p->pos.x - x - 0.5f) < 0.9f && std::fabs(p->pos.z - z - 0.5f) < 0.9f && std::fabs(p->pos.y - y) < 2) someone = true;
    if (someone && !all) { keep.push_back(d); continue; }
    BlockId id = w.getBlock(x, y, z); u8 m = w.getMeta(x, y, z); const BlockDef& b = reg().block(id);
    if (reg().tShape[id] == ShapeKind::Door && (m & 4)) { w.setBlock(x, y, z, id, (u8)(m & ~4)); w.setBlock(x, y + 1, z, id, (u8)((m & ~4) | 16)); lv.fx->sound(doorSnd(b, false), vec3(x + 0.5f, y + 1, z + 0.5f)); }
    else if (reg().tShape[id] == ShapeKind::Gate && (m & 4)) { w.setBlock(x, y, z, id, (u8)(m & ~4)); lv.fx->sound(doorSnd(b, false), vec3(x + 0.5f, y + 0.5f, z + 0.5f)); }
  }
  M.doors = keep;
}

// off a ladder: their weight back on their feet
void npcUnclimb(Creature& e, NpcMind& M) { if (M.climbing) { M.climbing = false; e.noGravity = false; e.climbing = false; } }

// walk to a spot: a path that goes through doors (opening them, and shutting them behind), up and down ladders, onto
// slabs and stairs at a walk. nullopt = there.
OptMove npcGo(Level& lv, Creature& e, NpcMind& M, vec3 t, float dt, float speed) {
  World& w = lv.world(e.realm);
  M.wentLast = true;
  const float d = std::hypot(t.x - e.pos.x, t.z - e.pos.z);
  if (d < 0.45f && std::fabs(t.y - e.pos.y) < 1.5f) { M.hasPath = false; npcUnclimb(e, M); return std::nullopt; }
  M.navT -= dt;
  const bool newGoal = !M.hasNavGoal || std::hypot(t.x - M.navGoal.x, t.z - M.navGoal.z) > 1 || std::fabs(t.y - M.navGoal.y) > 1.5f;
  if (newGoal) { M.partialN = 0; M.lostT = 0; }
  // (off the way they were on — knocked aside, round someone — think again)
  if (M.hasPath && M.pathI < M.path.size() && std::hypot(M.path[M.pathI].c.x - e.pos.x, M.path[M.pathI].c.y - e.pos.z) > 3.f) M.navT = 0;
  if (M.navT <= 0 || newGoal) {
    bool partial = false;
    NpcNavStats& st = npcNavStats();
    // the usual search first; a long one (a city across, round a whole quarter) when that one got nowhere new
    auto endsHere = [&](const std::vector<NpcStep>& p) {
      return p.empty() || (std::hypot(p.back().c.x - e.pos.x, p.back().c.y - e.pos.z) < 1.6f && std::fabs(p.back().floor - e.pos.y) < 1.2f);
    };
    bool snapped = false;
    M.path = npcPath(w, e.pos, t, e.w, e.h, &partial, 2400, &snapped);
    st.plans++;
    if (partial && (endsHere(M.path) || M.partialN >= 2)) { M.path = npcPath(w, e.pos, t, e.w, e.h, &partial, 12000, &snapped); st.plans++; }
    M.goalSnapped = snapped && !partial;
    if (partial) st.partial++;
    M.pathI = 0; M.navGoal = t; M.hasNavGoal = true; M.pathPartial = partial; M.hasPath = !M.path.empty() || !partial;   // (no steps and not partial: on the goal's step already)
    M.partialN = partial ? M.partialN + 1 : 0;
    // a whole way is kept till they stray from it or stick (a door shut, someone in the way); a partial one is looked
    // at again as they go
    M.navT = partial ? 2.5f + rnd() : 8 + rnd() * 2;
    // a partial way that ends about where they are already: there's no way on from here
    if (M.hasPath && partial && endsHere(M.path)) M.hasPath = false;
    if (!M.hasPath) {
      st.failed++;
      M.stuckGo++; M.navT = 1 + rnd() * 0.5f;   // (asked again in a second, not every tick)
      npcUnclimb(e, M);
    } else M.lostT = 0;
  }
  if (!M.hasPath) {
    // no way there, and not getting anywhere either (out on a roof, wedged in a corner, shut in): fifteen seconds of
    // that and they step to a free spot where they were going (it had waited for nobody to be about, so whoever saw one
    // stuck watched it turn round and round for good)
    if (M.lostT <= 0 || glm::distance(e.pos, M.lostFrom) > 3) { M.lostFrom = e.pos; M.lostT = 0; }
    M.lostT += std::max(dt, 0.f) + 1e-4f;
    if (M.lostT > 15) {
      M.lostT = 0;
      const int tx = ifloor(t.x), ty = ifloor(t.y + 0.45f), tz = ifloor(t.z);
      for (int r = 0; r <= 2; r++) for (int dy : {0, 1, -1, -2})
        for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
          if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
          const float f = npcStandAt(w, tx + dx, ty + dy, tz + dz, e.w, e.h);
          if (std::isnan(f)) continue;
          e.pos = vec3(tx + dx + 0.5f, f + 0.01f, tz + dz + 0.5f); e.vel = vec3(0); M.stuckGo = 0; M.navT = 0;
          npcNavStats().rescues++;
          return Move{0, e.yaw};
        }
    }
    // (close by, the last step or two over the threshold; further off they stand and think, they don't set off
    // across country in a straight line)
    return d < 3 ? Move{speed * 0.5f, hdg(e.pos, t)} : Move{0, hdg(e.pos, t)};
  }
  auto& P = M.path;
  auto near = [&](const NpcStep& s, float r) { return std::hypot(s.c.x - e.pos.x, s.c.y - e.pos.z) < r; };
  while (M.pathI < P.size()) {
    const NpcStep& nd = P[M.pathI];
    const float dy = std::fabs(nd.floor - e.pos.y);
    if (near(nd, 0.4f) && dy < (nd.climb || M.climbing ? 0.3f : 1.0f)) { M.pathI++; continue; }
    // (past it already, on the level, the next one nearer: on to that one, not back round)
    if (M.pathI + 1 < P.size() && !nd.climb && !P[M.pathI + 1].climb && !M.climbing && dy < 0.4f && std::fabs(P[M.pathI + 1].floor - nd.floor) < 0.4f
        && std::hypot(P[M.pathI + 1].c.x - e.pos.x, P[M.pathI + 1].c.y - e.pos.z) < std::hypot(nd.c.x - e.pos.x, nd.c.y - e.pos.z)
        && near(nd, 1.2f)) { M.pathI++; continue; }
    break;
  }
  if (M.pathI >= P.size()) {
    npcUnclimb(e, M);
    if (M.pathPartial) { M.navT = std::min(M.navT, 0.3f); return Move{0, hdg(e.pos, t)}; }   // (as far as that way went: think again)
    if (M.goalSnapped) return std::nullopt;   // (as near as anyone can stand to it: there)
    return Move{d < 0.45f ? 0 : speed * 0.5f, hdg(e.pos, t)};
  }
  const NpcStep& nd = P[M.pathI];
  const vec3 nc(nd.c.x, nd.floor, nd.c.y);
  const int cx = ifloor(e.pos.x), cz = ifloor(e.pos.z);
  const bool inCol = nd.at.x == cx && nd.at.z == cz;
  // a ladder (a vine, a rope): up or down it hand over hand, then off at the top onto the floor beside it
  const bool prevClimb = M.pathI > 0 && P[M.pathI - 1].climb && P[M.pathI - 1].at.x == cx && P[M.pathI - 1].at.z == cz;
  if ((nd.climb && inCol) || prevClimb || (M.climbing && std::fabs(nd.floor - e.pos.y) > 0.3f && !inCol && nd.floor > e.pos.y)) {
    if (!M.climbing) npcNavStats().climbs++;
    M.climbing = true; e.noGravity = true; e.climbing = true;
    e.vel.y = std::clamp((nd.floor + 0.03f - e.pos.y) * 5.f, -2.4f, 2.4f);
    // (in the shaft: held in the room the ladder's plate leaves them — a ladder's cell is barely wider than a body, and
    // the plate in the next cell up stops a head that strays over it — while they climb; at the top, off sideways once
    // they're up level)
    if (inCol) {
      float lo[2] = {cx + 0.02f + e.w, cz + 0.02f + e.w}, hi[2] = {cx + 0.98f - e.w, cz + 0.98f - e.w};
      static thread_local std::vector<AABB> bx;
      for (int y = ifloor(e.pos.y) - 1; y <= ifloor(e.pos.y + e.h) + 1; y++) {
        bx.clear(); blockBoxes(w, cx, y, cz, bx);
        for (const AABB& b : bx) {
          if (b.lo.x - cx > 0.5f) hi[0] = std::min(hi[0], b.lo.x - 0.02f - e.w); else if (b.hi.x - cx < 0.5f) lo[0] = std::max(lo[0], b.hi.x + 0.02f + e.w);
          if (b.lo.z - cz > 0.5f) hi[1] = std::min(hi[1], b.lo.z - 0.02f - e.w); else if (b.hi.z - cz < 0.5f) lo[1] = std::max(lo[1], b.hi.z + 0.02f + e.w);
        }
      }
      const float tx = lo[0] <= hi[0] ? (lo[0] + hi[0]) * 0.5f : cx + 0.5f, tz = lo[1] <= hi[1] ? (lo[1] + hi[1]) * 0.5f : cz + 0.5f;
      const float k = std::min(1.f, dt * 10);
      e.pos.x += (tx - e.pos.x) * k; e.pos.z += (tz - e.pos.z) * k; e.vel.x = e.vel.z = 0;
      return Move{0, e.yaw};
    }
    if (nd.floor - e.pos.y > 0.15f) return Move{0, hdg(e.pos, nc)};
    return Move{speed * 0.6f, hdg(e.pos, nc)};
  }
  npcUnclimb(e, M);
  // up: a jump onto anything over a half step (stairs are walked: their low half is the step to the high one)
  if (nd.floor - e.pos.y > 0.6f && e.onGround && near(nd, 1.6f)) {
    const BlockId under = w.getBlock(nd.at.x, nd.at.y - 1, nd.at.z);
    if (reg().tShape[under] != ShapeKind::Stairs || nd.floor - e.pos.y > 1.05f) e.wantJump = true;
  }
  // a closed door on the way: open it
  for (size_t k : {M.pathI, M.pathI + 1}) {
    if (k >= P.size()) continue;
    const ivec3& q = P[k].at;
    for (int y : {q.y, q.y + 1})
      if (isNpcDoor(w.getBlock(q.x, y, q.z)) && std::hypot(q.x + 0.5f - e.pos.x, q.z + 0.5f - e.pos.z) < 2.2f) { npcOpen(lv, e, M, q.x, y, q.z); break; }
  }
  const float prog = M.hasNav ? std::hypot(e.pos.x - M.navXZ.x, e.pos.z - M.navXZ.y) : 0;
  M.navXZ = vec2(e.pos.x, e.pos.z); M.hasNav = true;
  M.stuckT = prog < dt * 0.25f ? M.stuckT + dt : 0;
  if (M.stuckT > 1.5f) { M.stuckT = 0; M.navT = 0; if (e.stepAhead(lv)) e.wantJump = true; else M.sideStepT = 0.6f; M.stuckGo++; }
  // blocked by someone in the way: step round them for a moment instead of jumping
  if (M.sideStepT > 0) { M.sideStepT -= dt; return Move{speed * 0.7f, hdg(e.pos, nc) + (e.id % 2 ? 1.2f : -1.2f)}; }
  // (slower into a turn, so they don't swing wide round a corner or a door post and circle the step)
  const float turn = std::fabs(wrapAngle(hdg(e.pos, nc) - e.yaw));
  return Move{speed * (turn > 1.2f ? 0.35f : turn > 0.6f ? 0.7f : 1.f), hdg(e.pos, nc)};
}

// ------------------------------------------------------------------ guards and golems
static vec3 npcPickSpot(Level& lv, const Village& v, bool plaza) {
  if (plaza || v.roads.empty()) {
    // somewhere in the square to stand: dry ground with room (not in the fountain's basin or the well)
    World& w = lv.world(REALM_MAIN); const Registry& R = reg();
    for (int tries = 0; tries < 12; tries++) {
      float lo = v.tierN() ? (v.fr ? v.fr : 1) + 2.f : 3.f, a = rnd() * TAU;
      float d = v.tierN() ? lo + rnd() * std::max(1.5f, (v.P ? v.P : 7) - 2 - lo) : 3 + rnd() * 3.5f;
      const int x = ifloor(v.x + 0.5f + std::cos(a) * d), z = ifloor(v.z + 0.5f + std::sin(a) * d);
      for (int y = v.y + 4; y >= v.y - 3; y--) {
        const BlockId g = w.getBlock(x, y - 1, z);
        if (!R.tSolid[g] && !R.tLiquid[g]) continue;
        if (R.tLiquid[g] || R.tLiquid[w.getBlock(x, y, z)] || R.tSolid[w.getBlock(x, y, z)] || R.tSolid[w.getBlock(x, y + 1, z)]) break;
        return vec3(x + 0.5f, (float)y, z + 0.5f);
      }
    }
    if (v.roads.empty()) return vec3(v.x + 0.5f, v.y + 1, v.z + 0.5f);
  }
  // somewhere along a road
  const VRoad& road = v.roads[(size_t)std::floor(rnd() * v.roads.size())];
  if (!road.cells.empty()) { const ivec3& c = road.cells[(size_t)std::floor(rnd() * road.cells.size())]; return vec3(c.x + 0.5f, c.y + 1, c.z + 0.5f); }
  int t = 1 + (int)std::floor(rnd() * std::max(1, road.L)), s = (int)std::floor(rnd() * (2 * road.hw + 1)) - road.hw;
  int x = v.x + DXL[road.d] * t + (road.d % 2 == 0 ? s : 0), z = v.z + DZL[road.d] * t + (road.d % 2 ? s : 0);
  int y = findSafeY(lv.world(REALM_MAIN), x, z, DIM_OVERWORLD);
  return vec3(x + 0.5f, y > 0 ? y : v.y + 1, z + 0.5f);
}
static bool isDefender(const Creature& e) { return e.type == "village_guard" || e.T->j.value("golem", false); }

static Entity* npcFoe(Level& lv, Creature& e, NpcMind& M, const Village& v) {
  if (M.foe) {
    Entity* f = lv.entity(M.foe);
    auto* fc = dynamic_cast<Creature*>(f); auto* fp = dynamic_cast<Player*>(f);
    if (!f || f->removed || (fc && !fc->alive()) || (fp && fp->dead) || M.foeT <= 0) M.foe = 0;
  }
  if (M.foe) return lv.entity(M.foe);
  if ((M.scanT -= 0.05f) > 0) return nullptr;
  M.scanT = 0.6f;
  Creature* best = nullptr; float bd = e.T->j.value("golem", false) ? 18.f : 16.f;
  for (auto& up : lv.entities()) {
    auto* m = dynamic_cast<Creature*>(up.get());
    if (!m || m->removed || !m->alive() || !m->T->hostile || m->T->boss || m->realm != e.realm) continue;
    float d = std::hypot(m->pos.x - e.pos.x, m->pos.z - e.pos.z);
    if (d < bd && std::hypot(m->pos.x - v.x, m->pos.z - v.z) < v.r + 20 && std::fabs(m->pos.y - e.pos.y) < 8) { bd = d; best = m; }
  }
  if (best) { M.foe = best->id; M.foeT = 30; }
  return best;
}
static Move npcFight(Level& lv, Creature& e, NpcMind& M, Entity& foe, float dt) {
  bool golem = e.T->j.value("golem", false);
  float dx = foe.pos.x - e.pos.x, dz = foe.pos.z - e.pos.z, d = std::hypot(dx, dz), dy = foe.pos.y - e.pos.y;
  M.foeT -= dt;
  float reach = golem ? 2.2f : 1.6f;
  if (d < reach + foe.w && std::fabs(dy) < 2.2f && e.attackCd <= 0) {
    if (golem) {
      e.attackCd = 1.4f; e.swingT = 0.3f;
      DamageSrc s; s.knock = 2;
      damageEntity(lv, foe, 7 + std::floor(rnd() * 8), &e, "mob", s);
      if (!foe.isPlayer()) foe.vel.y = 7;
      lv.fx->sound("anvil_land", e.pos + vec3(0, 1.5f, 0));
    } else e.meleeAt(lv, foe, d, dy);
  }
  float toFoe = std::atan2(-dx, -dz);
  if (d < reach * 0.8f) return Move{0, toFoe};
  std::optional<float> nd; if (d > 2.5f) nd = e.navDir(lv, foe.pos, dt);
  return Move{e.T->speed * (golem ? 0.8f : 1.f), nd ? *nd : toFoe};
}
// a guard walking over to talk: a move, or nothing when there's nothing to do
static OptMove npcConfront(Level& lv, Creature& e, NpcMind& M, float dt) {
  Player* who = playerById(lv, M.confront);
  if (!who || who->dead || (M.confrontT -= dt) <= 0) { M.confront = 0; return std::nullopt; }
  float d = std::hypot(who->pos.x - e.pos.x, who->pos.z - e.pos.z);
  if (d > 28) { M.confront = 0; return std::nullopt; }
  float face = hdg(e.pos, who->pos);
  bool golem = e.T->j.value("golem", false);
  if (d > 2.6f) { OptMove m = npcGo(lv, e, M, who->pos, dt, e.T->speed * 0.7f); return m ? m : OptMove(Move{e.T->speed * 0.6f, face}); }
  e.yaw = turnTo(e.yaw, face, dt * 6);
  if (!M.confrontSaid) {
    M.confrontSaid = true;
    if (golem) { toast(lv, *who, "The iron golem stares you down. Don't do that again.", "warn"); M.confrontT = 3; }
    else openVillagerScreen(lv, *who, "talk", {{"npc", e.id}, {"confront", true}, {"victim", M.confrontVictim}});
  }
  if (!golem) M.pose |= NP_FOLD;
  return Move{0, e.yaw};
}

// ------------------------------------------------------------------ accidents happen: a warning first, the sword second
void npcOffend(Level& lv, Player& who, const std::string& vid, Creature& victim, bool killed) {
  VillagerSystem* S = vsys(lv); if (!S) return;
  std::string key = who.name + "|" + vid; Offence& o = S->offence[key];
  const double now = S->now;
  bool remembered = now - o.warnT < 150;
  NpcMind& VM = S->mind(victim.id);
  bool onDuty = isDefender(victim) && (VM.confront == who.id || VM.foe == who.id);
  std::vector<Creature*> defenders;
  for (auto& up : lv.entities()) {
    auto* c = dynamic_cast<Creature*>(up.get()); const json* N = c ? npcOf(*c) : nullptr;
    if (!N || c->removed || !c->alive() || !isDefender(*c) || N->value("v", "") != vid) continue;
    if (std::hypot(c->pos.x - victim.pos.x, c->pos.z - victim.pos.z) < 40) defenders.push_back(c);
  }
  if (killed || remembered || onDuty) {   // that's no accident
    npcRep(lv, who, vid, killed ? -30 : -4);
    for (Creature* d : defenders) { NpcMind& DM = S->mind(d->id); DM.foe = who.id; DM.foeT = 30; DM.confront = 0; }
    o.strikes++; o.warnT = now; o.hostile = true;
    openVillagerScreen(lv, who, "close", json::object());
    toast(lv, who, killed ? "The guards are coming for you!" : "That's it! The guards attack!", "warn");
    return;
  }
  // the first time: someone comes to have a word (and saying sorry makes up for it)
  npcRep(lv, who, vid, -1);
  const json* VN = npcOf(victim);
  o.strikes++; o.warnT = now; o.hostile = false; o.victim = VN ? VN->value("n", std::string("the golem")) : "the golem"; o.victimGuard = victim.type == "village_guard";
  Creature* speaker = nullptr; float bd = 1e9f;
  for (Creature* d : defenders) if (d->type == "village_guard") { float dd = std::hypot(d->pos.x - who.pos.x, d->pos.z - who.pos.z); if (dd < bd) { bd = dd; speaker = d; } }
  if (speaker) { NpcMind& SM = S->mind(speaker->id); SM.confront = who.id; SM.confrontT = 30; SM.confrontSaid = false; SM.confrontVictim = o.victim; SM.foe = 0; }
  else for (Creature* d : defenders) if (d->T->j.value("golem", false)) { NpcMind& DM = S->mind(d->id); DM.confront = who.id; DM.confrontT = 8; DM.confrontSaid = false; }   // the golem just comes and looms
  if (VN && !isDefender(victim)) npcSay(lv, victim, "Ow! Hey!");
}
void npcForgive(Level& lv, Player& who, const std::string& vid) {
  VillagerSystem* S = vsys(lv); if (!S) return;
  auto it = S->offence.find(who.name + "|" + vid);
  if (it != S->offence.end()) it->second.forgiven = true;
  for (auto& up : lv.entities()) {
    auto* c = dynamic_cast<Creature*>(up.get()); const json* N = c ? npcOf(*c) : nullptr;
    if (!N || !isDefender(*c) || N->value("v", "") != vid) continue;
    NpcMind& M = S->mind(c->id);
    if (M.confront == who.id) { M.confront = 0; M.hasGoal = false; }
  }
}

// a few words under their breath when you pass (the original's babble, one voice per line here)
void npcSay(Level& lv, Creature& e, const std::string& line, float vol) {
  const json* N = npcOf(e);
  NpcVoice V = npcVoice(N ? N->value("s", 0u) : 0u);
  if (line.empty()) { lv.fx->voice("villager", "say", e.pos + vec3(0, 1.6f, 0), V.pitch / 235.f); return; }
  // villagers.js babble: the line itself, a blip a letter in their own voice (audio/babble.h reads the name:
  // "babble:" + pitch|formant|triangle|seed|rate|max blips|words), placed where they stand
  char head[96];
  std::snprintf(head, sizeof head, "%.2f|%.4f|%d|%d|%.2f|%d|", V.pitch, V.formant, V.triangle ? 1 : 0, V.seed, 26.f, 90);
  lv.fx->sound(std::string("babble:") + head + line, e.pos + vec3(0, 1.6f, 0), 16, vol);
}
void npcMutter(Level& lv, Creature& e, const json& lines) {
  VillagerSystem* S = vsys(lv);
  if (!S || !lines.is_array() || lines.empty()) return;
  NpcMind& M = S->mind(e.id);
  Entity* p = nearestPawn(lv, e.realm, e.pos, 14);
  if (!p || (S->now - M.mutterT < 6)) return;
  M.mutterT = S->now;
  npcSay(lv, e, pickStr(lines, rnd()), 0.35f);
}
static void npcWorkSound(Level& lv, Creature& e, const std::string& j) {
  if (j == "mason") lv.fx->material("stone", "dig", e.pos + vec3(0, 1, 0));   // (the chisel on stone, not an anvil)
  else if (j.find("smith") != std::string::npos || j.find("armorer") != std::string::npos) lv.fx->sound("anvil", e.pos + vec3(0, 1, 0));
  else if (j == "farmer") lv.fx->sound("dig_gravel", e.pos + vec3(0, 0.5f, 0));
}

// a stop on the trade's round (gen/structures_jigsaw_village.cpp tradeRound; data/villagers.json jobs.*.tasks): the tool for it
// in hand and the work done the way it's done there: the smith beats the iron out on the anvil, the armourer dyes and
// quenches at the tub and fits what's made on a stand, the cleric minds the brewing stand...
static void npcTaskWork(Level& lv, Creature& e, NpcMind& M, const json& J, const VTask& t, float dt) {
  const json& TT = J.contains("tasks") && J["tasks"].is_object() ? J["tasks"] : json::object();
  const json T = TT.contains(t.kind) && TT[t.kind].is_object() ? TT[t.kind] : json::object();
  const std::string doing = T.value("do", std::string("work")), held = T.value("held", std::string());
  // the stonecutter fed by hand: the raw block pushed through the blade, then the cut piece taken off ("out"), stroke by stroke
  const std::string out = T.value("out", std::string());
  const std::string inHand = !out.empty() && (M.workN & 1) && reg().item(out) ? out : held;
  if (!inHand.empty() && reg().item(inHand) && e.heldItem != inHand) { e.heldItem = inHand; M.taskHeld = true; }
  // the work in the other hand (the smith's blade or helmet on the anvil, held in the tongs while the hammer comes down)
  { const std::string off = T.value("off", std::string()); if (e.offItem != off && (off.empty() || reg().item(off))) e.offItem = off; }
  const vec3 st = vec3(t.at) + vec3(0.5f + DXL[t.f] * 0.85f, 1.05f, 0.5f + DZL[t.f] * 0.85f);   // the station, in front of them
  M.work = doing; M.workAt = t.at;
  if (doing == "read" || doing == "draw") {
    M.pose |= NP_READ;
    if ((M.workT -= dt) <= 0) { M.workT = 5 + rnd() * 4; lv.fx->sound(doing == "draw" ? "map_paper" : "book_page", st, 8, 0.5f); }
    return;
  }
  struct Act { const char* name; float a, b; const char* sound; const char* fx; int n; };
  static const Act ACTS[] = {
    {"hammer", 0.8f, 1.2f, "anvil", "spark", 4}, {"tend", 2.4f, 3.4f, "fire_crackle", "smoke", 3}, {"dip", 2.2f, 3.0f, "splash_small", "splash", 5},
    {"fit", 1.6f, 2.4f, "equip", nullptr, 0}, {"grind", 0.45f, 0.7f, "grind", "crit", 3}, {"brew", 2.4f, 3.2f, "brew_bubble", "white", 3},
    {"shelve", 2.0f, 3.0f, "book_page", nullptr, 0}, {"sort", 2.0f, 3.0f, "chest", nullptr, 0}, {"chop", 0.9f, 1.3f, "hit", "crumbs", 2},
    {"knead", 1.0f, 1.4f, "craft", nullptr, 0}, {"cut", 1.0f, 1.4f, "saw", "dust", 3}, {"weave", 1.4f, 2.0f, "craft", nullptr, 0},
    {"feed", 1.8f, 2.4f, "saw", "dust", 6}, {"chisel", 0.5f, 0.8f, nullptr, "dust", 2},
    {"soak", 2.6f, 3.4f, "splash_small", "splash", 4}, {"scrape", 0.6f, 0.9f, "shear", nullptr, 0},   // (the tanner: hides into the barrels, scraped on the bench)
    {"work", 1.4f, 2.0f, "craft", nullptr, 0}};
  const Act* A = &ACTS[sizeof ACTS / sizeof ACTS[0] - 1];
  for (const Act& a : ACTS) if (doing == a.name) { A = &a; break; }
  if ((M.workT -= dt) > 0) return;
  M.workT = A->a + rnd() * (A->b - A->a); e.swingT = 0.3f; M.workN++;
  if (doing == "chisel" || doing == "feed") {   // stone worked: its grit off the block, the tap of the mallet (the blade's whine)
    if (A->sound) lv.fx->sound(A->sound, st, 10, 0.6f); else lv.fx->material("stone", "dig", st);
    lv.fx->particles("burst", st, A->n, json{{"item", doing == "feed" && !out.empty() ? out : std::string("stone")}, {"speed", 0.25}});
    return;
  }
  const bool hot = doing == "dip" && (held.find("iron") != std::string::npos || held.find("sword") != std::string::npos || held.find("ingot") != std::string::npos);
  if (hot) {   // the smith's quench: the hot iron hisses and steams in the tub
    lv.fx->sound("fizz", st, 12, 0.7f);
    lv.fx->particles("smoke", st + vec3(0, 0.3f, 0), 8);
    lv.fx->particles("splash", st, 3);
  } else {
    lv.fx->sound(A->sound, st, 10, 0.6f);
    if (A->fx) lv.fx->particles(A->fx, st, A->n);
  }
  if (doing == "tend" && rnd() < 0.5f) lv.fx->particles("flame", st, 2);
}

// towns3.js: a townsperson who ends up inside a wall or under the ground finds their way back to the surface
void npcStuckCheck(Level& lv, Creature& m, NpcMind& M) {
  World& w = lv.world(m.realm);
  const Registry& R = reg();
  int fx = ifloor(m.pos.x), fz = ifloor(m.pos.z), fy = ifloor(m.pos.y + 0.05f), hy = ifloor(m.pos.y + std::min(1.6f, m.h - 0.2f));
  if (!w.isLoaded(fx, fy, fz)) return;
  // (in a wall: the body itself overlaps blocks at the feet and the head, by their real shapes: a ladder's cell, a slab's,
  // a lift platform's aren't walls)
  const float sw = std::max(0.05f, m.w - 0.08f);
  bool inWall = R.tSolid[w.getBlock(fx, hy, fz)] && R.tSolid[w.getBlock(fx, fy, fz)]
                && collides(w, m.pos.x - sw, m.pos.y + 0.15f, m.pos.z - sw, m.pos.x + sw, m.pos.y + 0.6f, m.pos.z + sw)
                && collides(w, m.pos.x - sw, m.pos.y + m.h - 0.6f, m.pos.z - sw, m.pos.x + sw, m.pos.y + m.h - 0.15f, m.pos.z + sw);
  bool voidFall = fy - landBottomY(fy) < 2;
  // in a fountain or a well a few seconds: out onto the nearest dry ground beside it (they climbed in after a spot to
  // stand in the square, and the basin's rim was too high to step back over)
  // (wading a ford on their way, their head dry, isn't it: only when they're out of their depth or not getting out)
  if (R.tLiquid[w.getBlock(fx, hy, fz)] || (R.tLiquid[w.getBlock(fx, fy, fz)] && (!M.hasPath || M.stuckGo > 3))) {
    if (++M.wetN >= 3) {
      M.wetN = 0;
      float bd = 1e9f; ivec3 best(0); bool found = false;
      for (int dz = -6; dz <= 6; dz++) for (int dx = -6; dx <= 6; dx++) for (int dy = -1; dy <= 4; dy++) {
        const int x = fx + dx, y = fy + dy, z = fz + dz;
        const BlockId g = w.getBlock(x, y - 1, z), a0 = w.getBlock(x, y, z), a1 = w.getBlock(x, y + 1, z);
        if (!R.tSolid[g] || R.tLiquid[g] || R.tSolid[a0] || R.tSolid[a1] || R.tLiquid[a0] || R.tLiquid[a1]) continue;
        const float d = (float)(dx * dx + dz * dz) + dy * dy * 0.5f;
        if (d < bd) { bd = d; best = ivec3(x, y, z); found = true; }
      }
      if (found) { m.pos = vec3(best.x + 0.5f, best.y + 0.02f, best.z + 0.5f); m.vel = vec3(0); m.fallDist = 0; M.hasPath = false; }
    }
    return;
  }
  M.wetN = 0;
  if (!inWall && !voidFall) { M.stuckN = 0; return; }
  if (++M.stuckN < 2) return;   // twice in a row (two seconds): a moment's squeeze through a doorway doesn't count
  M.stuckN = 0;
  int base = dimBase(dimOfY(fy));
  for (int y = std::max(base + 1, fy); y < base + DIM_H - 2; y++)
    if (R.tSolid[w.getBlock(fx, y - 1, fz)] && !R.tSolid[w.getBlock(fx, y, fz)] && !R.tSolid[w.getBlock(fx, y + 1, fz)]) {
      m.pos = vec3(fx + 0.5f, y + 0.02f, fz + 0.5f); m.vel = vec3(0); m.fallDist = 0; npcNavStats().rescues++; return;
    }
}

// ------------------------------------------------------------------ hearth.js: the missing
OptMove lostAI(Level& lv, Creature& e, NpcMind& M, float dt) {
  M.pose = 0;
  Player* f = M.follow ? playerById(lv, M.follow) : nullptr;
  if (M.follow && (!f || f->dead)) { M.follow = 0; f = nullptr; }
  const json* N = npcOf(e);
  if (f) {
    float d = std::hypot(f->pos.x - e.pos.x, f->pos.z - e.pos.z);
    if (d > 56) { M.follow = 0; toast(lv, *f, (N ? N->value("n", std::string("They")) : std::string("They")) + " lost sight of you, and waits where they are", "warn"); return Move{0, e.yaw}; }
    if ((d > 18 || std::fabs(f->pos.y - e.pos.y) > 8) && f->onGround && e.teleportNear(lv, *f)) return Move{0, e.yaw};
    if (d > 2.4f) return Move{d > 7 ? e.T->speed * 1.9f : e.T->speed * 1.3f, e.goToward(lv, f->pos, dt)};
    return Move{0, heading(e.pos.x, e.pos.z, f->pos.x, f->pos.z)};
  }
  for (Entity* q : pawnsOf(lv, e.realm)) if (std::hypot(q->pos.x - e.pos.x, q->pos.z - e.pos.z) < 14) { M.pose |= NP_WAVE; e.yaw = turnTo(e.yaw, heading(e.pos.x, e.pos.z, q->pos.x, q->pos.z), dt * 4); break; }
  return Move{0, e.yaw};
}

// ------------------------------------------------------------------ a traveller on the road (villager_life.cpp travellersTick)
// data.road {pts [[x, z]...], k: the point they're making for, d: +1 / -1 along the road}: they walk it point to point,
// stop and face whoever comes up to them (talk to trade), and at the far end go once nobody is about.
OptMove travellerAI(Level& lv, Creature& e, NpcMind& M, Entity* p, float dist, float dt) {
  M.pose = 0;
  json& R = e.data["road"];
  if (!R.contains("pts") || !R["pts"].is_array() || R["pts"].empty()) { e.removed = true; return Move{0, e.yaw}; }
  if (M.holdT > 0) { M.holdT -= dt; if (Entity* who = lv.entity(M.holdBy)) e.yaw = turnTo(e.yaw, hdg(e.pos, who->pos), dt * 8); M.pose |= NP_TALK; return Move{0, e.yaw}; }
  if (e.panicT > 0) { e.panicT -= dt; Entity* f = lv.entity(e.fleeFrom); return Move{e.T->speed * 1.5f, f ? std::atan2(-(e.pos.x - f->pos.x), -(e.pos.z - f->pos.z)) : e.yaw}; }
  if (p && dist < 4.5f) {   // someone come to talk: stand and look at them
    e.yaw = turnTo(e.yaw, hdg(e.pos, p->pos), dt * 5);
    if (M.waveT <= 0 && !M.greeted.count("road")) { M.greeted["road"] = 1; M.waveT = 1.5f; }
    if (M.waveT > 0) { M.waveT -= dt; M.pose |= NP_WAVE; }
    return Move{0, e.yaw};
  }
  const json& pts = R["pts"]; const int n = (int)pts.size();
  int k = R.value("k", 0), d = R.value("d", 1);
  if (k < 0 || k >= n) {   // there: on into the village, and gone when nobody's watching
    if (!p || dist > 40) e.removed = true;
    return Move{0, e.yaw};
  }
  const float tx = pts[k][0].get<int>() + 0.5f, tz = pts[k][1].get<int>() + 0.5f;
  if (std::hypot(tx - e.pos.x, tz - e.pos.z) < 2.2f) { R["k"] = k + d; M.hasGoal = false; return Move{e.T->speed * 0.4f, e.yaw}; }
  if (!M.hasGoal || std::hypot(M.goal.x - tx, M.goal.z - tz) > 0.5f) {
    World& w = lv.world(e.realm);
    const int y = w.isLoaded(ifloor(tx), ifloor(e.pos.y), ifloor(tz)) ? findSafeY(w, ifloor(tx), ifloor(tz), DIM_OVERWORLD) : -1;
    M.goal = vec3(tx, y > 0 ? (float)y : e.pos.y, tz); M.hasGoal = true; M.goalT = 30; M.stuckGo = 0;
  }
  if ((M.goalT -= dt) <= 0 || M.stuckGo > 8) { R["k"] = k + d; M.hasGoal = false; }   // can't get there: on to the next
  if (rnd() < dt * 0.04f && p && dist < 16) npcMutter(lv, e, VD()["chat"]);
  OptMove m = npcGo(lv, e, M, M.goal, dt, e.T->speed * 0.45f);
  return m ? *m : Move{e.T->speed * 0.45f, e.goToward(lv, M.goal, dt)};
}

// ------------------------------------------------------------------ the mind of a villager
// The week's service (brainInner): the pews are the village's seats inside the chapel; the banners are the banner blocks
// in it (found once a minute). One villager a pew seat, the rest stand at the back; from ph 0.40 each bows at a banner.
struct ChapelInfo { std::vector<VSeatSpot> pews; std::vector<ivec3> banners; double at = -1e9; bool rung = false; i64 rungDay = -1; };
static std::unordered_map<std::string, ChapelInfo>& chapels() { static std::unordered_map<std::string, ChapelInfo> m; return m; }
static OptMove npcService(Level& lv, Creature& e, NpcMind& M, const Village& v, const VBld& ch, int idx, double ph, float dt) {
  ChapelInfo& C = chapels()[v.id];
  World& w = lv.world(REALM_MAIN);
  const Registry& R = reg();
  const double now = lv.info.time + (double)lv.info.day * 1e6;
  if (now - C.at > 60) {
    C.at = now; C.pews.clear(); C.banners.clear();
    for (auto& st : v.seats) if (st.at.x >= ch.x0 && st.at.x <= ch.x1 && st.at.z >= ch.z0 && st.at.z <= ch.z1) C.pews.push_back(st);
    for (int x = ch.x0; x <= ch.x1; x++) for (int z = ch.z0; z <= ch.z1; z++) for (int y = ch.y; y <= ch.y + 4; y++) {
      const std::string& k = R.block(w.getBlock(x, y, z)).key;
      if (k.size() > 7 && k.compare(k.size() - 7, 7, "_banner") == 0) C.banners.push_back(ivec3(x, y, z));
    }
  }
  if (C.pews.empty() && C.banners.empty()) return std::nullopt;
  // the bell, once a service
  if (!C.rung || C.rungDay != npcDay(lv)) {
    C.rung = true; C.rungDay = npcDay(lv);
    const vec3 b = ch.hasMid ? vec3(ch.mid) : vec3((ch.x0 + ch.x1) * 0.5f, (float)ch.y + 12, (ch.z0 + ch.z1) * 0.5f);
    for (int k = 0; k < 3; k++) lv.fx->sound("bell", b + vec3(0, 8, 0), 96);
  }
  auto pin = [&]() { e.vel = vec3(0); e.noGravity = true; e.lockMove = true; M.pinned = true; };
  const vec3 mid((ch.x0 + ch.x1 + 1) * 0.5f, (float)ch.y + 1, (ch.z0 + ch.z1 + 1) * 0.5f);
  if (ph >= 0.40 && !C.banners.empty()) {   // the banners: stand before one (on the side toward the nave) and bow
    const ivec3 bn = C.banners[(size_t)idx % C.banners.size()];
    vec3 spot(0); float best = 1e9f;
    for (int f = 0; f < 4; f++) {
      const int x = bn.x + DXL[f], z = bn.z + DZL[f], y = bn.y;
      if (R.tSolid[w.getBlock(x, y, z)] || R.tSolid[w.getBlock(x, y + 1, z)]) continue;
      const float d = std::hypot(x + 0.5f - mid.x, z + 0.5f - mid.z) + (float)(idx % 3) * 0.01f;
      if (d < best) { best = d; spot = vec3(x + 0.5f, (float)y, z + 0.5f); }
    }
    if (best < 1e8f) {
      if (std::hypot(spot.x - e.pos.x, spot.z - e.pos.z) > 0.9f) { if (OptMove m = npcGo(lv, e, M, spot, dt, e.T->speed * 0.4f)) return m; }
      e.yaw = turnTo(e.yaw, hdg(e.pos, vec3(bn) + vec3(0.5f, 0, 0.5f)), dt * 6);
      M.pose |= NP_FOLD | NP_BOW;
      return Move{0, e.yaw};
    }
  }
  if (idx < (int)C.pews.size()) {   // a pew: walk up to it and sit, facing the altar
    const VSeatSpot& sp = C.pews[(size_t)idx];
    const vec3 front = sp.walk ? vec3(sp.ax + 0.5f, (float)sp.at.y, sp.az + 0.5f) : vec3(sp.at.x + DXL[sp.f] + 0.5f, (float)sp.at.y, sp.at.z + DZL[sp.f] + 0.5f);
    const bool byIt = std::hypot(sp.at.x + 0.5f - e.pos.x, sp.at.z + 0.5f - e.pos.z) < 1.3f && std::fabs(e.pos.y - sp.at.y) < 1.2f;
    if (!byIt) { if (OptMove m = npcGo(lv, e, M, front, dt, e.T->speed * 0.42f)) return m; if (M.stuckGo <= 8) return Move{0, e.yaw}; }
    e.pos = vec3(sp.at.x + 0.5f, sp.at.y + 0.25f, sp.at.z + 0.5f);
    e.yaw = std::atan2((float)-DXL[sp.f], (float)-DZL[sp.f]); M.pose |= NP_SIT | NP_FOLD; pin(); M.hasPath = false;
    return Move{0, e.yaw};
  }
  // no pew left: stand at the back of the nave, facing the altar end
  const vec3 back = ch.hasMid ? vec3(ch.door) + (mid - vec3(ch.door)) * 0.35f + vec3((float)(idx % 3 - 1) * 0.8f, 0, 0) : mid;
  if (std::hypot(back.x - e.pos.x, back.z - e.pos.z) > 0.9f) { if (OptMove m = npcGo(lv, e, M, back, dt, e.T->speed * 0.42f)) return m; }
  e.yaw = turnTo(e.yaw, hdg(e.pos, mid + (mid - vec3(ch.door)) * 0.6f), dt * 4);
  M.pose |= NP_FOLD;
  return Move{0, e.yaw};
}

static Move brainInner(Level& lv, Creature& e, NpcMind& M, json& N, const Village& v, Entity* p, float dist, float dt) {
  VillagerSystem* S = vsys(lv);
  const double ph = npcPhase(lv); const bool night = npcNight(lv);
  const int idx = N.value("i", 0);
  const VFolk* folk = idx < 100 && idx < (int)v.folk.size() ? &v.folk[idx] : nullptr;
  const bool guard = e.type == "village_guard", golem = e.T->j.value("golem", false);
  npcShutDoors(lv, e, M);
  if (M.waveT > 0) { M.waveT -= dt; M.pose |= NP_WAVE; }
  if (M.holdT > 0) {   // talking with someone: stand and face them
    M.holdT -= dt;
    if (Entity* who = lv.entity(M.holdBy)) e.yaw = turnTo(e.yaw, hdg(e.pos, who->pos), dt * 8);
    M.pose |= NP_TALK;
    return Move{0, e.yaw};
  }
  // hurt: run away from whoever did it
  if (e.panicT > 0 && !guard && !golem) {
    e.panicT -= dt;
    Entity* f = lv.entity(e.fleeFrom);
    float away = f ? std::atan2(-(e.pos.x - f->pos.x), -(e.pos.z - f->pos.z)) : e.yaw;
    return Move{e.T->speed * 1.5f, away + std::sin(e.age * 2) * 0.3f};
  }
  // guards and golems: monsters (or whoever hurt the villagers) first
  if (guard || golem) {
    if (M.confront && !M.foe) if (OptMove m = npcConfront(lv, e, M, dt)) return *m;
    if (Entity* foe = npcFoe(lv, e, M, v)) return npcFight(lv, e, M, *foe, dt);
    // on watch: walk between the plaza and the ends of the roads; stop and look about now and then
    if (!M.hasGoal || M.goalT <= 0 || std::hypot(M.goal.x - e.pos.x, M.goal.z - e.pos.z) < 0.8f) {
      if (M.hasGoal && M.lookT == 0) M.lookT = 2 + rnd() * 4;
      if (M.lookT > 0) { M.lookT -= dt; if (guard) M.pose |= NP_FOLD; if (M.lookT > 0) return Move{0, e.yaw}; M.lookT = 0; }
      if (golem) {   // (only where a body its width can get to from the square and back: never a garden it can't leave)
        auto G = golemGround(lv, v, e);
        std::vector<i64> pick;
        if (G) for (i64 k : G->cells) { const vec3 at = G->spot(k); if (std::hypot(at.x - v.x, at.z - v.z) < 22) pick.push_back(k); }
        M.goal = !pick.empty() ? G->spot(pick[(size_t)(rnd() * pick.size()) % pick.size()]) : npcPickSpot(lv, v, true);
      } else if (v.roads.empty()) M.goal = npcPickSpot(lv, v, true);
      else {
        const VRoad& rd = v.roads[(size_t)std::floor(rnd() * v.roads.size())];
        int t = std::min(rd.L, 6 + (int)std::floor(rnd() * rd.L));
        if (!rd.cells.empty() && rnd() >= 0.35f) { const ivec3& c = rd.cells[(size_t)std::floor(rnd() * rd.cells.size())]; M.goal = vec3(c.x + 0.5f, c.y + 1, c.z + 0.5f); }   // a jigsaw town's streets
        else M.goal = rnd() < 0.35f ? npcPickSpot(lv, v, true) : vec3(v.x + DXL[rd.d] * t + 0.5f, v.y + 1, v.z + DZL[rd.d] * t + 0.5f);
      }
      M.hasGoal = true; M.goalT = 40; M.goalSerial++;
    }
    M.goalT -= dt;
    OptMove m = npcGo(lv, e, M, M.goal, dt, e.T->speed * (golem ? 0.45f : 0.4f));
    if (M.stuckGo > 6) { M.stuckGo = 0; M.hasGoal = false; }
    return m ? *m : Move{0, e.yaw};
  }
  // ---- the day of a villager ----
  const std::string job = N.value("j", std::string("none"));
  const json& JJ = VD()["jobs"]; const json J = JJ.contains(job) ? JJ[job] : JJ.value("none", json::object());
  const bool shop = hasShop(job);
  enum { NONE, SLEEP, IDLE, WORK, SIT, SOCIAL } act = NONE;
  const VTask* task = nullptr;   // a stop on the trade's round, while working it
  bool hasGoal = false; vec3 goal(0);
  // the village's own seats, then any chair, stool or bench a player put down there (villagers.cpp records them)
  static thread_local std::vector<VSeatSpot> seats; seats.assign(v.seats.begin(), v.seats.end());
  {
    json& rec = villageRec(lv, v.id);
    if (rec.contains("seats") && rec["seats"].is_array()) for (auto& sj : rec["seats"]) if (sj.is_array() && sj.size() >= 4) {
      VSeatSpot o; o.at = ivec3(sj[0].get<int>(), sj[1].get<int>(), sj[2].get<int>()); o.f = sj[3].get<int>() & 3;
      if (reg().block(lv.world(REALM_MAIN).getBlock(o.at.x, o.at.y, o.at.z)).has("seat")) seats.push_back(o);
    }
  }
  const VSeatSpot* seat = seats.empty() ? nullptr : &seats[idx % seats.size()];
  if (seat && seat->at == M.badSeat && S->now < M.badSeatUntil) seat = nullptr;   // couldn't get to it: not now
  // ---- the week's service (every seventh day, late morning): the chapel bell rings, the village sits in the pews
  // facing the altar while the cleric reads at it, and as it ends each goes up to the town's banners either side of
  // the altar and bows before them. Shopkeepers keep their shops.
  if (!night && ph >= 0.3 && ph < 0.42 && npcDay(lv) % 7 == 0 && !(folk && folk->hasWork && shop)) {
    const VBld* ch = nullptr; for (auto& b : v.blds) if (b.type == "chapel") { ch = &b; break; }
    if (ch) if (OptMove m = npcService(lv, e, M, v, *ch, idx, ph, dt)) return *m;
  }
  // the outdoor trades (the builder, lumberjack, miner, fisherman, rancher, shepherd) work on till noon
  static const std::unordered_set<std::string> LONG = {"builder", "lumberjack", "miner", "fisherman", "rancher", "shepherd"};
  const bool longDay = folk && LONG.count(folk->job);
  // home: their own, their partner's (moved in), their parents' (a child) — sim/villager_family.cpp
  ivec3 homeAt(0); int homeF = 0; bool homeShared = false;
  const bool hasHome = npcHome(lv, v, idx, homeAt, homeF, homeShared);
  if (night && folk && folk->noBed && !homeShared) { goal = vec3(folk->home); hasGoal = true; act = IDLE; }   // no bed of their own: the night at the hall
  else if (night && hasHome) { goal = vec3(homeAt); hasGoal = true; act = SLEEP; }
  else if (!night) if (OptMove m = childDay(lv, e, M, v, idx, ph, dt)) return *m;   // (a child: lessons, then play)
  else if (ph < 0.04) act = IDLE;
  else if (folk && folk->hasWork && (ph < 0.3 || (shop && ph < 0.46) || (longDay && ph < 0.42))) {
    if (!lv.isRemote() && (ph < 0.3 || longDay)) if (OptMove m = npcJobWork(lv, e, M, v, *folk, idx, (float)ph, dt)) return *m;   // the trade's work round its workplace (sim/villager_jobs.cpp)
    if (!lv.isRemote() && (ph < 0.3 || longDay)) if (OptMove m = npcChore(lv, e, M, v, *folk, (float)ph, dt)) return *m;   // the day's real work
    if (longDay && M.choreIdle) act = SOCIAL;   // nothing for them just now: about the streets (they stood swinging at the hall door)
    else {
      goal = vec3(folk->work); hasGoal = true; act = WORK;
      if (!folk->tasks.empty() && ph < 0.3) {   // the morning's work goes round the trade's stations (the counter is kept after)
        const int n = (int)folk->tasks.size();
        if (M.taskI < 0 || M.taskI >= n) { M.taskI = (int)(rnd() * n) % n; M.taskT = 10 + rnd() * 10; }
        if ((M.taskT -= dt) <= 0) {
          M.taskI = (M.taskI + 1) % n; M.taskT = 16 + rnd() * 14; M.workT = 0.6f;
          // the trade's real work takes the longest: the smith at the anvil, the tanner's hides in the barrels
          const std::string& k = folk->tasks[M.taskI].kind;
          if (k == "anvil" || (job == "leatherworker" && (k == "stores" || k == "table"))) M.taskT *= 2.2f;
        }
        task = &folk->tasks[M.taskI]; goal = vec3(task->at);
      }
    }
  }
  else if (ph >= 0.43 && seat) {
    goal = seat->walk ? vec3(seat->ax, seat->at.y, seat->az) : vec3(seat->at.x + DXL[seat->f], seat->at.y, seat->at.z + DZL[seat->f]); hasGoal = true; act = SIT;   // walk up to the bench, then sit
    if (!seat->walk) {   // its front blocked (a bench against a wall, one set down the wrong way round): come up from a side that's open
      World& w = lv.world(e.realm); const Registry& R = reg();
      auto open = [&](int x, int y, int z) { return !R.tSolid[w.getBlock(x, y, z)] && !R.tSolid[w.getBlock(x, y + 1, z)]; };
      if (!open((int)goal.x, (int)goal.y, (int)goal.z))
        for (int k = 1; k < 4; k++) { const int f = (seat->f + k) & 3; if (open(seat->at.x + DXL[f], seat->at.y, seat->at.z + DZL[f])) { goal = vec3(seat->at.x + DXL[f], seat->at.y, seat->at.z + DZL[f]); break; } }
    }
    if (idx >= (int)seats.size()) { act = SOCIAL; hasGoal = false; }
  } else act = SOCIAL;
  if (M.taskHeld && !task) {   // (the round's tool put down: their own back in hand, or what they're carrying)
    e.heldItem = !M.carry.empty() && M.carryN > 0 ? M.carry : J.contains("held") && J["held"].is_string() ? J["held"].get<std::string>() : std::string();
    if (!e.heldItem.empty() && !reg().item(e.heldItem)) e.heldItem.clear();
    M.taskHeld = false;
    e.offItem.clear();
  }
  if (act == SOCIAL) M.aboutAt = S->now;
  if (act == SOCIAL) if (OptMove m = npcSocial(lv, e, M, v, idx, dt)) return *m;   // free time: a talk with whoever's about (sim/villager_social.cpp)
  if (act == SOCIAL) {
    if (!M.hasGoal || M.goalT <= 0) { M.goal = npcPickSpot(lv, v, rnd() < 0.55f); M.hasGoal = true; M.goalT = 25 + rnd() * 25; M.goalSerial++; }
    M.goalT -= dt; goal = M.goal; hasGoal = true;
  }
  // someone you know walking by: look at them, and wave the first time
  if (p && dist < 6 && act != SLEEP && isPawn(p)) {
    std::string key = p->isPlayer() ? static_cast<Player*>(p)->name : "p";
    auto it = M.greeted.find(key);
    if (it == M.greeted.end() || S->now - it->second > 240) { M.greeted[key] = S->now; M.waveT = 1.4f; if (dist < 5) npcMutter(lv, e, VD()["hello"]); }
  }
  if (hasGoal) {
    const bool soc = act == SOCIAL;
    float gx = soc ? goal.x : goal.x + 0.5f, gz = soc ? goal.z : goal.z + 0.5f;
    float d = std::hypot(gx - e.pos.x, gz - e.pos.z);
    bool seated = act == SIT && std::hypot(seat->at.x + 0.5f - e.pos.x, seat->at.z + 0.5f - e.pos.z) < 0.35f && std::fabs(e.pos.y - seat->at.y - 0.25f) < 0.3f;
    // close enough to the seat itself (from any side) is close enough to sit down: no more circling to the one spot in front
    const bool byIt = act == SIT && std::hypot(seat->at.x + 0.5f - e.pos.x, seat->at.z + 0.5f - e.pos.z) < 1.3f && std::fabs(e.pos.y - seat->at.y) < 1.2f;
    if (act == SIT && !seated && !byIt && (npcInTalk(e.id) || M.stuckGo > 3)) if (OptMove m = npcSocial(lv, e, M, v, idx, dt)) return *m;   // (held up on the way: a word with who's there)
    if (act == SIT && !seated && !byIt && M.stuckGo > 6) {   // it can't get there: leave that seat be for a couple of minutes
      M.badSeat = seat->at; M.badSeatUntil = S->now + 120; M.stuckGo = 0; M.hasPath = false; return Move{0, e.yaw};
    }
    if (d > 0.6f && !seated && !byIt) {
      // stuck for a long time and nobody about to see it: step there (villagers know their way home)
      if (M.stuckGo > 8 && !nearestPawn(lv, e.realm, e.pos, 24)) { npcNavStats().rescues++; npcUnclimb(e, M); e.pos = vec3(gx, goal.y + 0.01f, gz); M.stuckGo = 0; M.hasPath = false; return Move{0, e.yaw}; }
      if (M.stuckGo > 10 && soc) { M.hasGoal = false; M.stuckGo = 0; }
      if (OptMove m = npcGo(lv, e, M, vec3(gx, goal.y, gz), dt, e.T->speed * (act == SLEEP ? 0.55f : 0.42f))) return *m;
    }
    M.stuckGo = 0;
    auto pin = [&]() { e.vel = vec3(0); e.noGravity = true; e.lockMove = true; M.pinned = true; };
    if (act == SLEEP) {
      int f = homeF & 3;
      e.pos = vec3(goal.x + 0.5f + DXL[f] * 0.5f, goal.y + 0.5625f, goal.z + 0.5f + DZL[f] * 0.5f);
      if (homeShared) {   // (sharing the bed: beside them, or a child at its foot)
        const bool child = npcIsChild(lv, v.id, idx);
        e.pos += child ? vec3(DXL[f], 0, DZL[f]) * -0.15f + vec3(DZL[f], 0, -DXL[f]) * 0.25f : vec3(DZL[f], 0, -DXL[f]) * 0.42f;
      }
      e.yaw = std::atan2((float)-DXL[f], (float)-DZL[f]); M.pose |= NP_SLEEP; pin();
      return Move{0, e.yaw};
    }
    if (act == SIT) {   // (0.9.0.11) on the seat the way you sit on one: half a block up, held still
      M.sitHeld = seat->held; e.pos = vec3(seat->at.x + 0.5f, seat->at.y + 0.25f, seat->at.z + 0.5f);
      e.yaw = std::atan2((float)-DXL[seat->f], (float)-DZL[seat->f]); M.pose |= NP_SIT; pin(); M.hasPath = false;
      N["p"] = (int)(N.value("p", 0) | NP_SIT);   // (seen as sat by the others' talk at once)
      npcSocial(lv, e, M, v, idx, dt, true);   // a word with whoever's sat beside them (sim/villager_social.cpp)
      return Move{0, e.yaw};
    }
    if (act == WORK) {
      int f = (task ? task->f : folk->workF) & 3; e.yaw = turnTo(e.yaw, std::atan2((float)-DXL[f], (float)-DZL[f]), dt * 6);
      if (task) { npcTaskWork(lv, e, M, J, *task, dt); return Move{0, e.yaw}; }
      std::string wk = J.value("work", std::string("idle"));
      if (wk == "read") M.pose |= NP_READ;
      else if (wk == "hammer" || wk == "hoe" || wk == "chisel") { M.work = wk; M.workAt = folk->work; M.workT -= dt; if (M.workT <= 0) { M.workT = 1.1f + rnd() * 0.8f; e.swingT = 0.3f; npcWorkSound(lv, e, job); } }
      else if (p && dist < 5) e.yaw = turnTo(e.yaw, hdg(e.pos, p->pos), dt * 4);
      else M.pose |= NP_FOLD;
      return Move{0, e.yaw};
    }
  }
  // standing about: chat with a neighbour, or look at you
  if (p && dist < 5) { e.yaw = turnTo(e.yaw, hdg(e.pos, p->pos), dt * 4); return Move{0, e.yaw}; }
  auto* mate = M.chatMate && M.chatT > 0 ? dynamic_cast<Creature*>(lv.entity(M.chatMate)) : nullptr;
  if (mate && mate->alive() && !mate->removed) {
    M.chatT -= dt; e.yaw = turnTo(e.yaw, hdg(e.pos, mate->pos), dt * 5); M.pose |= NP_TALK;
    if (rnd() < dt * 0.3f && dist < 14) npcMutter(lv, e, VD()["chat"]);
    return Move{0, e.yaw};
  }
  if (rnd() < dt * 0.15f) {
    for (auto& up : lv.entities()) {
      auto* o = dynamic_cast<Creature*>(up.get()); const json* ON = o ? npcOf(*o) : nullptr;
      if (!ON || o == &e || o->removed || ON->value("v", "") != v.id || isDefender(*o) || std::hypot(o->pos.x - e.pos.x, o->pos.z - e.pos.z) >= 2.6f) continue;
      NpcMind& OM = S->mind(o->id);
      if (OM.chatT > 0) continue;
      M.chatMate = o->id; OM.chatMate = e.id; M.chatT = OM.chatT = 5 + rnd() * 6; M.hasGoal = OM.hasGoal = false;
      break;
    }
  }
  M.pose |= NP_FOLD;
  return Move{0, e.yaw};
}

OptMove npcBrain(Level& lv, Creature& e, Entity* p, float dist, float dt) {
  VillagerSystem* S = vsys(lv);
  if (!S || lv.isRemote()) return std::nullopt;
  if (!npcOf(e)) return Move{0, e.yaw};   // a golem of no village stands where it is
  json& N = e.data["npc"];
  NpcMind& M = S->mind(e.id);
  if (M.pinned) { e.noGravity = false; e.lockMove = false; M.pinned = false; }
  if (M.climbing && !M.wentLast) npcUnclimb(e, M);   // (off a ladder when they've stopped walking a way)
  M.wentLast = false;
  M.pose = 0; M.sitHeld.clear(); M.work.clear();
  // towns3.js: nobody stays stuck
  if ((M.stuckCheckT -= dt) <= 0) { M.stuckCheckT = 1; npcStuckCheck(lv, e, M); }
  Move out{0, e.yaw};
  // on a lift that's carrying them: they stay on it till it's there, whatever else the day wants of them (they walked
  // off a moving platform onto the floor it was passing)
  if (M.rideStage == 1 && M.rideS.x != INT32_MIN)
    if (OptMove m = npcRideLift(lv, e, M, M.rideS, M.rideTo, dt)) { N["p"] = M.pose; return *m; }
  if (M.sent) {   // (sent somewhere: there first)
    if (OptMove m = npcGo(lv, e, M, M.sentTo, dt, e.T->speed * 0.42f)) { N["p"] = M.pose; return *m; }
    M.sent = false;
  }
  if (N.value("i", 0) == 300) { OptMove m = lostAI(lv, e, M, dt); out = m ? *m : out; }
  else if (N.value("i", 0) == 400) { OptMove m = travellerAI(lv, e, M, p, dist, dt); out = m ? *m : out; }
  else if (VillageRef v = villageById(lv, N.value("v", std::string()))) {
    const int i = N.value("i", 0);
    // a broad body (a golem) shut in where it can't get out at its width — among a market's stalls, from before it was
    // set down on open ground — goes once to the nearest open ground with a way to the square, while nobody watches
    if (e.w > 0.5f && (M.wedgeT -= dt) <= 0) {
      M.wedgeT = 20;
      World& w = lv.world(e.realm);
      const vec3 sq(v->x + 0.5f, v->y + 1, v->z + 0.5f);
      bool partial = true;
      const auto way = npcPath(w, e.pos, sq, e.w, e.h, &partial, 4000);
      vec3 o;
      // (shut in: no room for all of it where it stands, or no way on more than a step or two — not merely far)
      bool room = false;
      const int bx = ifloor(e.pos.x + 0.5f) - 1, bz = ifloor(e.pos.z + 0.5f) - 1, by = ifloor(e.pos.y + 0.45f);
      for (int dz = -1; dz <= 1 && !room; dz++) for (int dx = -1; dx <= 1 && !room; dx++) for (int dy : {0, 1, -1}) if (!std::isnan(npcStandAt(w, bx + dx, by + dy, bz + dz, e.w, e.h))) { room = true; break; }
      auto G = golemGround(lv, *v, e);
      const bool pocket = G && !G->cells.empty() && !G->near(e.pos);   // (off the ground it can reach from the square: a pocket)
      if ((!room || pocket || (partial && way.size() < 4)) && !nearestPawn(lv, e.realm, e.pos, 24) && [&] {
            if (G && !G->cells.empty()) {   // (the nearest of the ground it can reach from the square)
              float bd = 1e9f; for (i64 k : G->cells) { const vec3 at = G->spot(k); const float d = glm::distance(at, e.pos); if (d < bd) { bd = d; o = at; } }
              return true;
            }
            return npcOpenGround(w, e.pos, sq, e.w, e.h, o); }()) {
        e.pos = o + vec3(0, 0.01f, 0); e.vel = vec3(0); M.hasPath = false; M.stuckGo = 0; npcNavStats().rescues++;
      }
    }
    // the first guard takes a turn on lookout now and then, up a corner tower's lift (sim/villager_lookout.cpp); a
    // monster about, or someone to have a word with, comes first
    if (i == 100 && e.type == "village_guard" && !M.confront && v->hasWall && M.holdT <= 0 && !npcFoe(lv, e, M, *v))
      if (OptMove m = lookoutAI(lv, e, M, *v, dt)) { N["p"] = M.pose; return *m; }
    out = brainInner(lv, e, M, N, *v, p, dist, dt);
    // towns3.js: the watch at the gates — each guard keeps one, either side of the road just inside, looking out
    if (i >= 100 && i < 200 && e.type == "village_guard" && !M.foe && !M.confront && v->hasWall) {
      const auto& gates = gatesOf(lv, *v);
      if (!gates.empty()) {
        const int gi = i - 100;
        const VGate& G = gates[gi % gates.size()];
        const vec3 post = G.posts[(gi / (int)gates.size()) % 2];
        float dPost = std::hypot(post.x - e.pos.x, post.z - e.pos.z);
        if (M.goalSerial != M.lastSerial) {   // the brain has just picked somewhere new to go
          if (M.patrol) { M.patrol = false; M.goal = post; M.goalT = 60; }
          else if (M.watchT < (M.watchLen ? M.watchLen : (M.watchLen = 45 + rnd() * 75))) { M.goal = post; M.goalT = 60; }
          else { M.watchT = 0; M.watchLen = 45 + rnd() * 75; M.patrol = true; }
        }
        M.lastSerial = M.goalSerial;
        if (dPost < 1.3f) {
          M.watchT += dt;
          if (M.lookT > 0 && !(p && dist < 4)) { e.yaw = turnTo(e.yaw, std::atan2((float)G.inX, (float)G.inZ), dt * 3); out = Move{0, e.yaw}; }
        }
      }
    }
  }
  N["p"] = M.pose;
  if (M.work.empty()) { if (N.contains("wk")) N.erase("wk"); }
  else { N["wk"] = M.work; N["wa"] = json::array({M.workAt.x, M.workAt.y, M.workAt.z}); }
  if (M.sitHeld.empty()) { if (N.contains("sh")) N.erase("sh"); } else N["sh"] = M.sitHeld;
  return out;
}

} // namespace dq
