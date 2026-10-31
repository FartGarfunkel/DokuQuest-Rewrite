// sim/blockfx.cpp — port of game/blockfx.js (FallingBlock, PrimedTNT, checkFall, checkHarden,
// primeTNT, explode, trySleep) and game.js onBlockChanged.
#include "sim/blockfx.h"
#include "world/carve_faces.h"
#include "sim/gameplay.h"
#include "sim/combat.h"
#include "sim/rules.h"
#include <random>

namespace dq {

static double rnd() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }

// ---- a block in mid-air: lands where it stops ----
class FallingBlock : public Entity {
public:
  FallingBlock() { type = "falling_block"; w = 0.49f; h = 0.98f; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    World& w = lv.world(realm);
    if (!w.isLoaded(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z))) return;
    vel.y = std::max(-40.f, vel.y - 28 * dt);
    moveEntity(w, *this, dt, {0, 0, false});
    if (!onGround && age <= 20) return;
    removed = true;
    BlockId block = reg().blockId(data.value("block", std::string()));
    u8 meta = (u8)data.value("meta", 0);
    int x = ifloor(pos.x), y = ifloor(pos.y + 0.3f), z = ifloor(pos.z);
    BlockId cur = w.getBlock(x, y, z);
    if (block && (cur == AIR || reg().tReplaceable[cur])) {
      if (cur && !reg().tLiquid[cur]) dropBlockItems(lv, w, x, y, z, cur, true);
      if (w.setBlock(x, y, z, block, meta, SetFlags::Urgent)) { sfxAt(lv, "thud", vec3(x, y, z)); checkHarden(lv, w, x, y, z); return; }
    }
    if (block) dropItem(lv, realm, ItemStack(reg().block(block).key, 1), pos + vec3(0, 0.5f, 0), true);
  }
};

// ---- lit TNT: flashes, then goes off ----
class PrimedTNT : public Entity {
public:
  PrimedTNT() { type = "tnt"; w = 0.49f; h = 0.98f; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    float fuse = data.value("fuse", 4.0f) - dt;
    data["fuse"] = fuse;
    vel.y = std::max(-30.f, vel.y - 22 * dt);
    vel.x *= std::max(0.f, 1 - dt * 3); vel.z *= std::max(0.f, 1 - dt * 3);
    moveEntity(lv.world(realm), *this, dt, {0, 0, false});
    if (rnd() < dt * 20) lv.fx->particles("smoke", pos + vec3(0, 1.05f, 0), 1);
    if (fuse <= 0) { removed = true; explode(lv, realm, pos + vec3(0, 0.5f, 0), 4); }
  }
};

void registerBlockFxEntities() {
  registerEntityFactory("falling_block", [](const std::string&) { return std::unique_ptr<Entity>(new FallingBlock()); });
  registerEntityFactory("tnt", [](const std::string&) { return std::unique_ptr<Entity>(new PrimedTNT()); });
}

// Sand, gravel and powder with nothing under them start to fall.
void checkFall(Level& lv, World& w, int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  if (!bhas(id, BF::Falls)) return;
  const Registry& R = reg();
  BlockId below = w.getBlock(x, y - 1, z);
  if (!inWorldY(y - 1) || !w.isLoaded(x, y - 1, z)) return;
  if (below && !R.tReplaceable[below] && !R.tLiquid[below] && R.tSolid[below]) return;
  if (below && !R.tLiquid[below] && !R.tReplaceable[below]) return;
  u8 meta = w.getMeta(x, y, z);
  w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent);
  auto e = std::make_unique<FallingBlock>();
  e->realm = w.realm; e->pos = vec3(x + 0.5f, y, z + 0.5f);
  e->data = {{"block", R.block(id).key}, {"meta", meta}};
  lv.spawn(std::move(e));
}

// Concrete powder touching water becomes concrete.
void checkHarden(Level& lv, World& w, int x, int y, int z) {
  const BlockDef& d = reg().block(w.getBlock(x, y, z));
  std::string to = d.str("hardensTo");
  if (to.empty()) return;
  static const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
  for (auto& n : N) if (waterAt(w, x + n[0], y + n[1], z + n[2])) { w.setBlock(x, y, z, reg().blockId(to)); sfxAt(lv, "fizz", vec3(x, y, z)); return; }
}

void primeTNT(Level& lv, World& w, int x, int y, int z, float fuse) {
  vec3 c(x + 0.5f, y + 0.5f, z + 0.5f);
  if (!rule(lv, "tntExplodes")) { sfxAt(lv, "fizz", c); burst(lv, c, "white", 8, 0.5f); dropItem(lv, w.realm, ItemStack("tnt", 1), c, true); return; }
  auto e = std::make_unique<PrimedTNT>();
  e->realm = w.realm; e->pos = vec3(x + 0.5f, y, z + 0.5f);
  e->vel = {(float)(rnd() - 0.5) * 0.6f, 2, (float)(rnd() - 0.5) * 0.6f};
  e->data = {{"fuse", fuse}};
  lv.spawn(std::move(e));
  sfxAt(lv, "fuse", c);
}

