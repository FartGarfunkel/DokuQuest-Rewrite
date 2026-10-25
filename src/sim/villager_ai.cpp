// sim/villager_ai.cpp — the mind of a villager (game/villagers.js npcBrain, npcPath, npcGo, the doors, guards and
// golems, "accidents happen" — the warning and the sword; towns3.js: nobody stays stuck, the watch at the gates;
// hearth.js: the missing villager who waits, waves and follows you home).
#include "sim/villagers_internal.h"
#include "sim/mob_combat.h"
#include "core/registry.h"
#include "world/dims.h"
#include <queue>

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

static std::vector<ivec3> npcPath(const World& w, int sx, int sy, int sz, int tx, int ty, int tz, int maxNodes = 900) {
  doorTables();
  const Registry& R = reg();
  const int H = 2, RR = 48;
  std::vector<ivec3> none;
  if (std::abs(tx - sx) > RR - 2 || std::abs(tz - sz) > RR - 2) return none;
  static const BlockId farmland = reg().blockId("farmland");
  auto pass = [&](int x, int y, int z) { BlockId id = w.getBlock(x, y, z); return (!R.tSolid[id] || g_door[id]) && !g_danger[id]; };
  auto clear = [&](int x, int y, int z) { return pass(x, y, z) && pass(x, y + 1, z); };
  auto ground = [&](int x, int y, int z) { BlockId id = w.getBlock(x, y - 1, z); return (R.tSolid[id] && !g_noStand[id] && !g_danger[id] && !g_door[id]) || R.tLiquid[id] == 1; };
  auto stand = [&](int x, int y, int z) { return y > 0 && y < WORLD_H - H && clear(x, y, z) && ground(x, y, z); };
  auto key = [&](int x, int y, int z) { return (i64)(x - sx + RR) | ((i64)(z - sz + RR) << 8) | ((i64)y << 16); };
  auto hfn = [&](int x, int y, int z) { float dx = std::abs(x - tx), dz = std::abs(z - tz); return std::max(dx, dz) + 0.414f * std::min(dx, dz) + std::abs(y - ty) * 0.6f; };
  struct Node { int x, y, z; float g, f; int parent; bool closed; };
  std::vector<Node> nodes; nodes.reserve(maxNodes * 3);
  std::unordered_map<i64, int> seen;
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
  nodes.push_back({sx, sy, sz, 0, hfn(sx, sy, sz), -1, false});
  seen[key(sx, sy, sz)] = 0; open.push({nodes[0].f, 0});
  int best = 0; float bestH = nodes[0].f; int n = 0;
  static const int DIRS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  while (!open.empty() && n++ < maxNodes) {
    int ci = open.top().second; open.pop();
    if (nodes[ci].closed) continue;
    nodes[ci].closed = true;
    const Node c = nodes[ci];
    float hc = hfn(c.x, c.y, c.z);
    if (hc < bestH) { bestH = hc; best = ci; }
    if (c.x == tx && c.z == tz && std::abs(c.y - ty) <= 1) { best = ci; break; }
    for (auto& dd : DIRS) {
      int dx = dd[0], dz = dd[1], nx = c.x + dx, nz = c.z + dz;
      if (std::abs(nx - sx) >= RR || std::abs(nz - sz) >= RR) continue;
      if (dx && dz && (!clear(c.x + dx, c.y, c.z) || !clear(c.x, c.y, c.z + dz) || g_door[w.getBlock(c.x + dx, c.y, c.z)] || g_door[w.getBlock(c.x, c.y, c.z + dz)])) continue;
      int ny = -1; float cost = dx && dz ? 1.414f : 1;
      if (stand(nx, c.y, nz)) ny = c.y;
      else if (!dx || !dz) {
        if (pass(c.x, c.y + H, c.z) && stand(nx, c.y + 1, nz)) { ny = c.y + 1; cost += 0.6f; }
        else if (clear(nx, c.y, nz)) { for (int d = 1; d <= 3; d++) { if (stand(nx, c.y - d, nz)) { ny = c.y - d; cost += 0.3f * d; break; } else if (!pass(nx, c.y - d, nz)) break; } }
      }
      if (ny < 0) continue;
      if (farmland && w.getBlock(nx, ny - 1, nz) == farmland) cost += 4;   // keep off the crops
      if (R.tLiquid[w.getBlock(nx, ny, nz)]) cost += 3;
      i64 k = key(nx, ny, nz); float g2 = c.g + cost;
      auto it = seen.find(k);
      if (it != seen.end() && (nodes[it->second].closed || nodes[it->second].g <= g2)) continue;
      nodes.push_back({nx, ny, nz, g2, g2 + hfn(nx, ny, nz) * 1.1f, ci, false});
      seen[k] = (int)nodes.size() - 1; open.push({nodes.back().f, (int)nodes.size() - 1});
    }
  }
  if (best == 0) return none;
  std::vector<ivec3> out;
  for (int c = best; c > 0; c = nodes[c].parent) out.push_back(ivec3(nodes[c].x, nodes[c].y, nodes[c].z));
  std::reverse(out.begin(), out.end());
  return out;
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

// walk to a spot: a path that goes through doors (opening them, and shutting them behind). nullopt = there.
static OptMove npcGo(Level& lv, Creature& e, NpcMind& M, vec3 t, float dt, float speed) {
  float d = std::hypot(t.x - e.pos.x, t.z - e.pos.z);
  if (d < 0.45f && std::fabs(t.y - e.pos.y) < 1.5f) { M.hasPath = false; return std::nullopt; }
  M.navT -= dt;
  if (M.navT <= 0 || !M.hasPath || !M.hasNavGoal || std::hypot(t.x - M.navGoal.x, t.z - M.navGoal.z) > 1) {
    M.navT = 2 + rnd();
    M.path = npcPath(lv.world(e.realm), ifloor(e.pos.x), ifloor(e.pos.y + 0.45f), ifloor(e.pos.z), ifloor(t.x), ifloor(t.y + 0.45f), ifloor(t.z));
    M.pathI = 0; M.navGoal = t; M.hasNavGoal = true; M.hasPath = !M.path.empty();
    if (!M.hasPath) { M.stuckGo++; return Move{speed * 0.6f, hdg(e.pos, t)}; }
  }
  auto& P = M.path;
  while (M.pathI < P.size()) {
    const ivec3& nd = P[M.pathI];
    if (std::hypot(nd.x + 0.5f - e.pos.x, nd.z + 0.5f - e.pos.z) < 0.4f && std::fabs(nd.y - e.pos.y) < 1.2f) M.pathI++;
    else break;
  }
  if (M.pathI >= P.size()) return Move{d < 0.45f ? 0 : speed * 0.5f, hdg(e.pos, t)};
  const ivec3 nd = P[M.pathI];
  if (nd.y - e.pos.y > 0.55f && e.onGround) e.wantJump = true;   // only a real step up: no hopping along the paths
  // a closed door on the way: open it
  World& w = lv.world(e.realm);
  for (size_t k : {M.pathI, M.pathI + 1}) {
    if (k >= P.size()) continue;
    const ivec3& q = P[k];
    if (isNpcDoor(w.getBlock(q.x, q.y, q.z)) && std::hypot(q.x + 0.5f - e.pos.x, q.z + 0.5f - e.pos.z) < 2.2f) npcOpen(lv, e, M, q.x, q.y, q.z);
  }
  float prog = M.hasNav ? std::hypot(e.pos.x - M.navXZ.x, e.pos.z - M.navXZ.y) : 0;
  M.navXZ = vec2(e.pos.x, e.pos.z); M.hasNav = true;
  M.stuckT = prog < dt * 0.25f ? M.stuckT + dt : 0;
  if (M.stuckT > 1.5f) { M.stuckT = 0; M.navT = 0; if (e.stepAhead(lv)) e.wantJump = true; else M.sideStepT = 0.6f; M.stuckGo++; }
  vec3 nc(nd.x + 0.5f, nd.y, nd.z + 0.5f);
  // blocked by someone in the way: step round them for a moment instead of jumping
  if (M.sideStepT > 0) { M.sideStepT -= dt; return Move{speed * 0.7f, hdg(e.pos, nc) + (e.id % 2 ? 1.2f : -1.2f)}; }
  return Move{speed, hdg(e.pos, nc)};
}

// ------------------------------------------------------------------ guards and golems
static vec3 npcPickSpot(Level& lv, const Village& v, bool plaza) {
  if (plaza || v.roads.empty()) {
    float lo = v.tierN() ? (v.fr ? v.fr : 1) + 2.f : 3.f, a = rnd() * TAU;
    float d = v.tierN() ? lo + rnd() * std::max(1.5f, (v.P ? v.P : 7) - 2 - lo) : 3 + rnd() * 3.5f;
    return vec3(v.x + 0.5f + std::cos(a) * d, v.y + 1, v.z + 0.5f + std::sin(a) * d);
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
    e.attackCd = golem ? 1.4f : 0.9f; e.swingT = 0.3f;
    float dmg = golem ? 7 + std::floor(rnd() * 8) : e.T->damage;
    DamageSrc s; s.knock = golem ? 2 : 1;
    damageEntity(lv, foe, dmg, &e, "mob", s);
    if (golem && !foe.isPlayer()) foe.vel.y = 7;
    if (golem) lv.fx->sound("anvil_land", e.pos + vec3(0, 1.5f, 0));
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
  lv.fx->voice("villager", "say", e.pos + vec3(0, 1.6f, 0), V.pitch / 235.f);
  (void)line; (void)vol;
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
  if (j.find("smith") != std::string::npos || j.find("armorer") != std::string::npos || j.find("mason") != std::string::npos) lv.fx->sound("anvil", e.pos + vec3(0, 1, 0));
  else if (j == "farmer") lv.fx->sound("dig_gravel", e.pos + vec3(0, 0.5f, 0));
}

// towns3.js: a townsperson who ends up inside a wall or under the ground finds their way back to the surface
void npcStuckCheck(Level& lv, Creature& m, NpcMind& M) {
  World& w = lv.world(m.realm);
  const Registry& R = reg();
  int fx = ifloor(m.pos.x), fz = ifloor(m.pos.z), fy = ifloor(m.pos.y + 0.05f), hy = ifloor(m.pos.y + std::min(1.6f, m.h - 0.2f));
  if (!w.isLoaded(fx, fy, fz)) return;
  bool inWall = R.tSolid[w.getBlock(fx, hy, fz)] && R.tSolid[w.getBlock(fx, fy, fz)];
  bool voidFall = (fy & 255) < 2;
  if (!inWall && !voidFall) { M.stuckN = 0; return; }
  if (++M.stuckN < 2) return;   // twice in a row (two seconds): a moment's squeeze through a doorway doesn't count
  M.stuckN = 0;
  int base = dimBase(dimOfY(fy));
  for (int y = std::max(base + 1, fy); y < base + DIM_H - 2; y++)
    if (R.tSolid[w.getBlock(fx, y - 1, fz)] && !R.tSolid[w.getBlock(fx, y, fz)] && !R.tSolid[w.getBlock(fx, y + 1, fz)]) {
      m.pos = vec3(fx + 0.5f, y + 0.02f, fz + 0.5f); m.vel = vec3(0); m.fallDist = 0; return;
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

// ------------------------------------------------------------------ the mind of a villager
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
      if (golem || v.roads.empty()) M.goal = npcPickSpot(lv, v, true);
      else {
        const VRoad& rd = v.roads[(size_t)std::floor(rnd() * v.roads.size())];
        int t = std::min(rd.L, 6 + (int)std::floor(rnd() * rd.L));
        M.goal = rnd() < 0.35f ? npcPickSpot(lv, v, true) : vec3(v.x + DXL[rd.d] * t + 0.5f, v.y + 1, v.z + DZL[rd.d] * t + 0.5f);
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
  bool hasGoal = false; vec3 goal(0);
  const VSeatSpot* seat = v.seats.empty() ? nullptr : &v.seats[idx % v.seats.size()];
  if (night && folk) { goal = vec3(folk->home); hasGoal = true; act = SLEEP; }
  else if (ph < 0.04) act = IDLE;
  else if (folk && folk->hasWork && (ph < 0.3 || (shop && ph < 0.46))) { goal = vec3(folk->work); hasGoal = true; act = WORK; }
  else if (ph >= 0.43 && seat) {
    goal = seat->walk ? vec3(seat->ax, seat->at.y, seat->az) : vec3(seat->at.x + DXL[seat->f], seat->at.y, seat->at.z + DZL[seat->f]); hasGoal = true; act = SIT;   // walk up to the bench, then sit
    if (idx >= (int)v.seats.size()) { act = SOCIAL; hasGoal = false; }
  } else act = SOCIAL;
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
    if (d > 0.6f && !seated) {
      // stuck for a long time and nobody about to see it: step there (villagers know their way home)
      if (M.stuckGo > 8 && !nearestPawn(lv, e.realm, e.pos, 24)) { e.pos = vec3(gx, goal.y + 0.01f, gz); M.stuckGo = 0; M.hasPath = false; return Move{0, e.yaw}; }
      if (M.stuckGo > 10 && soc) { M.hasGoal = false; M.stuckGo = 0; }
      if (OptMove m = npcGo(lv, e, M, vec3(gx, goal.y, gz), dt, e.T->speed * (act == SLEEP ? 0.55f : 0.42f))) return *m;
    }
    M.stuckGo = 0;
    auto pin = [&]() { e.vel = vec3(0); e.noGravity = true; e.lockMove = true; M.pinned = true; };
    if (act == SLEEP) {
      int f = folk->homeF & 3;
      e.pos = vec3(goal.x + 0.5f + DXL[f] * 0.5f, goal.y + 0.5625f, goal.z + 0.5f + DZL[f] * 0.5f);
      e.yaw = std::atan2((float)-DXL[f], (float)-DZL[f]); M.pose |= NP_SLEEP; pin();
      return Move{0, e.yaw};
    }
    if (act == SIT) {   // (0.9.0.11) on the seat the way you sit on one: half a block up, held still
      M.sitHeld = seat->held; e.pos = vec3(seat->at.x + 0.5f, seat->at.y + 0.25f, seat->at.z + 0.5f);
      e.yaw = std::atan2((float)-DXL[seat->f], (float)-DZL[seat->f]); M.pose |= NP_SIT; pin(); M.hasPath = false;
      return Move{0, e.yaw};
    }
    if (act == WORK) {
      int f = folk->workF & 3; e.yaw = turnTo(e.yaw, std::atan2((float)-DXL[f], (float)-DZL[f]), dt * 6);
      std::string wk = J.value("work", std::string("idle"));
      if (wk == "read") M.pose |= NP_READ;
      else if (wk == "hammer" || wk == "hoe") { M.workT -= dt; if (M.workT <= 0) { M.workT = 1.1f + rnd() * 0.8f; e.swingT = 0.3f; npcWorkSound(lv, e, job); } }
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
  M.pose = 0; M.sitHeld.clear();
  // towns3.js: nobody stays stuck
  if ((M.stuckCheckT -= dt) <= 0) { M.stuckCheckT = 1; npcStuckCheck(lv, e, M); }
  Move out{0, e.yaw};
  if (N.value("i", 0) == 300) { OptMove m = lostAI(lv, e, M, dt); out = m ? *m : out; }
  else if (VillageRef v = villageById(lv, N.value("v", std::string()))) {
    out = brainInner(lv, e, M, N, *v, p, dist, dt);
    // towns3.js: the watch at the gates — each guard keeps one, either side of the road just inside, looking out
    const int i = N.value("i", 0);
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
  if (M.sitHeld.empty()) { if (N.contains("sh")) N.erase("sh"); } else N["sh"] = M.sitHeld;
  return out;
}

} // namespace dq
