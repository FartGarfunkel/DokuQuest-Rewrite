// sim/world_life.cpp — lightning changing creatures, the enderman's carried block, mushrooms and vines that grow
// (sim/world_life.h).
#include "sim/world_life.h"
#include "world/dims.h"
#include "sim/creature.h"
#include "sim/player.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/villagers.h"
#include "core/registry.h"
#include "world/world.h"
#include "world/dirs.h"
#include "world/shapes.h"
#include "sim/blockinfo.h"
#include "gen/huge_mushroom.h"

namespace dq {

namespace {
const double RT = 68.27;   // seconds between a block's random ticks, on average (sim/worldsys.cpp)
int ri(int lo, int hi) { return lo + std::min(hi - lo, (int)(frand() * (hi - lo + 1))); }   // lo..hi
struct LifeIds {
  BlockId brownM, redM, mycelium, podzol, vine, stem, redCap, brownCap, sand, redSand, cactus, moss;
  LifeIds() {
    const Registry& R = reg();
    brownM = R.blockId("brown_mushroom"); redM = R.blockId("red_mushroom"); mycelium = R.blockId("mycelium"); podzol = R.blockId("podzol");
    vine = R.blockId("vine"); stem = R.blockId("mushroom_stem"); redCap = R.blockId("red_mushroom_block"); brownCap = R.blockId("brown_mushroom_block");
    sand = R.blockId("sand"); redSand = R.blockId("red_sand"); cactus = R.blockId("cactus"); moss = R.blockId("moss_block");
  }
};
const LifeIds& LI() { static const LifeIds I; return I; }
int rawLight(World& w, int x, int y, int z) { u8 l = w.getLight(x, y, z); return std::max(l >> 4, l & 15); }
bool fullCube(BlockId id) { const Registry& R = reg(); return id && R.tSolid[id] && R.tOpaque[id] && R.tShape[id] == ShapeKind::None && !R.tLiquid[id]; }
bool mushroomGround(BlockId b) { const LifeIds& I = LI(); return b && (b == I.mycelium || b == I.podzol); }
bool soilFor(BlockId b) {   // where a flower may stand
  const Registry& R = reg();
  if (!b) return false;
  if (R.tGrassy[b]) return true;
  static const std::vector<BlockId> S = {R.blockId("dirt"), R.blockId("coarse_dirt"), R.blockId("podzol"), R.blockId("rooted_dirt"), R.blockId("moss_block"), R.blockId("mud")};
  return std::find(S.begin(), S.end(), b) != S.end();
}
bool mushroomCanStay(World& w, int x, int y, int z) {
  BlockId below = w.getBlock(x, y - 1, z);
  if (mushroomGround(below)) return true;
  return fullCube(below) && rawLight(w, x, y, z) <= 12;
}
// a face a vine can hold to: a whole solid block, or leaves
bool clingable(BlockId id) { const Registry& R = reg(); return id && (fullCube(id) || (R.tLeaves[id] && R.tSolid[id])); }
void say(Level& lv, const char* part, vec3 at, int n) { if (lv.fx) lv.fx->particles(part, at, n); }
}

// ------------------------------------------------------------------ lightning
bool isFuseling(const std::string& t) { return t == "fuseling" || (t.size() > 9 && t.compare(t.size() - 9, 9, "_fuseling") == 0); }
bool fuselingCharged(const Entity& e) { return e.data.is_object() && e.data.value("charged", false); }
float chargedBlastScale(const Entity& e) { return isFuseling(e.type) && fuselingCharged(e) ? 2.f : 1.f; }

static void convert(Level& lv, Creature& c, const std::string& to) {
  Creature* n = spawnCreature(lv, c.realm, to, c.pos);
  if (!n) return;
  n->yaw = c.yaw; n->prevPos = c.pos;
  std::string name = c.petName;
  if (name.empty() && c.data.is_object() && c.data.contains("npc") && c.data["npc"].is_object()) name = c.data["npc"].value("n", std::string());   // a villager's own name
  if (!name.empty()) { n->petName = name; n->persistent = true; }
  if (c.persistent || c.kept) n->persistent = true;
  if (c.baby) n->makeBaby();
  if (isVillager(c)) villagerLost(lv, c);   // its village counts it gone, its claims dropped (sim/villagers.h)
  n->data["struck"] = true;   // (made by a bolt: drawn the same as any other, kept so a save shows where it came from)
  c.removed = true;
  say(lv, "smoke", c.pos + vec3(0, c.h * 0.5f, 0), 20);
}

void lightningChangesCreatures(Level& lv, int realm, int x, int y, int z) {
  if (lv.isRemote()) return;
  // who stood by it, gathered first: what the bolt makes isn't struck again by the same bolt
  std::vector<Creature*> near;
  for (auto& e : lv.entities()) {
    auto* c = dynamic_cast<Creature*>(e.get());
    if (!c || c->removed || c->ghost || !c->alive() || c->realm != realm) continue;
    if (std::hypot(c->pos.x - x - 0.5f, c->pos.z - z - 0.5f) < 4 && std::abs(c->pos.y - y) < 5) near.push_back(c);
  }
  const bool witch = isCreatureType("witch");
  for (Creature* c : near) {
    if (isFuseling(c->type)) {
      if (!fuselingCharged(*c)) { c->data["charged"] = true; say(lv, "crit", c->pos + vec3(0, 1, 0), 24); }
    } else if (c->type == "pig") convert(lv, *c, "zombie_pigman");
    else if (c->type == "villager" && witch) convert(lv, *c, "witch");
  }
}

// ------------------------------------------------------------------ the enderman's block
bool blockCarriable(BlockId id) {
  static std::vector<u8> T;
  const Registry& R = reg();
  if (T.empty()) { T.assign(R.blocks.size() + 1, 0); for (auto& b : R.blocks) if (b.id < T.size()) T[b.id] = b.has("carriable"); }
  return id && id < T.size() && T[id];
}
bool blockNatural(BlockId id) {
  static std::vector<u8> T;
  const Registry& R = reg();
  if (T.empty()) { T.assign(R.blocks.size() + 1, 0); for (auto& b : R.blocks) if (b.id < T.size()) T[b.id] = b.has("natural") || b.has("carriable"); }
  return id == AIR || (id < T.size() && T[id]);
}
bool nearBuilt(World& w, int x, int y, int z) {
  for (int dy = -2; dy <= 2; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) if (!blockNatural(w.getBlock(x + dx, y + dy, z + dz))) return true;
  return false;
}
std::string carriedBlock(const Entity& e) {
  if (!e.data.is_object()) return "";
  auto it = e.data.find("carry");
  return it != e.data.end() && it->is_string() ? it->get<std::string>() : "";
}

bool enderTryLift(Level& lv, Creature& m) {
  if (!carriedBlock(m).empty() || !rule(lv, "mobGriefing")) return false;
  World& w = lv.world(m.realm);
  const int fx = ifloor(m.pos.x), fy = ifloor(m.pos.y), fz = ifloor(m.pos.z);
  const int x = fx + ri(-2, 2), y = fy + ri(-1, 2), z = fz + ri(-2, 2);
  const BlockId id = w.getBlock(x, y, z);
  if (!blockCarriable(id)) return false;
  // never a block a player put down, nor one in among them (a wall of dirt, a planted bed)
  if (skillWasPlaced(lv, m.realm, x, y, z)) return false;
  for (int k = 0; k < 6; k++) if (skillWasPlaced(lv, m.realm, x + D6X[k], y + D6Y[k], z + D6Z[k])) return false;
  // nothing sits on it (a torch, a flower someone set there, the next block of sand)
  if (w.getBlock(x, y + 1, z) != AIR) return false;
  // and nothing made lies near it: planks, bricks, glass, a torch, a station (a build from before the record was kept)
  if (nearBuilt(w, x, y, z)) return false;
  const u8 meta = w.getMeta(x, y, z);
  w.setBlock(x, y, z, AIR, 0);
  m.data["carry"] = reg().block(id).key; m.data["carryMeta"] = meta;
  if (lv.fx) lv.fx->material(reg().block(id).sound, "break", vec3(x + 0.5f, y + 0.5f, z + 0.5f));
  return true;
}

static bool fitsAt(World& w, BlockId id, int x, int y, int z) {
  const Registry& R = reg(); const LifeIds& I = LI();
  if (w.getBlock(x, y, z) != AIR) return false;
  const BlockId below = w.getBlock(x, y - 1, z);
  if (!fullCube(below) && !(id == I.cactus && below == I.cactus)) return false;
  if (id == I.brownM || id == I.redM) return mushroomCanStay(w, x, y, z);
  if (id == I.cactus) {
    if (below != I.sand && below != I.redSand && below != I.cactus) return false;
    for (int k = 0; k < 4; k++) if (R.tSolid[w.getBlock(x + DIRX[k], y, z + DIRZ[k])]) return false;
    return true;
  }
  if (bhas(id, BF::NeedsSupport) || R.tRender[id] == (u8)Render::Cross) return soilFor(below);   // a flower: on soil
  return true;
}

bool enderTrySetDown(Level& lv, Creature& m) {
  const std::string key = carriedBlock(m);
  if (key.empty()) return false;
  const BlockId id = reg().blockId(key);
  if (!id) { m.data.erase("carry"); m.data.erase("carryMeta"); return false; }
  World& w = lv.world(m.realm);
  const int x = ifloor(m.pos.x) + ri(-2, 2), y = ifloor(m.pos.y) + ri(-1, 2), z = ifloor(m.pos.z) + ri(-2, 2);
  if (!fitsAt(w, id, x, y, z)) return false;
  // not into anyone (itself included)
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != m.realm || e->type == "item") continue;
    const float hw = std::max(0.3f, e->w);
    if (e->pos.x + hw > x && e->pos.x - hw < x + 1 && e->pos.z + hw > z && e->pos.z - hw < z + 1 && e->pos.y < y + 1 && e->pos.y + std::max(0.5f, e->h) > y) return false;
  }
  const u8 meta = (u8)m.data.value("carryMeta", 0);
  w.setBlock(x, y, z, id, meta);
  m.data.erase("carry"); m.data.erase("carryMeta");
  if (lv.fx) lv.fx->material(reg().block(id).sound, "place", vec3(x + 0.5f, y + 0.5f, z + 0.5f));
  return true;
}