void explode(Level& lv, int realm, vec3 at, float power, bool noBlocks, u32 source) {
  World& w = lv.world(realm);
  float r = power;
  lv.fx->sound("explode", at, 64);
  for (Player* p : playersIn(lv, realm)) if (glm::distance(p->pos, at) < 32) lv.fx->shake(p->id, 0.8f);
  const Registry& R = reg();
  if (!noBlocks) {
    int ri = (int)std::ceil(r);
    for (int dy = -ri; dy <= ri; dy++) for (int dz = -ri; dz <= ri; dz++) for (int dx = -ri; dx <= ri; dx++) {
      float d = std::sqrt((float)(dx * dx + dy * dy + dz * dz));
      if (d > r * (0.75f + (float)rnd() * 0.3f)) continue;
      int bx = ifloor(at.x + dx), by = ifloor(at.y + dy), bz = ifloor(at.z + dz);
      BlockId id = w.getBlock(bx, by, bz);
      const BlockDef& b = R.block(id);
      if (!id || R.tLiquid[id] || b.hardness < 0 || b.hardness >= 20 || b.str("mech") == "none") continue;
      if (bhas(id, BF::Tnt)) { w.setBlock(bx, by, bz, AIR); primeTNT(lv, w, bx, by, bz, 0.5f + (float)rnd()); continue; }
      if (json* ent = w.blockEntity(bx, by, bz)) { json copy = *ent; spillEntity(lv, w, copy, bx, by, bz); }
      u8 meta = w.getMeta(bx, by, bz);
      w.setBlock(bx, by, bz, AIR, 0, SetFlags::Urgent);
      if (rnd() < 0.3) dropBlockItems(lv, w, bx, by, bz, id, true, meta);
    }
  }
  lv.fx->particles("explosion", at, 40, {{"radius", r}});
  float reach = r * 2;
  for (Player* p : playersIn(lv, realm)) {
    float d = glm::distance(p->pos + vec3(0, 1, 0), at);
    if (d >= reach) continue;
    float k = 1 - d / reach; if (d == 0) d = 1;
    DamageSource src; src.type = "explosion"; src.knock = 0; src.attacker = source;
    damagePlayer(lv, *p, std::round(k * 14), src);
    p->vel.x += (p->pos.x - at.x) / d * 10 * k; p->vel.z += (p->pos.z - at.z) / d * 10 * k; p->vel.y = std::max(p->vel.y, 6 * k);
  }
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != realm || !isMob(*e) || e->hp <= 0) continue;
    float d = glm::distance(e->pos, at);
    if (d < reach) { DamageSource src; src.type = "explosion"; damageEntity(lv, *e, std::round((1 - d / reach) * 14), src); }
  }
}

// Everyone in bed: skip to morning (1.8 s of fade first, like the original).
void tickSleep(Level& lv, double dt) {
  std::vector<Player*> all;
  for (auto& e : lv.entities()) if (e->isPlayer() && !e->removed) all.push_back(static_cast<Player*>(e.get()));
  bool anyAsleep = false, allAsleep = !all.empty();
  for (Player* p : all) { if (p->sleeping) anyAsleep = true; else if (!p->dead) allAsleep = false; }
  double& t = levelSlot<double>(lv, "sleepFade");
  if (!anyAsleep || !allAsleep) { t = 0; return; }
  t += dt;
  if (t < 1.8) return;
  t = 0;
  lv.info.time = DAY_LEN * 0.02;
  lv.info.day++;
  for (Player* p : all) p->sleeping = false;
  toastAll(lv, "Good morning", "good");
  if (lv.events) lv.events->emit("morning", json::array({lv.info.day}));
}

