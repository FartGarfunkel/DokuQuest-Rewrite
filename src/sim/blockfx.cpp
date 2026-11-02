// sim/blockfx.cpp — port of game/blockfx.js (FallingBlock, PrimedTNT, checkFall, checkHarden,
// primeTNT, explode, trySleep) and game.js onBlockChanged.
#include "sim/blockfx.h"
#include "sim/endstory.h"
#include "sim/rules.h"
#include "world/carve_faces.h"
#include "sim/gameplay.h"
#include "sim/combat.h"
#include "sim/rules.h"
#include <random>
#include <unordered_set>

namespace dq {

void villageSeatChanged(Level& lv, World& w, int x, int y, int z, BlockId oldId, BlockId newId);   // sim/villagers.cpp

static double rnd() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }

// ---- a block in mid-air: lands where it stops ----
class FallingBlock : public Entity {
public:
  FallingBlock() { type = "falling_block"; w = 0.49f; h = 0.98f; }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    World& w = lv.world(realm);
    if (!w.isLoaded(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z))) return;
    const bool up = data.value("up", false);   // driftstone falls up (tools/port/skylands_more.py)
    if (up) {
      vel.y = std::min(14.f, vel.y + 18 * dt);
      const MoveOut mo = moveEntity(w, *this, dt, {0, 0, false});
      const bool stopped = mo.hitCeiling || pos.y > WORLD_H - 2;   // met something above
      if (!stopped && age <= 20) return;
    } else {
      vel.y = std::max(-40.f, vel.y - 28 * dt);
      moveEntity(w, *this, dt, {0, 0, false});
      if (!onGround && age <= 20) return;
    }
    removed = true;
    BlockId block = reg().blockId(data.value("block", std::string()));
    u8 meta = (u8)data.value("meta", 0);
    int x = ifloor(pos.x), y = ifloor(pos.y + (up ? 0.7f : 0.3f)), z = ifloor(pos.z);
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
  if (R.block(id).has("floats")) {   // driftstone: nothing above it, and it drifts up until it meets something
    BlockId above = w.getBlock(x, y + 1, z);
    if (!inWorldY(y + 1) || !w.isLoaded(x, y + 1, z)) return;
    if (above && !R.tLiquid[above] && !R.tReplaceable[above]) return;
    u8 meta = w.getMeta(x, y, z);
    w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent);
    auto e = std::make_unique<FallingBlock>();
    e->realm = w.realm; e->pos = vec3(x + 0.5f, y, z + 0.5f);
    e->data = {{"block", R.block(id).key}, {"meta", meta}, {"up", true}};
    lv.spawn(std::move(e));
    return;
  }
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