void enderCarryTick(Level& lv, Creature& m, float dt) {
  if (lv.isRemote() || m.ghost || !m.alive()) return;
  if (carriedBlock(m).empty()) { if (frand() < dt / 20) enderTryLift(lv, m); }   // now and then it reaches down
  else if (frand() < dt / 30) enderTrySetDown(lv, m);                            // and some while later sets it down
}

void enderDropCarried(Level& lv, Creature& m) {
  const std::string key = carriedBlock(m);
  if (key.empty()) return;
  m.data.erase("carry"); m.data.erase("carryMeta");
  if (lv.isRemote()) return;
  std::string item = key;
  if (!reg().item(item)) { const BlockId id = reg().blockId(key); item = id ? reg().block(id).drop : ""; }
  if (!item.empty() && reg().item(item)) dropItem(lv, m.realm, ItemStack(item, 1), m.pos + vec3(0, 1.2f, 0), true);
}

// ------------------------------------------------------------------ mushrooms and vines
int countNear(World& w, int x, int y, int z, BlockId id, int rxz, int ry) {
  int n = 0;
  for (int dy = -ry; dy <= ry; dy++) for (int dz = -rxz; dz <= rxz; dz++) for (int dx = -rxz; dx <= rxz; dx++) if (w.getBlock(x + dx, y + dy, z + dz) == id) n++;
  return n;
}