// Things fixed to the block at (x,y,z) whose face is no longer whole drop off: topOK/botOK/sideOK[d] say which faces still
// hold (a plain block: all of them while it is solid; a chiseled block: the faces with all four octants left).
static void dropUnsupported(Level& lv, World& w, int x, int y, int z, BlockId newId, bool topOK, bool botOK, const bool sideOK[4]) {
  const Registry& R = reg();
  const KnownBlocks& B = KB();
  // things that stood on it
  BlockId above = w.getBlock(x, y + 1, z); u8 am = w.getMeta(x, y + 1, z);
  const BlockDef& ad = R.block(above);
  std::string ash = ad.str("shape");
  bool hangs = R.tRender[above] == (u8)Render::Torch ? (am & 7) != 0
             : (ad.has("attached") && (ad.str("rs") == "hook" || (am & 7) != 0)) ? true
             : (ash == "lantern" || ash == "soul_lantern") ? (am & 0x7f) == 1
             : bhas(above, BF::Hangs) || ad.has("cocoa") || ad.has("frame") || (ad.has("altTile") && (am & 1));
  if (above && bhas(above, BF::NeedsSupport) && !hangs && !bhas(above, BF::AlwaysWet) && !topOK) {
    bool keep = bhas(above, BF::Door) && (am & 16);
    if (!keep) { w.setBlock(x, y + 1, z, R.tWlog[above] && (am & WLOG) ? B.water : AIR); dropBlockItems(lv, w, x, y + 1, z, above, true, am); }
  }
  if (above && bhas(above, BF::Falls) && !topOK) checkFall(lv, w, x, y + 1, z);
  // things that hung from it
  BlockId below = w.getBlock(x, y - 1, z); u8 bm = w.getMeta(x, y - 1, z);
  const BlockDef& bd = R.block(below);
  std::string bsh = bd.str("shape");
  if ((bsh == "lantern" || bsh == "soul_lantern") && (bm & 0x7f) == 1 && !botOK && R.block(newId).str("shape") != "chain") {
    w.setBlock(x, y - 1, z, (bm & WLOG) ? B.water : AIR); dropBlockItems(lv, w, x, y - 1, z, below, true);
  } else if ((bhas(below, BF::Hangs) || (bd.has("altTile") && (bm & 1))) && !botOK && !(bhas(newId, BF::Hangs) && bhas(newId, BF::StackPlant) && bhas(below, BF::StackPlant))) {
    w.setBlock(x, y - 1, z, R.tWlog[below] && (bm & WLOG) ? B.water : AIR); dropBlockItems(lv, w, x, y - 1, z, below, true, bm);
  }
  // torches, ladders, signs and cocoa fixed to its sides
  for (int d = 0; d < 4; d++) {
    if (sideOK[d]) continue;
    int nx = x - DIRX[d], nz = z - DIRZ[d];
    BlockId nid = w.getBlock(nx, y, nz); u8 nm = w.getMeta(nx, y, nz);
    if (!nid) continue;
    const BlockDef& nd = R.block(nid);
    static const int torchSide[4] = {3, 2, 4, 1};
    bool attached = false;
    if (R.tRender[nid] == (u8)Render::Torch) attached = (nm & 7) == torchSide[d];
    else if (R.tShape[nid] == ShapeKind::Ladder) attached = (nm & 3) == d;
    else if (bhas(nid, BF::Sign) && (nm & 16)) attached = (nm & 3) == d;
    else if (nd.has("cocoa")) attached = ((nm >> 4) & 3) == d;
    else if (nd.has("frame") || nd.num("rack") == 2) attached = (nm & 3) == d;
    if (attached && (nd.has("frame") || nd.has("rack"))) if (json* fe = w.blockEntity(nx, y, nz)) { json c = *fe; spillEntity(lv, w, c, nx, y, nz); }
    if (attached) { w.setBlock(nx, y, nz, R.tWlog[nid] && (nm & WLOG) ? B.water : AIR); if (nid != B.vine) dropBlockItems(lv, w, nx, y, nz, nid, true, nm); }
  }
}

void onCarveChangedRules(Level& lv, World& w, int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  bool solid = reg().tSolid[id];
  u8 carve = w.getCarve(x, y, z);
  bool sides[4];
  for (int d = 0; d < 4; d++) sides[d] = solid && carveFaceFull(carve, -DIRX[d], 0, -DIRZ[d]);   // the face toward (x-DIRX[d], z-DIRZ[d])
  dropUnsupported(lv, w, x, y, z, id, solid && carveFaceFull(carve, 0, 1, 0), solid && carveFaceFull(carve, 0, -1, 0), sides);
}

// ---- reactions to world changes (Game.onBlockChanged) ----
void onBlockChangedRules(Level& lv, World& w, int x, int y, int z, BlockId oldId, BlockId newId) {
  if (oldId == newId) return;
  const Registry& R = reg();
  const KnownBlocks& B = KB();
  if (!R.tSolid[newId] || R.tShape[newId]) {
    bool s = R.tSolid[newId], sides[4] = {s, s, s, s};
    dropUnsupported(lv, w, x, y, z, newId, s, s, sides);
  }
  // a dirt path under a new solid block packs back into dirt (Minecraft)
  if (R.tSolid[newId] && !R.tShape[newId] && B.dirt_path && B.dirt && w.getBlock(x, y - 1, z) == B.dirt_path) w.setBlock(x, y - 1, z, B.dirt);
  for (auto& h : hooks().blockChanged) h(lv, w, x, y, z, oldId, newId);
  if (bhas(newId, BF::Falls)) checkFall(lv, w, x, y, z);
  if (newId == B.water || R.tLiquid[newId] == 1) {
    static const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
    for (auto& n : N) if (bhas(w.getBlock(x + n[0], y + n[1], z + n[2]), BF::HardensTo)) checkHarden(lv, w, x + n[0], y + n[1], z + n[2]);
  }
}

} // namespace dq
