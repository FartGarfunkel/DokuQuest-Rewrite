// sim/redstone_parts.cpp — pistons and sticky pistons (game/redstone.js), what falls off when its support
// goes, lit TNT and explosions (game/blockfx.js), targets (game/rsplace.js).
#include "sim/redstone.h"
#include "sim/redstone_entities.h"
#include "sim/vehicles.h"
#include "sim/physics.h"
#include "sim/player.h"

namespace dq {
using namespace rs;

namespace {
struct Guard { Redstone* r; bool was; explicit Guard(Redstone* r) : r(r), was(r ? r->pistonGuard : false) { if (r) r->pistonGuard = true; } ~Guard() { if (r) r->pistonGuard = was; } };

bool pistonImmovable(const World& w, BlockId id, int x, int y, int z) {
  const Registry& R = reg();
  const BlockDef& b = R.block(id);
  if (b.hardness < 0 || b.hardness >= 40 || R.tPortal[id] || b.has("movingBlock") || b.has("pistonHead") || b.has("extended") || b.has("mech")) return true;
  if (b.key == "obsidian" || b.key == "crying_obsidian" || b.key == "respawn_anchor" || b.key == "reinforced_deepslate") return true;
  if (const_cast<World&>(w).blockEntity(x, y, z)) return true;   // chests, furnaces, signs, jukeboxes... stay put
  return false;
}
bool pistonBreaks(BlockId id) {
  if (!id) return false;
  const Registry& R = reg();
  const BlockDef& b = R.block(id);
  return !R.tSolid[id] || R.tReplaceable[id] || R.tLiquid[id] || b.has("door") || b.has("bed") || b.has("double") || b.render == Render::Model ||
    b.render == Render::Cross || R.tShape[id] == ShapeKind::Cake || R.tShape[id] == ShapeKind::Candle || b.key == "pumpkin" || b.key == "carved_pumpkin" ||
    b.key == "jack_o_lantern" || b.key == "melon" || b.key == "cobweb";
}
bool isSticky(BlockId id) { return id && (id == T().slime || id == T().honey); }
bool freeCell(BlockId id) { const Registry& R = reg(); return !id || R.tLiquid[id] || R.tReplaceable[id]; }
} // namespace

// ------------------------------------------------------------------------------------------ pistons
void Redstone::pistonMove(int x, int y, int z) {
  // a piston that is still moving finishes its last move first
  auto it = anims.find(posKey(x, y, z));
  if (it != anims.end()) { if (auto* a = dynamic_cast<PistonAnim*>(lv.entity(it->second))) if (!a->removed) a->finish(lv); anims.erase(it); }
  BlockId id = w.getBlock(x, y, z);
  int m = w.getMeta(x, y, z), dir = (m & 7) % 6;
  bool ext = reg().block(id).has("extended");
  bool on = powerAt(x, y, z, dir) > 0;
  if (on && !ext) extend(x, y, z, id, dir);
  else if (!on && ext) retract(x, y, z, id, dir);
}
// The blocks that move when pushing (or pulling) from `start` toward `mv`. false if something won't budge.
bool Redstone::gather(int sx, int sy, int sz, int mv, ivec3 piston, Plan& out) {
  out = Plan();
  std::unordered_set<u64> moveSet, breakSet;
  std::deque<ivec3> q{{sx, sy, sz}};
  while (!q.empty()) {
    ivec3 p = q.front(); q.pop_front();
    u64 k = posKey(p.x, p.y, p.z);
    if (moveSet.count(k) || p == piston) continue;
    BlockId id = w.getBlock(p.x, p.y, p.z);
    if (freeCell(id)) continue;
    if (pistonBreaks(id)) { if (breakSet.insert(k).second) out.breaks.push_back({p.x, p.y, p.z, id}); continue; }
    if (pistonImmovable(w, id, p.x, p.y, p.z)) return false;
    moveSet.insert(k);
    out.move.push_back({p.x, p.y, p.z, id, w.getMeta(p.x, p.y, p.z)});
    if (out.move.size() > 12) return false;
    // whatever is in the way moves too
    q.push_back({p.x + D6X[mv], p.y + D6Y[mv], p.z + D6Z[mv]});
    // slime and honey drag their neighbours along
    if (isSticky(id)) for (int d = 0; d < 6; d++) {
      if (d == mv) continue;
      ivec3 n(p.x + D6X[d], p.y + D6Y[d], p.z + D6Z[d]);
      BlockId nid = w.getBlock(n.x, n.y, n.z);
      if (!nid || n == piston || freeCell(nid) || pistonBreaks(nid) || pistonImmovable(w, nid, n.x, n.y, n.z)) continue;
      if (isSticky(nid) && nid != id) continue;   // slime and honey don't stick to each other
      q.push_back(n);
    }
  }
  // destinations must be free (or breakable)
  int band = dimOfY(piston.y);
  for (auto& mvd : out.move) {
    int tx = mvd[0] + D6X[mv], ty = mvd[1] + D6Y[mv], tz = mvd[2] + D6Z[mv];
    if (!inWorldY(ty) || dimOfY(ty) != band) return false;
    u64 k = posKey(tx, ty, tz);
    if (moveSet.count(k)) continue;
    if (ivec3(tx, ty, tz) == piston) return false;
    BlockId tid = w.getBlock(tx, ty, tz);
    if (freeCell(tid)) continue;
    if (pistonBreaks(tid)) { if (breakSet.insert(k).second) out.breaks.push_back({tx, ty, tz, tid}); continue; }
    return false;
  }
  return true;
}
void Redstone::extend(int x, int y, int z, BlockId id, int dir) {
  const Tables& t = T();
  bool sticky = reg().block(id).has("sticky");
  int hx = x + D6X[dir], hy = y + D6Y[dir], hz = z + D6Z[dir];
  if (!inWorldY(hy) || dimOfY(hy) != dimOfY(y)) return;
  Plan plan;
  if (!gather(hx, hy, hz, dir, ivec3(x, y, z), plan)) return;
  carry(plan, dir, [&] { w.setBlock(x, y, z, sticky ? t.stickyExt : t.pistonExt, dir); },
        {{x, y, z, hx, hy, hz, (int)t.head, dir | (sticky ? 8 : 0), 0}}, posKey(x, y, z));
  lv.fx->sound("piston_out", vec3(x, y, z) + vec3(0.5f), 24);
}
void Redstone::retract(int x, int y, int z, BlockId id, int dir) {
  const Tables& t = T();
  Guard g(this);
  bool sticky = reg().block(id).has("sticky");
  int hx = x + D6X[dir], hy = y + D6Y[dir], hz = z + D6Z[dir];
  if (w.getBlock(hx, hy, hz) == t.head) w.setBlock(hx, hy, hz, AIR, 0);
  w.setBlock(x, y, z, sticky ? t.sticky : t.piston, dir);
  lv.fx->sound("piston_in", vec3(x, y, z) + vec3(0.5f), 24);
  std::vector<std::array<int, 9>> anim = {{hx, hy, hz, x, y, z, (int)t.head, dir | (sticky ? 8 : 0), 1}};
  if (sticky) {
    int fx = hx + D6X[dir], fy = hy + D6Y[dir], fz = hz + D6Z[dir];
    BlockId fid = w.getBlock(fx, fy, fz);
    if (!freeCell(fid) && !pistonBreaks(fid) && !pistonImmovable(w, fid, fx, fy, fz)) {
      Plan plan;
      if (gather(fx, fy, fz, OPP6[dir], ivec3(x, y, z), plan)) { carry(plan, OPP6[dir], nullptr, anim, posKey(x, y, z)); return; }
    }
  }
  carry(Plan(), OPP6[dir], nullptr, anim, posKey(x, y, z));
}
// Lift the blocks, break what's in the way, and slide them over with an animation.
void Redstone::carry(const Plan& plan, int mv, const std::function<void()>& then, std::vector<std::array<int, 9>> extra, u64 key) {
  const Tables& t = T();
  Guard g(this);
  for (auto& b : plan.breaks) {
    if (!reg().tLiquid[b[3]]) dropBlockItems(lv, w.realm, b[0], b[1], b[2], (BlockId)b[3]);
    w.setBlock(b[0], b[1], b[2], AIR, 0);
  }
  for (auto& m : plan.move) w.setBlock(m[0], m[1], m[2], AIR, 0);
  auto a = std::make_unique<PistonAnim>();
  a->realm = w.realm; a->mv = mv;
  for (auto& m : plan.move) {
    PistonAnim::Piece p{{m[0], m[1], m[2]}, {m[0] + D6X[mv], m[1] + D6Y[mv], m[2] + D6Z[mv]}, (BlockId)m[3], (u8)m[4]};
    a->pieces.push_back(p);
    w.setBlock(p.to.x, p.to.y, p.to.z, t.moving, 0);
  }
  if (then) then();
  for (auto& e : extra) {
    PistonAnim::Piece p{{e[0], e[1], e[2]}, {e[3], e[4], e[5]}, (BlockId)e[6], (u8)e[7], e[8] != 0};
    a->extra.push_back(p);
    if (!p.retracting) w.setBlock(p.to.x, p.to.y, p.to.z, t.moving, 0);
  }
  ivec3 first = a->pieces.empty() ? a->extra[0].from : a->pieces[0].from;
  a->pos = vec3(first.x + 0.5f, first.y, first.z + 0.5f);
  // whoever is in the way (or riding on top) gets moved along
  std::vector<AABB> boxes;
  for (auto& p : a->pieces) {
    vec3 lo = vec3(glm::min(p.from, p.to)), hi = vec3(glm::max(p.from, p.to)) + vec3(1, 1 + (D6Y[mv] == 0 ? 0.1f : 0.f), 1);
    boxes.push_back({lo, hi});
  }
  for (auto& p : a->extra) if (!p.retracting) boxes.push_back({vec3(p.to), vec3(p.to) + vec3(1)});
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != w.realm || e->type == "piston_anim") continue;
    float hw = e->isPlayer() ? 0.3f : e->w, h = e->isPlayer() ? 1.8f : (e->h > 0 ? e->h : 0.5f);
    AABB eb{e->pos - vec3(hw, 0, hw), e->pos + vec3(hw, h, hw)};
    for (auto& b : boxes) if (b.overlaps(eb)) { a->riders.push_back(e->id); break; }
  }
  Entity* ae = lv.spawn(std::move(a));
  anims[key] = ae->id;
}
void Redstone::finishAnims() {
  for (auto& [k, id] : anims) if (auto* a = dynamic_cast<PistonAnim*>(lv.entity(id))) if (!a->removed) a->finish(lv);
  anims.clear();
}