class Creature; Creature* spawnCreature(Level& lv, int realm, const std::string& type, vec3 p);   // sim/creature.h (it clashes with gameplay.h here)

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
  // nightmares (BTA's doNightmares, Minecraft beta's): asleep in the dark in the Overworld with monsters about, one may find
  // its way to your bed and wake you, and the night goes on
  if (rule(lv, "nightmares") && !lv.isRemote() && rule(lv, "mobSpawning")) {
    bool woken = false;
    for (Player* p : all) {
      if (!p->sleeping || p->realm != REALM_MAIN || dimOfY(ifloor(p->pos.y)) != DIM_OVERWORLD || p->creative()) continue;
      World& w = lv.world(p->realm);
      const int bx = ifloor(p->pos.x), by = ifloor(p->pos.y), bz = ifloor(p->pos.z);
      if ((w.getLight(bx, by, bz) & 15) >= 8 || rnd() > 0.3) continue;   // a lit room keeps them away; and only now and then
      const Registry& R = reg();
      for (int tries = 0; tries < 24 && !woken; tries++) {
        const float a = (float)(rnd() * TAU), d = 2 + (float)rnd() * 3;
        const int x = ifloor(p->pos.x + std::cos(a) * d), z = ifloor(p->pos.z + std::sin(a) * d);
        for (int y = by + 1; y >= by - 2; y--) {
          if (!R.tSolid[w.getBlock(x, y - 1, z)] || R.tSolid[w.getBlock(x, y, z)] || R.tSolid[w.getBlock(x, y + 1, z)] || R.tLiquid[w.getBlock(x, y, z)]) continue;
          if ((w.getLight(x, y, z) & 15) >= 8) break;
          static const char* KIND[3] = {"zombie", "skeleton", "spider"};
          if (spawnCreature(lv, p->realm, KIND[(int)(rnd() * 3) % 3], vec3(x + 0.5f, (float)y, z + 0.5f))) {
            woken = true;
            for (Player* q : all) q->sleeping = false;
            lv.fx->message(p->id, "You were woken by something at your bedside", "bad");
            lv.fx->sound("nightmare", p->pos, 12);   // a sting: a jolt and a dread swell (audio/nether_sfx.cpp)
          }
          break;
        }
      }
      if (woken) break;
    }
    if (woken) return;
  }
  lv.info.time = DAY_LEN * 0.02;
  lv.info.day++;
  for (Player* p : all) p->sleeping = false;
  toastAll(lv, "Good morning", "good");
  // dreams: as the world wears thin the sleepers dream of the same place (the story; riftDream says what, "" = none)
  for (Player* p : all) { const std::string dr = riftDream(lv); if (!dr.empty()) lv.fx->message(p->id, dr, "dream"); }
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
             : (ash == "lantern" || ash == "soul_lantern") ? (am & 3) == 1
             : bhas(above, BF::Hangs) || ad.has("cocoa") || ad.has("frame") || (ad.has("altTile") && (am & 1));
  if (above && bhas(above, BF::NeedsSupport) && !hangs && !bhas(above, BF::AlwaysWet) && !topOK) {
    bool keep = bhas(above, BF::Door) && (am & 16);
    if (!keep) { w.setBlock(x, y + 1, z, R.tWlog[above] && (am & WLOG) ? B.water : AIR); dropBlockItems(lv, w, x, y + 1, z, above, true, am); }
  }
  if (above && bhas(above, BF::Falls) && !topOK) checkFall(lv, w, x, y + 1, z);
  { const BlockId under = w.getBlock(x, y - 1, z); if (under && bhas(under, BF::Falls) && R.block(under).has("floats")) checkFall(lv, w, x, y - 1, z); }   // driftstone held down by it drifts up
  // things that hung from it
  BlockId below = w.getBlock(x, y - 1, z); u8 bm = w.getMeta(x, y - 1, z);
  const BlockDef& bd = R.block(below);
  std::string bsh = bd.str("shape");
  if ((bsh == "lantern" || bsh == "soul_lantern") && (bm & 3) == 1 && ((bm >> 2) & 31) == 0 && !botOK && R.block(newId).str("shape") != "chain") {   // (one on a long chain hangs from higher up)
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

// Pumice (our take on BTA's): it soaks up the lava round it — a flood fill of up to 64 lava cells within 6 — and turns
// molten; water cools molten pumice back. Started when either is put down next to the other.
static void pumiceSoak(World& w, int x, int y, int z) {
  const Registry& R = reg();
  static const BlockId molten = R.blockId("molten_pumice");
  std::vector<ivec3> q{ivec3(x, y, z)}; std::unordered_set<i64> seen; int took = 0;
  auto key = [](int a, int b, int c) { return ((i64)(a & 0xfffff) << 40) ^ ((i64)(b & 0xfffff) << 20) ^ (i64)(c & 0xfffff); };
  seen.insert(key(x, y, z));
  for (size_t i = 0; i < q.size() && took < 64; i++) {
    for (int d = 0; d < 6; d++) {
      int nx = q[i].x + (d == 0) - (d == 1), ny = q[i].y + (d == 2) - (d == 3), nz = q[i].z + (d == 4) - (d == 5);
      if (std::abs(nx - x) > 6 || std::abs(ny - y) > 6 || std::abs(nz - z) > 6 || !seen.insert(key(nx, ny, nz)).second) continue;
      if (R.tLiquid[w.getBlock(nx, ny, nz)] != 2) continue;
      w.setBlock(nx, ny, nz, AIR); took++;
      q.push_back(ivec3(nx, ny, nz));
      if (took >= 64) break;
    }
  }
  if (took && molten) w.setBlock(x, y, z, molten);
}

void onCarveChangedRules(Level& lv, World& w, int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  bool solid = reg().tSolid[id];
  u8 carve = w.getCarve(x, y, z);
  const Registry& R = reg();
  // a face carved back to a slab's inner face still holds what sits half a block in: sinking things on top, torches
  // and item frames on the sides
  auto holds = [&](int nx, int ny, int nz, BlockId on) {
    int dp = carveFaceDepth(carve, nx, ny, nz);
    return solid && (dp == 0 || (dp == 8 && (ny ? R.tSink[on] : (R.tRender[on] == (u8)Render::Torch || R.tShape[on] == ShapeKind::Frame))));
  };
  bool sides[4];
  for (int d = 0; d < 4; d++) sides[d] = holds(-DIRX[d], 0, -DIRZ[d], w.getBlock(x - DIRX[d], y, z - DIRZ[d]));   // the face toward (x-DIRX[d], z-DIRZ[d])
  dropUnsupported(lv, w, x, y, z, id, holds(0, 1, 0, w.getBlock(x, y + 1, z)), solid && carveFaceFull(carve, 0, -1, 0), sides);
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
  // pumice and lava, molten pumice and water
  {
    static const BlockId pumice = R.blockId("pumice"), molten = R.blockId("molten_pumice");
    if (pumice && molten) {
      static const int N6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
      if (newId == pumice) {
        for (auto& n : N6) if (R.tLiquid[w.getBlock(x + n[0], y + n[1], z + n[2])] == 2) { pumiceSoak(w, x, y, z); break; }
      } else if (R.tLiquid[newId] == 2) {
        for (auto& n : N6) if (w.getBlock(x + n[0], y + n[1], z + n[2]) == pumice) { pumiceSoak(w, x + n[0], y + n[1], z + n[2]); break; }
      }
      auto cool = [&](int cx, int cy, int cz) { w.setBlock(cx, cy, cz, pumice); sfxAt(lv, "fizz", vec3(cx + 0.5f, cy + 0.5f, cz + 0.5f)); };
      if (newId == molten) { for (auto& n : N6) if (R.tLiquid[w.getBlock(x + n[0], y + n[1], z + n[2])] == 1) { cool(x, y, z); break; } }
      else if (R.tLiquid[newId] == 1) for (auto& n : N6) if (w.getBlock(x + n[0], y + n[1], z + n[2]) == molten) cool(x + n[0], y + n[1], z + n[2]);
    }
  }
  // a dirt path under a new solid block packs back into dirt (Minecraft)
  if (R.tSolid[newId] && !R.tShape[newId] && B.dirt_path && B.dirt && w.getBlock(x, y - 1, z) == B.dirt_path) w.setBlock(x, y - 1, z, B.dirt);
  for (auto& h : hooks().blockChanged) h(lv, w, x, y, z, oldId, newId);
  villageSeatChanged(lv, w, x, y, z, oldId, newId);   // furniture put down in a village gets used
  if (bhas(newId, BF::Falls)) checkFall(lv, w, x, y, z);
  if (newId == B.water || R.tLiquid[newId] == 1) {
    static const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
    for (auto& n : N) if (bhas(w.getBlock(x + n[0], y + n[1], z + n[2]), BF::HardensTo)) checkHarden(lv, w, x + n[0], y + n[1], z + n[2]);
  }
}

} // namespace dq