void mushroomTick(Level& lv, World& w, int x, int y, int z, BlockId id, bool force) {
  // bright light (over 12) withers it when a random tick looks, unless it stands on mycelium or podzol; the land makes
  // them only in the shade (gen/decorate.cpp, gen/biome_features.cpp: under rock, or enough leaves), so the ones it was made
  // with stay, and none litter a sunny floor
  if (!mushroomGround(w.getBlock(x, y - 1, z)) && rawLight(w, x, y, z) > 12) {
    if (force || frand() < 0.5f) {
      w.setBlock(x, y, z, AIR, 0);
      dropItem(lv, w.realm, ItemStack(reg().block(id).key, 1), vec3(x + 0.5f, y + 0.3f, z + 0.5f), true);
    }
    return;
  }
  if (!force && frand() > 1.0f / 25) return;   // spreading is slow
  if (countNear(w, x, y, z, id, 4, 1) >= 5) return;   // a few of a kind to a 9x3x9 patch
  auto step = [&](ivec3 p) { return p + ivec3(ri(-1, 1), ri(0, 1) - ri(0, 1), ri(-1, 1)); };
  ivec3 at(x, y, z), to = step(at);
  for (int k = 0; k < 4; k++) {
    if (w.getBlock(to.x, to.y, to.z) == AIR && mushroomCanStay(w, to.x, to.y, to.z)) at = to;
    to = step(at);
  }
  // (never next to water or lava: a flow washes it off as an item, and a patch by a lava fall refilled itself as fast as
  // the lava took it, a stream of loose mushrooms)
  auto wet = [&](ivec3 p) { for (const ivec3 d : {ivec3(1, 0, 0), ivec3(-1, 0, 0), ivec3(0, 0, 1), ivec3(0, 0, -1), ivec3(0, 1, 0)}) if (reg().tLiquid[w.getBlock(p.x + d.x, p.y + d.y, p.z + d.z)]) return true; return false; };
  if (w.getBlock(to.x, to.y, to.z) == AIR && mushroomCanStay(w, to.x, to.y, to.z) && dimOfY(to.y) == dimOfY(y) && !wet(to)) w.setBlock(to.x, to.y, to.z, id, 0);
}

static const int VINE_CAP = 5;       // vines to a 9x3x9 patch before it stops creeping sideways and up
static const int VINE_HANG = 16;     // how far a strand hangs by itself
static int strandAbove(World& w, int x, int y, int z) { const BlockId v = LI().vine; int n = 0; while (n < 64 && w.getBlock(x, y + n, z) == v) n++; return n; }