void PistonAnim::tick(Level& lv, double dt) {
  float step = std::min((float)dt, dur - t);
  t += (float)dt;
  if (step > 0) {
    float k = step / dur;
    for (u32 id : riders) {
      Entity* e = lv.entity(id);
      if (!e || e->removed) continue;
      e->pos.x += D6X[mv] * k; e->pos.z += D6Z[mv] * k;
      if (D6Y[mv]) { e->pos.y += D6Y[mv] * k * (D6Y[mv] > 0 ? 1.02f : 1.f); if (D6Y[mv] > 0) e->vel.y = std::max(e->vel.y, 0.f); }
    }
  }
  if (t >= dur) finish(lv);
}
void PistonAnim::finish(Level& lv) {
  if (done) return;
  done = true; removed = true;
  World* w = lv.worldIfOpen(realm);
  if (!w) return;
  Guard g(redstoneOf(lv, realm));
  BlockId mov = T().moving;
  for (auto& p : pieces) if (w->getBlock(p.to.x, p.to.y, p.to.z) == mov) w->setBlock(p.to.x, p.to.y, p.to.z, p.id, p.meta);
  for (auto& p : extra) if (!p.retracting && w->getBlock(p.to.x, p.to.y, p.to.z) == mov) w->setBlock(p.to.x, p.to.y, p.to.z, p.id, p.meta);
}
json PistonAnim::save() const { json j = Entity::save(); j["type"] = "piston_anim"; return j; }

// ------------------------------------------------------------------------------------------ the world's side
// A piston or its head was broken (by hand or blast): take the other half with it.
void Redstone::pistonPartBroken(int x, int y, int z, BlockId oldId) {
  const Registry& R = reg();
  const BlockDef& b = R.block(oldId);
  Guard g(this);
  for (int e = 0; e < 6; e++) {
    int nx = x + D6X[e], ny = y + D6Y[e], nz = z + D6Z[e];
    BlockId nid = w.getBlock(nx, ny, nz);
    int nm = w.getMeta(nx, ny, nz);
    if (b.has("pistonHead") && R.block(nid).has("extended") && (nm & 7) == OPP6[e]) {
      w.setBlock(nx, ny, nz, AIR, 0);
      dropBlockItems(lv, w.realm, nx, ny, nz, R.block(nid).has("sticky") ? T().sticky : T().piston);
    }
    if (b.has("extended") && R.block(nid).has("pistonHead") && (nm & 7) == e) w.setBlock(nx, ny, nz, AIR, 0);
  }
}
// Levers, buttons and hooks (and redstone torches) fall off when the block they're fixed to goes.
void Redstone::dropAttached(int x, int y, int z) {
  const Registry& R = reg();
  const Tables& t = T();
  for (int e = 0; e < 6; e++) {
    int nx = x + D6X[e], ny = y + D6Y[e], nz = z + D6Z[e];
    BlockId nid = w.getBlock(nx, ny, nz);
    if (!nid) continue;
    u8 role = t.role[nid];
    int nm = w.getMeta(nx, ny, nz), sup;
    if (R.block(nid).has("attached")) sup = role == HOOK ? hookSup(nm) : leverSup(nm);
    else if (role == TORCH || role == TORCH_OFF) sup = TORCH_SUP[(nm & 7) % 5];
    else continue;
    if (sup != OPP6[e]) continue;
    w.setBlock(nx, ny, nz, AIR, 0);
    dropBlockItems(lv, w.realm, nx, ny, nz, role == TORCH_OFF ? t.torch : nid);
  }
}
// An arrow hit a target block: a signal that's stronger the nearer the middle.
void Redstone::targetHit(int x, int y, int z, vec3 hit) {
  BlockId id = w.getBlock(x, y, z);
  if (T().role[id] != TARGET) return;
  float fx = hit.x - x, fy = hit.y - y, fz = hit.z - z;
  // which face it went in by: the coordinate nearest an edge
  float ex = std::min(fx, 1 - fx), ey = std::min(fy, 1 - fy), ez = std::min(fz, 1 - fz), a, b2;
  if (ex <= ey && ex <= ez) { a = fy; b2 = fz; } else if (ey <= ez) { a = fx; b2 = fz; } else { a = fx; b2 = fy; }
  float d = std::max(std::fabs(a - 0.5f), std::fabs(b2 - 0.5f));
  int p = clampv((int)std::ceil(15 * (1 - d * 2)), 1, 15);
  w.setBlock(x, y, z, id, (w.getMeta(x, y, z) & ~15) | p);
  unschedule(x, y, z);
  schedule(x, y, z, 20, "target");
  lv.fx->sound("click", vec3(x, y, z) + vec3(0.5f), 24);
}