void vineTick(Level& lv, World& w, int x, int y, int z, u8 meta, bool force) {
  (void)lv;
  if (!force && frand() > 0.25f) return;
  const BlockId v = LI().vine;
  const int f = meta & 3;
  const int choice = ri(0, 5);   // 0..3 a side, 4 up, 5 down
  if (choice == 5) {
    if (w.getBlock(x, y - 1, z) != AIR || dimOfY(y - 1) != dimOfY(y)) return;
    // a strand hangs only so far below the last vine that holds to a wall
    int free = 0; for (int k = 0; k < VINE_HANG + 1 && w.getBlock(x, y + k, z) == v; k++) { if (clingable(w.getBlock(x + DIRX[f], y + k, z + DIRZ[f]))) break; free++; }
    if (free >= VINE_HANG) return;
    w.setBlock(x, y - 1, z, v, (u8)f);
    return;
  }
  auto room = [&](int tx, int ty, int tz) { return w.getBlock(tx, ty, tz) == AIR && countNear(w, tx, ty, tz, v, 4, 1) < VINE_CAP && countNear(w, x, y, z, v, 4, 1) < VINE_CAP; };
  if (choice == 4) {
    if (dimOfY(y + 1) == dimOfY(y) && clingable(w.getBlock(x + DIRX[f], y + 1, z + DIRZ[f])) && room(x, y + 1, z)) w.setBlock(x, y + 1, z, v, (u8)f);
    return;
  }
  const int k = choice;
  if (k == f || k == ((f + 2) & 3)) return;   // into its own wall, or out into the open
  const int tx = x + DIRX[k], tz = z + DIRZ[k];
  if (w.getBlock(tx, y, tz) != AIR) return;
  if (clingable(w.getBlock(tx + DIRX[f], y, tz + DIRZ[f]))) { if (room(tx, y, tz)) w.setBlock(tx, y, tz, v, (u8)f); return; }   // along the wall
  // round an outside corner: onto the wall's side face
  const int cx = tx + DIRX[f], cz = tz + DIRZ[f];
  if (clingable(w.getBlock(x + DIRX[f], y, z + DIRZ[f])) && room(cx, y, cz)) w.setBlock(cx, y, cz, v, (u8)((k + 2) & 3));
}

bool lifeRandomTick(Level& lv, World& w, int x, int y, int z, BlockId id, u8 meta) {
  const LifeIds& I = LI();
  if (!id) return false;
  if (id == I.brownM || id == I.redM) { mushroomTick(lv, w, x, y, z, id); return true; }
  if (id == I.vine) { vineTick(lv, w, x, y, z, meta); return true; }
  return false;
}

bool growHugeMushroom(World& w, int x, int y, int z, BlockId small) {
  const LifeIds& I = LI(); const Registry& R = reg();
  if (!I.stem || !I.redCap || !I.brownCap || !mushroomCanStay(w, x, y, z)) return false;
  const bool red = small == I.redM;
  const int hgt = ri(5, 7);
  std::vector<ivec3> cells; hugeMushroomCells(red, hgt, cells);
  for (const ivec3& c : cells) {
    const int cy = y + c.y;
    if (dimOfY(cy) != dimOfY(y)) return false;
    if (c == ivec3(0)) continue;
    const BlockId b = w.getBlock(x + c.x, cy, z + c.z);
    if (b != AIR && !R.tLeaves[b] && !(R.tReplaceable[b] && !R.tLiquid[b])) return false;
  }
  buildHugeMushroom([&](int X, int Y, int Z, BlockId b) { w.setBlock(X, Y, Z, b, 0); }, x, y, z, red, hgt, I.stem, I.redCap, I.brownCap);
  return true;
}

bool lifeBonemeal(Level& lv, World& w, int x, int y, int z, BlockId id, bool force) {
  (void)lv;
  const LifeIds& I = LI();
  if (!id) return false;
  if (id == I.brownM || id == I.redM) {
    if (!mushroomCanStay(w, x, y, z)) return false;
    if (force || frand() < 0.4f) growHugeMushroom(w, x, y, z, id);   // (like a sapling: not every time)
    return true;
  }
  if (id == I.vine) {
    int by = y; while (by > y - 64 && w.getBlock(x, by - 1, z) == I.vine) by--;
    const u8 meta = w.getMeta(x, by, z);
    int grown = 0;
    for (int k = 0, n = ri(1, 3); k < n; k++) {
      if (w.getBlock(x, by - 1, z) != AIR || dimOfY(by - 1) != dimOfY(y)) break;
      w.setBlock(x, --by, z, I.vine, meta & 3); grown++;
    }
    return grown > 0;
  }
  return false;
}

} // namespace dq