// ------------------------------------------------------------------------------------------ TNT
void Redstone::primeTnt(int x, int y, int z, float fuse) {
  vec3 c = vec3(x, y, z) + vec3(0.5f);
  if (!lv.info.rules.value("tntExplodes", true)) {
    lv.fx->sound("fizz", c); lv.fx->particles("burst", c, 8, json{{"color", "white"}});
    dropStack(lv, w.realm, ItemStack("tnt", 1), c);
    return;
  }
  auto t = std::make_unique<PrimedTnt>();
  t->realm = w.realm; t->fuse = fuse;
  t->pos = vec3(x + 0.5f, y, z + 0.5f);
  t->vel = vec3((rnd() - 0.5f) * 0.6f, 2, (rnd() - 0.5f) * 0.6f);
  lv.spawn(std::move(t));
  lv.fx->sound("fuse", c);
}
void PrimedTnt::tick(Level& lv, double dt) {
  float d = (float)dt;
  fuse -= d;
  vel.y = std::max(-30.f, vel.y - 22 * d);
  float f = std::max(0.f, 1 - d * 3);
  vel.x *= f; vel.z *= f;
  if (World* wp = lv.worldIfOpen(realm)) {
    MoveResult r = moveBox(*wp, pos, w, h, vel * d, 0);
    if (r.hitX) vel.x = 0;
    if (r.hitZ) vel.z = 0;
    if (r.hitY) vel.y = 0;
    onGround = r.onGround;
  }
  if (rs::rnd() < d * 20) lv.fx->particles("smoke", pos + vec3(0, 1.05f, 0), 1);
  if (fuse <= 0) { removed = true; explode(lv, realm, pos + vec3(0, 0.5f, 0), 4); }
}
json PrimedTnt::save() const { json j = Entity::save(); j["data"]["fuse"] = fuse; return j; }
void PrimedTnt::load(const json& j) { Entity::load(j); fuse = data.value("fuse", 4.f); }

void explode(Level& lv, int realm, vec3 at, float power, bool noBlocks) {
  if (lv.events && !lv.events->emit("explode", json::array({at.x, at.y, at.z, power, realm}))) return;
  World* wp = lv.worldIfOpen(realm);
  if (!wp) return;
  World& w = *wp;
  const Registry& R = reg();
  int r = (int)power;
  lv.fx->sound("explode", at, 64);
  for (auto& e : lv.entities()) if (!e->removed && e->isPlayer() && e->realm == realm && glm::distance(e->pos, at) < 24) lv.fx->shake(e->id, 0.8f);
  Redstone* rsys = redstoneOf(lv, realm);
  if (!noBlocks && lv.info.rules.value("mobGriefing", true) != false)
    for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
      float d = std::sqrt((float)(dx * dx + dy * dy + dz * dz));
      if (d > r * (0.75f + rnd() * 0.3f)) continue;
      int bx = ifloor(at.x + dx), by = ifloor(at.y + dy), bz = ifloor(at.z + dz);
      BlockId id = w.getBlock(bx, by, bz);
      const BlockDef& b = R.block(id);
      if (!id || R.tLiquid[id] || b.hardness < 0 || b.hardness >= 20 || b.str("mech") == "none") continue;
      if (b.has("tnt")) { w.setBlock(bx, by, bz, AIR, 0); if (rsys) rsys->primeTnt(bx, by, bz, 0.5f + rnd()); continue; }
      int meta = w.getMeta(bx, by, bz);
      if (json* ent = w.blockEntity(bx, by, bz)) {   // spill what it held
        if ((*ent)["items"].is_array()) for (auto& it : (*ent)["items"]) { ItemStack s = ItemStack::fromJson(it); if (!s.empty()) dropStack(lv, realm, s, vec3(bx + 0.5f, by + 0.5f, bz + 0.5f), vec3((rnd() - 0.5f) * 3, 3, (rnd() - 0.5f) * 3)); }
      }
      w.setBlock(bx, by, bz, AIR, 0, SetFlags::Urgent);
      (void)meta;
      if (rnd() < 0.3f) dropBlockItems(lv, realm, bx, by, bz, id);
    }
  lv.fx->particles("explosion", at, 40, json{{"radius", power}});
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != realm || e->ghost) continue;
    bool mob = R.mobs.is_object() && R.mobs.contains(e->type);
    if (e->isPlayer()) {
      Player& p = static_cast<Player&>(*e);
      float d = glm::distance(p.pos + vec3(0, 1, 0), at);
      if (d >= power * 2 || p.dead) continue;
      float k = 1 - d / (power * 2);
      if (!p.creative() && p.mode != "spectator") { p.hp -= std::round(k * 14); p.hurtTime = 0.5f; }
      vec3 dir = (p.pos - at) / std::max(d, 1e-3f);
      p.vel += vec3(dir.x * 10 * k, 6 * k, dir.z * 10 * k);
    } else if (mob && e->hp > 0) {
      float d = glm::distance(e->pos, at);
      if (d < power * 2) { e->hp -= std::round((1 - d / (power * 2)) * 14); e->hurtTime = 0.5f; }
    } else if (isVehicle(*e)) {
      if (glm::distance(e->pos, at) < power * 1.5f) hurtVehicle(lv, *e, nullptr, true);
    }
  }
}

} // namespace dq
