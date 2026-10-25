// sim/redstone.cpp — circuits (game/redstone.js): power, dust, torches, repeaters, comparators, observers,
// lamps, bulbs, note blocks, bells, TNT, doors, rails, timers (plates, hooks, detector rails, daylight),
// hoppers, dispensers and droppers. Pistons live in sim/redstone_parts.cpp.
#include "sim/redstone.h"
#include "sim/minecart.h"
#include "sim/vehicles.h"
#include "sim/machines.h"
#include "sim/physics.h"
#include "sim/player.h"
#include <random>

namespace dq {
namespace rs {

const int RAIL_EXITS[10][2][3] = {
  {{0, 0, -1}, {0, 0, 1}}, {{-1, 0, 0}, {1, 0, 0}},
  {{-1, 0, 0}, {1, 1, 0}}, {{-1, 1, 0}, {1, 0, 0}}, {{0, 1, -1}, {0, 0, 1}}, {{0, 0, -1}, {0, 1, 1}},
  {{0, 0, 1}, {1, 0, 0}}, {{0, 0, 1}, {-1, 0, 0}}, {{0, 0, -1}, {-1, 0, 0}}, {{0, 0, -1}, {1, 0, 0}},
};

static Tables g_tables;
const Tables& T() {
  const Registry& R = reg();
  if (g_tables.nblocks == R.blocks.size()) return g_tables;
  Tables& t = g_tables;
  t = Tables();
  t.nblocks = R.blocks.size();
  static const std::unordered_map<std::string, u8> ROLE = {{"wire", WIRE}, {"torch", TORCH}, {"torch_off", TORCH_OFF}, {"block", BLOCK},
    {"lever", LEVER}, {"button", BUTTON}, {"plate", PLATE}, {"repeater", REPEATER}, {"comparator", COMPARATOR}, {"observer", OBSERVER},
    {"lamp", LAMP}, {"piston", PISTON}, {"dispenser", DISPENSER}, {"dropper", DROPPER}, {"hopper", HOPPER}, {"daylight", DAYLIGHT},
    {"target", TARGET}, {"prail", PRAIL}, {"drail", DRAIL}, {"bulb", BULB}, {"hook", HOOK}};
  for (size_t i = 0; i < R.blocks.size() && i < MAX_BLOCKS; i++) {
    const BlockDef& b = R.blocks[i];
    u8 r = 0;
    std::string rr = b.str("rs");
    if (!rr.empty()) { auto it = ROLE.find(rr); if (it != ROLE.end()) r = it->second; }
    if (b.key == "redstone_block") r = BLOCK;
    else if (b.has("noteBlock")) r = NOTE;
    else if (b.has("tnt")) r = TNT;
    else if (b.has("door")) r = DOOR;
    else if (b.has("trapdoor")) r = TRAPDOOR;
    else if (b.shapeKind == ShapeKind::Gate) r = GATE;
    else if (b.has("bellBlock") || b.has("cmdBlock")) r = BELL;
    t.role[i] = r;
    t.cond[i] = b.solid && b.opaque && !b.shapeKind && b.render == Render::Cube && r != BLOCK && r != OBSERVER && r != PISTON ? 1 : 0;
    t.ticker[i] = r == HOPPER ? 1 : r == DAYLIGHT ? 2 : r == PLATE ? 3 : r == HOOK ? 4 : r == DRAIL ? 5 : 0;
    std::string rail = b.str("rail");
    t.rail[i] = rail == "normal" ? RAIL_NORMAL : rail == "powered" ? RAIL_POWERED : rail == "detector" ? RAIL_DETECTOR : rail == "activator" ? RAIL_ACTIVATOR : RAIL_NONE;
    t.mech[i] = b.has("mech") ? 1 : 0;
  }
  auto id = [&](const char* k) { return R.blockId(k); };
  t.wire = id("redstone_wire"); t.torch = id("redstone_torch"); t.torchOff = id("redstone_torch_off");
  t.lamp = id("redstone_lamp"); t.lampOn = id("redstone_lamp_on"); t.bulb = id("copper_bulb"); t.bulbLit = id("copper_bulb_lit");
  t.piston = id("piston"); t.sticky = id("sticky_piston"); t.pistonExt = id("piston_extended"); t.stickyExt = id("sticky_piston_extended");
  t.head = id("piston_head"); t.moving = id("moving_block"); t.slime = id("slime_block"); t.honey = id("honey_block");
  t.tripwire = id("tripwire"); t.water = id("water"); t.lava = id("lava"); t.fire = id("fire");
  t.ore = id("redstone_ore"); t.oreLit = id("redstone_ore_lit"); t.lily = id("lily_pad"); t.berryEmpty = id("berry_bush_empty");
  return t;
}

int railShape(BlockId id, int m) { return T().rail[id] == RAIL_NORMAL ? (m & 15) % 10 : (m & 7) % 6; }

// Does dust beside a block (in direction h) run toward it?
static bool rsAttracts(BlockId id, int m, int h) {
  switch (T().role[id]) {
    case WIRE: case TORCH: case TORCH_OFF: case BLOCK: case LEVER: case BUTTON: case PLATE:
    case COMPARATOR: case DAYLIGHT: case TARGET: case DRAIL: case HOOK: return true;
    case REPEATER: return ((m & 3) & 1) == (h & 1);
    case OBSERVER: return (m & 7) == H2D6[h];
  }
  return false;
}
void wireConnects(const World& w, int x, int y, int z, int out[4]) {
  const Tables& t = T();
  const Registry& R = reg();
  bool upCond = t.cond[w.getBlock(x, y + 1, z)];
  for (int h = 0; h < 4; h++) {
    int dx = DIRX[h], dz = DIRZ[h];
    BlockId n = w.getBlock(x + dx, y, z + dz);
    int c = 0;
    if (rsAttracts(n, w.getMeta(x + dx, y, z + dz), h)) c = 1;
    else if (!upCond && R.tSolid[n] && t.role[w.getBlock(x + dx, y + 1, z + dz)] == WIRE) c = 2;
    else if (!t.cond[n] && t.role[w.getBlock(x + dx, y - 1, z + dz)] == WIRE) c = 1;
    out[h] = c;
  }
}

float rnd() { static thread_local std::mt19937 g(0xD0C0u); return std::uniform_real_distribution<float>(0.f, 1.f)(g); }

std::string doorSound(const BlockDef& b, bool open) {
  const std::string& k = b.key;
  bool metal = startsWith(k, "iron") || startsWith(k, "copper") || k.find("_iron") != std::string::npos || k.find("_copper") != std::string::npos;
  std::string kind = metal ? "door_iron" : b.has("trapdoor") ? "trapdoor" : b.shapeKind == ShapeKind::Gate ? "gate" : "door";
  return kind + (open ? "_open" : "_close");
}

Entity* dropStack(Level& lv, int realm, const ItemStack& s, vec3 pos, vec3 vel, float pickupDelay) {
  if (s.empty()) return nullptr;
  auto e = createEntity("item");
  e->type = "item"; e->realm = realm; e->pos = pos; e->vel = vel; e->w = 0.125f; e->h = 0.25f;
  e->data["stack"] = s.toJson();
  e->data["pickupDelay"] = pickupDelay;
  return lv.spawn(std::move(e));
}
void dropBlockItems(Level& lv, int realm, int x, int y, int z, BlockId id) {
  const BlockDef& b = reg().block(id);
  std::string drop = b.drop.empty() ? b.key : b.drop;
  if (drop == "air" || drop == "none" || !reg().item(drop)) return;
  dropStack(lv, realm, ItemStack(drop, 1), vec3(x + 0.5f, y + 0.3f, z + 0.5f), vec3((rnd() - 0.5f) * 2, 3, (rnd() - 0.5f) * 2));
}
bool boxFree(const World& w, vec3 lo, vec3 hi) {
  std::vector<AABB> tmp; AABB box{lo, hi};
  for (int y = ifloor(lo.y); y <= ifloor(hi.y - 1e-4f); y++) for (int z = ifloor(lo.z); z <= ifloor(hi.z - 1e-4f); z++) for (int x = ifloor(lo.x); x <= ifloor(hi.x - 1e-4f); x++) {
    tmp.clear(); blockBoxes(w, x, y, z, tmp);
    for (auto& b : tmp) if (b.overlaps(box)) return false;
  }
  return true;
}
Player* playerNamed(Level& lv, const std::string& n) {
  for (auto& e : lv.entities()) if (!e->removed && e->isPlayer() && static_cast<Player*>(e.get())->name == n) return static_cast<Player*>(e.get());
  return nullptr;
}
} // namespace rs

using namespace rs;

int fillSignal(const json& items) {
  if (!items.is_array() || items.empty()) return 0;
  double f = 0; bool any = false;
  for (auto& j : items) { ItemStack s = ItemStack::fromJson(j); if (!s.empty()) { f += (double)s.n / s.maxStack(); any = true; } }
  return any ? (int)std::floor(1 + (f / items.size()) * 14) : 0;
}

// ------------------------------------------------------------------------------------------ bookkeeping
Redstone::Redstone(Level& l, World& world) : lv(l), w(world) {}

void Redstone::tick(World&, double dt) {
  if (lv.isRemote()) return;
  acc += dt;
  int n = 0;
  while (acc >= 0.05 && n < 4) { acc -= 0.05; step(); n++; }
  if (n == 4) acc = 0;
}
void Redstone::queue(int x, int y, int z) { if (pendingSet.insert(posKey(x, y, z)).second) pending.push_back({x, y, z}); }
void Redstone::schedule(int x, int y, int z, int delay, const char* kind, bool want) {
  u64 k = posKey(x, y, z);
  auto it = sched.find(k);
  if (it != sched.end() && it->second.kind == kind) return;
  Sched s{x, y, z, now + delay, kind, want, it != sched.end() ? it->second.seq : ++seqN, ++uidN};
  sched[k] = s;
}
void Redstone::observe(int x, int y, int z) {
  u64 k = posKey(x, y, z);
  if (sched.count(k)) return;
  sched[k] = Sched{x, y, z, now + 2, "observer", true, ++seqN, ++uidN};
}

void Redstone::onBlockChanged(World&, int x, int y, int z, BlockId oldId, BlockId newId) {
  if (lv.isRemote()) return;
  const Tables& t = T();
  const Registry& R = reg();
  if (t.role[newId]) queue(x, y, z);
  if (t.ticker[newId]) noteTicker(x, y, z);
  // a pressed button lets go by itself, lit redstone ore fades (also when the press came over the network)
  if (t.role[newId] == BUTTON && (w.getMeta(x, y, z) & 8)) schedule(x, y, z, R.block(newId).has("wooden") ? 30 : 20, "button");
  if (newId == t.oreLit && oldId != newId) schedule(x, y, z, 100 + (int)(rnd() * 300), "ore");
  for (int d = 0; d < 6; d++) {
    int nx = x + D6X[d], ny = y + D6Y[d], nz = z + D6Z[d];
    BlockId id = w.getBlock(nx, ny, nz);
    if (t.role[id]) {
      queue(nx, ny, nz);
      if (t.role[id] == OBSERVER && (w.getMeta(nx, ny, nz) & 7) == OPP6[d]) observe(nx, ny, nz);
    }
    if (t.cond[id]) for (int e = 0; e < 6; e++) {
      int mx = nx + D6X[e], my = ny + D6Y[e], mz = nz + D6Z[e];
      if (t.role[w.getBlock(mx, my, mz)]) queue(mx, my, mz);
    }
  }
  // (game.js setBlock) levers, buttons and hooks lose what they were fixed to; pistons lose their other half
  if (!R.tSolid[newId] || R.tShape[newId]) dropAttached(x, y, z);
  const BlockDef& ob = R.block(oldId);
  if (!pistonGuard && oldId != newId && (ob.has("pistonHead") || ob.has("extended"))) pistonPartBroken(x, y, z, oldId);
}

void Redstone::step() {
  if (lv.isRemote()) return;
  now++;
  entsBuilt = false;
  if (!sched.empty()) {
    std::vector<Sched> due;
    for (auto& [k, s] : sched) if (s.t <= now) due.push_back(s);
    std::sort(due.begin(), due.end(), [](const Sched& a, const Sched& b) { return a.t != b.t ? a.t < b.t : a.seq < b.seq; });
    for (auto& s : due) {
      u64 k = posKey(s.x, s.y, s.z);
      auto it = sched.find(k);
      if (it != sched.end() && it->second.uid == s.uid) sched.erase(it);
      fire(s);
    }
  }
  runTickers();
  for (int round = 0; round < 16 && !pending.empty(); round++) {
    std::vector<ivec3> list; list.swap(pending);
    pendingSet.clear(); netDone.clear();
    for (auto& p : list) update(p.x, p.y, p.z);
  }
}

// ------------------------------------------------------------------------------------------ reading power
void Redstone::dustArms(int x, int y, int z, bool arms[4]) {
  int c[4]; wireConnects(w, x, y, z, c);
  int n = 0;
  for (int h = 0; h < 4; h++) { arms[h] = c[h] > 0; n += arms[h]; }
  if (n == 0) for (int h = 0; h < 4; h++) arms[h] = true;
  else if (n == 1) for (int h = 0; h < 4; h++) if (c[h]) arms[(h + 2) & 3] = true;
}
int Redstone::emit(int x, int y, int z, BlockId id, int m, int d) {
  switch (T().role[id]) {
    case BLOCK: return 15;
    case LEVER: case BUTTON: return m & 8 ? 15 : 0;
    case PLATE: case DAYLIGHT: case TARGET: return m & 15;
    case TORCH: return d == TORCH_SUP[(m & 7) % 5] ? 0 : 15;
    case REPEATER: return (m & 16) && d == H2D6[m & 3] ? 15 : 0;
    case COMPARATOR: return d == H2D6[m & 3] ? (m >> 4) & 15 : 0;
    case OBSERVER: return (m & 8) && d == OPP6[(m & 7) % 6] ? 15 : 0;
    case DRAIL: case HOOK: return m & 8 ? 15 : 0;
    case WIRE: {
      int p = m & 15;
      if (!p || d == 1) return 0;
      if (d == 0) return p;
      bool arms[4]; dustArms(x, y, z, arms);
      return arms[D62H[d]] ? p : 0;
    }
  }
  return 0;
}
int Redstone::strong(int x, int y, int z, BlockId id, int m, int d) {
  switch (T().role[id]) {
    case LEVER: case BUTTON: return (m & 8) && d == leverSup(m) ? 15 : 0;
    case PLATE: return d == 0 ? m & 15 : 0;
    case TORCH: return d == 1 && TORCH_SUP[(m & 7) % 5] != 1 ? 15 : 0;
    case REPEATER: case COMPARATOR: case OBSERVER: return emit(x, y, z, id, m, d);
    case DRAIL: return d == 0 && (m & 8) ? 15 : 0;
    case HOOK: return (m & 8) && d == hookSup(m) ? 15 : 0;
  }
  return 0;
}
int Redstone::blockPower(int x, int y, int z, bool strongOnly) {
  const Tables& t = T();
  int best = 0;
  for (int e = 0; e < 6; e++) {
    int nx = x + D6X[e], ny = y + D6Y[e], nz = z + D6Z[e];
    BlockId id = w.getBlock(nx, ny, nz);
    u8 r = t.role[id]; if (!r) continue;
    int m = w.getMeta(nx, ny, nz), d = OPP6[e];
    int v = strong(nx, ny, nz, id, m, d);
    if (!strongOnly && r == WIRE) v = std::max(v, emit(nx, ny, nz, id, m, d));
    if (v > best) { best = v; if (best >= 15) return 15; }
  }
  return best;
}
int Redstone::inputFrom(int x, int y, int z, int d) {
  const Tables& t = T();
  int nx = x + D6X[d], ny = y + D6Y[d], nz = z + D6Z[d];
  BlockId id = w.getBlock(nx, ny, nz);
  int v = 0;
  if (t.role[id]) v = emit(nx, ny, nz, id, w.getMeta(nx, ny, nz), OPP6[d]);
  if (t.cond[id] && v < 15) v = std::max(v, blockPower(nx, ny, nz, false));
  return v;
}
int Redstone::powerAt(int x, int y, int z, int skip) {
  int best = 0;
  for (int d = 0; d < 6; d++) { if (d == skip) continue; int v = inputFrom(x, y, z, d); if (v > best) { best = v; if (best >= 15) break; } }
  return best;
}
bool Redstone::supportPowered(int x, int y, int z) {
  BlockId id = w.getBlock(x, y, z);
  if (T().role[id] == BLOCK) return true;
  return T().cond[id] ? blockPower(x, y, z, false) > 0 : false;
}

// ------------------------------------------------------------------------------------------ reacting
void Redstone::update(int x, int y, int z) {
  const Tables& t = T();
  const Registry& R = reg();
  BlockId id = w.getBlock(x, y, z);
  u8 r = t.role[id];
  if (!r) return;
  int m = w.getMeta(x, y, z);
  vec3 c = vec3(x, y, z) + vec3(0.5f);
  switch (r) {
    case WIRE: updateWire(x, y, z); break;
    case TORCH: case TORCH_OFF: {
      int s = TORCH_SUP[(m & 7) % 5];
      bool on = !supportPowered(x + D6X[s], y + D6Y[s], z + D6Z[s]);
      if ((r == TORCH) != on) schedule(x, y, z, 2, "torch");
      break;
    }
    case REPEATER: {
      bool locked = repeaterLocked(x, y, z, m);
      if (locked != !!(m & 32)) { w.setBlock(x, y, z, id, locked ? m | 32 : m & ~32); return; }
      if (locked) return;
      bool on = inputFrom(x, y, z, OPP6[H2D6[m & 3]]) > 0;
      if (on != !!(m & 16)) schedule(x, y, z, 2 * (1 + ((m >> 2) & 3)), "repeater", on);
      break;
    }
    case COMPARATOR:
      if (comparatorValue(x, y, z, m) != ((m >> 4) & 15)) schedule(x, y, z, 2, "comparator");
      break;
    case LAMP: {
      bool on = powerAt(x, y, z) > 0;
      if (on && id == t.lamp) { unschedule(x, y, z); w.setBlock(x, y, z, t.lampOn, m); }
      else if (!on && id == t.lampOn) schedule(x, y, z, 4, "lamp");
      break;
    }
    case BULB: {
      bool on = powerAt(x, y, z) > 0, was = !!(m & 0x20);
      if (on && !was) { w.setBlock(x, y, z, id == t.bulb ? t.bulbLit : t.bulb, m | 0x20); lv.fx->sound("click", c, 16); }
      else if (!on && was) w.setBlock(x, y, z, id, m & ~0x20);
      break;
    }
    case PISTON: {
      bool ext = R.block(id).has("extended");
      bool on = powerAt(x, y, z, m & 7) > 0;
      if (on != ext) schedule(x, y, z, 1, "piston");
      break;
    }
    case DISPENSER: case DROPPER: {
      bool on = powerAt(x, y, z) > 0, trig = !!(m & 8);
      if (on && !trig) { w.setBlock(x, y, z, id, m | 8); schedule(x, y, z, 4, "dispense"); }
      else if (!on && trig) w.setBlock(x, y, z, id, m & ~8);
      break;
    }
    case HOPPER: {
      bool on = powerAt(x, y, z) > 0;
      if (on != !!(m & 8)) w.setBlock(x, y, z, id, on ? m | 8 : m & ~8);
      break;
    }
    case NOTE: case BELL: {
      bool on = powerAt(x, y, z) > 0, was = !!(m & 0x20);
      if (on && !was) {
        w.setBlock(x, y, z, id, m | 0x20);
        if (r == NOTE) {
          int n = m & 31;
          // sfx.note(n, under): the instrument comes from the block underneath (its sound material)
          lv.fx->sound("note:" + R.block(w.getBlock(x, y - 1, z)).sound, c, 48, 1, std::pow(2.f, n / 12.f));
          lv.fx->particles("note", c + vec3(0, 0.7f, 0), 1, json{{"n", n}});
        } else lv.fx->sound("bell", c, 48);
      } else if (!on && was) w.setBlock(x, y, z, id, m & ~0x20);
      break;
    }
    case TNT: if (powerAt(x, y, z) > 0) { w.setBlock(x, y, z, AIR, 0); primeTnt(x, y, z, 4); } break;
    case DOOR: {
      int lower = m & 16 ? y - 1 : y;
      if (w.getBlock(x, lower, z) != id || w.getBlock(x, lower + 1, z) != id) break;
      int ml = w.getMeta(x, lower, z);
      bool on = powerAt(x, lower, z) > 0 || powerAt(x, lower + 1, z) > 0;
      if (on != !!(ml & 0x20)) {
        int nm = (ml & ~(4 | 0x20 | 16)) | (on ? 4 | 0x20 : 0);
        w.setBlock(x, lower, z, id, nm); w.setBlock(x, lower + 1, z, id, nm | 16);
        if (!!(ml & 4) != on) lv.fx->sound(doorSound(R.block(id), on), c);
      }
      break;
    }
    case TRAPDOOR: case GATE: {
      bool on = powerAt(x, y, z) > 0;
      if (on != !!(m & 0x20)) {
        w.setBlock(x, y, z, id, (m & ~(4 | 0x20)) | (on ? 4 | 0x20 : 0));
        if (!!(m & 4) != on) lv.fx->sound(doorSound(R.block(id), on), c);
      }
      break;
    }
    case PRAIL: {
      bool on = railPowered(x, y, z, id, m);
      if (on != !!(m & 8)) w.setBlock(x, y, z, id, on ? m | 8 : m & ~8);
      break;
    }
  }
}

void Redstone::fire(const Sched& s) {
  const Tables& t = T();
  int x = s.x, y = s.y, z = s.z;
  BlockId id = w.getBlock(x, y, z);
  int m = w.getMeta(x, y, z);
  u8 r = t.role[id];
  vec3 c = vec3(x, y, z) + vec3(0.5f);
  const std::string& k = s.kind;
  if (k == "torch") {
    if (r != TORCH && r != TORCH_OFF) return;
    int sup = TORCH_SUP[(m & 7) % 5];
    bool on = !supportPowered(x + D6X[sup], y + D6Y[sup], z + D6Z[sup]);
    u64 key = posKey(x, y, z);
    if (on && r == TORCH_OFF) {
      // burnt out: stays dark for a while
      auto it = burn.find(key);
      if (it != burn.end() && it->second.size() >= 8 && now - it->second[it->second.size() - 8] < 60) { schedule(x, y, z, 160, "torch"); return; }
      w.setBlock(x, y, z, t.torch, m);
    } else if (!on && r == TORCH) {
      auto& hist = burn[key];
      hist.push_back(now); while (hist.size() > 10) hist.pop_front();
      w.setBlock(x, y, z, t.torchOff, m);
      if (hist.size() >= 8 && now - hist[hist.size() - 8] < 60) {
        lv.fx->sound("fizz", c, 16);
        lv.fx->particles("burst", vec3(x + 0.5f, y + 0.7f, z + 0.5f), 5, json{{"color", "white"}, {"speed", 0.3}, {"size", 0.1}});
      }
    }
  } else if (k == "repeater") {
    if (r != REPEATER || (m & 32)) return;
    int nm = s.want ? m | 16 : m & ~16;
    if (nm != m) w.setBlock(x, y, z, id, nm);
    queue(x, y, z);
  } else if (k == "comparator") {
    if (r != COMPARATOR) return;
    int v = comparatorValue(x, y, z, m);
    int nm = (m & 7) | (v > 0 ? 8 : 0) | (v << 4);
    if (nm != m) w.setBlock(x, y, z, id, nm);
  } else if (k == "lamp") {
    if (id == t.lampOn && powerAt(x, y, z) == 0) w.setBlock(x, y, z, t.lamp, m);
  } else if (k == "observer") {
    if (r != OBSERVER) return;
    if (s.want) { w.setBlock(x, y, z, id, m | 8); sched[posKey(x, y, z)] = Sched{x, y, z, now + 2, "observer", false, ++seqN, ++uidN}; }
    else w.setBlock(x, y, z, id, m & ~8);
  } else if (k == "button") {
    if (r == BUTTON && (m & 8)) { w.setBlock(x, y, z, id, m & ~8); lv.fx->sound("click_off", c, 16); }
  } else if (k == "target") {
    if (r == TARGET) w.setBlock(x, y, z, id, m & ~15);
  } else if (k == "piston") {
    if (r == PISTON) pistonMove(x, y, z);
  } else if (k == "dispense") {
    if (r == DISPENSER || r == DROPPER) dispense(x, y, z);
  } else if (k == "ore") {
    // redstone ore glows for a while after it's touched, then fades (the original faded on random ticks)
    if (id == t.oreLit) w.setBlock(x, y, z, t.ore, 0);
  }
}

// ------------------------------------------------------------------------------------------ dust
void Redstone::wireLinks(int x, int y, int z, std::vector<ivec3>& out) {
  const Tables& t = T();
  out.clear();
  bool upFree = !t.cond[w.getBlock(x, y + 1, z)];
  for (int h = 0; h < 4; h++) {
    int ax = x + DIRX[h], az = z + DIRZ[h];
    BlockId side = w.getBlock(ax, y, az);
    if (t.role[side] == WIRE) out.push_back({ax, y, az});
    else {
      if (upFree && t.role[w.getBlock(ax, y + 1, az)] == WIRE) out.push_back({ax, y + 1, az});
      if (!t.cond[side] && t.role[w.getBlock(ax, y - 1, az)] == WIRE) out.push_back({ax, y - 1, az});
    }
  }
}
int Redstone::dustInput(int x, int y, int z) {
  const Tables& t = T();
  int best = 0;
  for (int d = 0; d < 6; d++) {
    int nx = x + D6X[d], ny = y + D6Y[d], nz = z + D6Z[d];
    BlockId id = w.getBlock(nx, ny, nz);
    u8 r = t.role[id];
    if (r == WIRE) continue;
    int v = 0;
    if (r) v = emit(nx, ny, nz, id, w.getMeta(nx, ny, nz), OPP6[d]);
    if (t.cond[id] && v < 15) v = std::max(v, blockPower(nx, ny, nz, true));
    if (v > best) { best = v; if (best >= 15) break; }
  }
  return best;
}
// Work out the whole connected stretch of dust at once and set every cell.
void Redstone::updateWire(int x, int y, int z) {
  u64 k0 = posKey(x, y, z);
  if (netDone.count(k0)) return;
  std::vector<ivec3> cells{{x, y, z}};
  std::unordered_map<u64, int> index{{k0, 0}};
  std::vector<std::vector<int>> links;
  std::vector<ivec3> tmp;
  for (size_t i = 0; i < cells.size() && cells.size() < 4096; i++) {
    ivec3 c = cells[i];
    wireLinks(c.x, c.y, c.z, tmp);
    std::vector<int> ln;
    for (auto& q : tmp) {
      u64 k = posKey(q.x, q.y, q.z);
      auto it = index.find(k);
      int n;
      if (it == index.end()) { n = (int)cells.size(); index[k] = n; cells.push_back(q); } else n = it->second;
      ln.push_back(n);
    }
    if (links.size() <= i) links.resize(i + 1);
    links[i] = std::move(ln);
  }
  size_t N = cells.size();
  links.resize(N);
  std::vector<int> power(N, 0);
  std::vector<std::vector<int>> buckets(16);
  for (size_t i = 0; i < N; i++) {
    netDone.insert(posKey(cells[i].x, cells[i].y, cells[i].z));
    int v = dustInput(cells[i].x, cells[i].y, cells[i].z);
    power[i] = v; if (v > 0) buckets[v].push_back((int)i);
  }
  for (int v = 15; v > 1; v--)
    for (size_t bi = 0; bi < buckets[v].size(); bi++) {
      int i = buckets[v][bi];
      if (power[i] != v) continue;
      for (int n : links[i]) if (power[n] < v - 1) { power[n] = v - 1; buckets[v - 1].push_back(n); }
    }
  BlockId wire = T().wire;
  for (size_t i = 0; i < N; i++) {
    ivec3 c = cells[i];
    int m = w.getMeta(c.x, c.y, c.z);
    if ((m & 15) != power[i]) w.setBlock(c.x, c.y, c.z, wire, (m & ~15) | power[i]);
  }
}

// ------------------------------------------------------------------------------------------ repeaters & comparators
bool Redstone::repeaterLocked(int x, int y, int z, int m) {
  const Tables& t = T();
  int f = m & 3;
  for (int s : {H2D6[(f + 1) & 3], H2D6[(f + 3) & 3]}) {
    int nx = x + D6X[s], nz = z + D6Z[s];
    BlockId id = w.getBlock(nx, y, nz);
    u8 r = t.role[id];
    if (r != REPEATER && r != COMPARATOR) continue;
    if (emit(nx, y, nz, id, w.getMeta(nx, y, nz), OPP6[s]) > 0) return true;
  }
  return false;
}
int Redstone::sideInput(int x, int y, int z, int d) {
  int nx = x + D6X[d], nz = z + D6Z[d];
  BlockId id = w.getBlock(nx, y, nz);
  int m = w.getMeta(nx, y, nz);
  switch (T().role[id]) {
    case WIRE: return m & 15;
    case BLOCK: return 15;
    case REPEATER: case COMPARATOR: return emit(nx, y, nz, id, m, OPP6[d]);
  }
  return 0;
}
int Redstone::comparatorValue(int x, int y, int z, int m) {
  int f = m & 3, back = OPP6[H2D6[f]];
  int rear = inputFrom(x, y, z, back);
  int bx = x + D6X[back], by = y, bz = z + D6Z[back];
  int cv = measure(bx, by, bz);
  if (cv >= 0) rear = cv;
  else if (T().cond[w.getBlock(bx, by, bz)] && rear < 15) {
    int cv2 = measure(bx + D6X[back], by, bz + D6Z[back]);
    if (cv2 >= 0) rear = std::max(rear, cv2);
  }
  int side = std::max(sideInput(x, y, z, H2D6[(f + 1) & 3]), sideInput(x, y, z, H2D6[(f + 3) & 3]));
  if (m & 4) return std::max(0, rear - side);
  return rear >= side ? rear : 0;
}
// What a comparator reads from a block: -1 when there's nothing to read.
int Redstone::measure(int x, int y, int z) {
  const Registry& R = reg();
  BlockId id = w.getBlock(x, y, z);
  const BlockDef& b = R.block(id);
  int m = w.getMeta(x, y, z);
  if (b.has("container") || b.has("furnace")) { Container c = inv(x, y, z); return c.items ? fillSignal(*c.items) : 0; }
  if (b.has("composter")) return std::min(8, m & 15);
  if (R.tShape[id] == ShapeKind::Cake) return 14 - 2 * (m & 7);
  if (b.has("cauldron")) return m & 3;
  if (b.has("jukebox")) { json* e = w.blockEntity(x, y, z); return e && e->contains("disc") && !(*e)["disc"].is_null() ? 15 : 0; }
  if (b.has("hive")) return ((m >> 2) & 3) * 5;
  if (b.has("frame")) { json* e = w.blockEntity(x, y, z); return e && e->contains("item") && !(*e)["item"].is_null() ? 1 + (e->value("rot", 0) & 7) : 0; }
  if (T().role[id] == DRAIL) { Minecart* c = cartAt(lv, w.realm, x, y, z, true); return c ? fillSignal(c->data["items"]) : -1; }
  return -1;
}

// ------------------------------------------------------------------------------------------ powered and activator rails
// Powered directly, or joined (along the track, through rails of the same kind) to one that is, within 8.
bool Redstone::railPowered(int x, int y, int z, BlockId id, int m) {
  if (powerAt(x, y, z) > 0) return true;
  auto step = [&](int cx, int cy, int cz, const int* e, ivec3& out) {
    int nx = cx + e[0], nz = cz + e[2];
    if (e[1]) { if (w.getBlock(nx, cy + 1, nz) == id) { out = {nx, cy + 1, nz}; return true; } return false; }
    for (int ny : {cy, cy - 1}) if (w.getBlock(nx, ny, nz) == id) { out = {nx, ny, nz}; return true; }
    return false;
  };
  for (int end = 0; end < 2; end++) {
    int cx = x, cy = y, cz = z, cm = m;
    bool hasPrev = false; ivec3 prev(0);
    const int* ex = RAIL_EXITS[railShape(id, cm)][end];
    for (int i = 0; i < 8; i++) {
      ivec3 n;
      if (!step(cx, cy, cz, ex, n)) break;
      if (hasPrev && n == prev) break;
      if (powerAt(n.x, n.y, n.z) > 0) return true;
      prev = {cx, cy, cz}; hasPrev = true; cx = n.x; cy = n.y; cz = n.z; cm = w.getMeta(cx, cy, cz);
      const auto& ends = RAIL_EXITS[railShape(id, cm)];
      bool back0 = cx + ends[0][0] == prev.x && cz + ends[0][2] == prev.z;
      ex = back0 ? ends[1] : ends[0];
    }
  }
  return false;
}

// ------------------------------------------------------------------------------------------ timers
void Redstone::scanTickers(Chunk& c) {
  auto& set = tickers[c.key()];
  set.clear();
  const Tables& t = T();
  for (int sy = 0; sy < SECS; sy++) {
    const Section* s = c.section(sy);
    if (!s) continue;
    for (int i = 0; i < 4096; i++) if (t.ticker[s->blocks[i]]) set.insert(((sy * 16 + (i >> 8)) << 8) | (i & 255));
  }
}
void Redstone::onChunkLoaded(World&, Chunk& c) { if (!lv.isRemote()) scanTickers(c); }
void Redstone::onChunkUnloading(World&, Chunk& c) { tickers.erase(c.key()); }
void Redstone::noteTicker(int x, int y, int z) {
  Chunk* c = w.chunkAtBlock(x, y, z);
  if (!c) return;
  auto it = tickers.find(c->key());
  if (it == tickers.end()) { scanTickers(*c); it = tickers.find(c->key()); }
  it->second.insert(((y & 255) << 8) | ((z & 15) << 4) | (x & 15));
}
void Redstone::buildEnts() {
  if (entsBuilt) return;
  entsBuilt = true; ents.clear();
  const Registry& R = reg();
  for (auto& e : lv.entities()) {
    if (e->removed || e->realm != w.realm) continue;
    int kind = -1;
    if (e->isPlayer()) { if (!static_cast<Player*>(e.get())->dead) kind = 0; }
    else if (e->type == "item" || e->type == "arrow") kind = 1;
    else if (e->type == "minecart") kind = 2;
    else if (R.mobs.is_object() && R.mobs.contains(e->type) && e->hp > 0) kind = 0;
    if (kind < 0) continue;
    float hw = e->isPlayer() ? 0.3f : e->w, h = e->isPlayer() ? 1.8f : (e->h > 0 ? e->h : 0.5f);
    ents.push_back({e->pos - vec3(hw, 0, hw), e->pos + vec3(hw, h, hw), kind});
  }
}
int Redstone::countIn(vec3 lo, vec3 hi, int only) {
  buildEnts();
  int n = 0;
  for (auto& b : ents) if (b.hi.x > lo.x && b.lo.x < hi.x && b.hi.y > lo.y && b.lo.y < hi.y && b.hi.z > lo.z && b.lo.z < hi.z && (only < 0 || b.kind == only)) n++;
  return n;
}
void Redstone::runTickers() {
  if (now & 1) return;
  std::vector<ivec2> centers;
  for (auto& e : lv.entities()) if (!e->removed && e->isPlayer() && e->realm == w.realm) centers.push_back({ifloor(e->pos.x) >> 4, ifloor(e->pos.z) >> 4});
  if (centers.empty()) return;
  const Tables& t = T();
  std::vector<std::array<int, 6>> work;   // x, y, z, id, meta, kind
  for (auto& [key, set] : tickers) {
    if (set.empty()) continue;
    bool near = false;
    for (auto& p : centers) if (std::abs(key.cx - p.x) <= 6 && std::abs(key.cz - p.y) <= 6) { near = true; break; }
    if (!near) continue;
    Chunk* c = w.chunk(key.dim, key.cx, key.cz);
    if (!c) continue;
    for (auto it = set.begin(); it != set.end();) {
      int i = *it, ly = i >> 8, lz = (i >> 4) & 15, lx = i & 15;
      BlockId id = c->get(lx, ly, lz);
      int kind = t.ticker[id];
      if (!kind) { it = set.erase(it); continue; }
      work.push_back({c->worldX() + lx, c->baseY() + ly, c->worldZ() + lz, id, c->getMeta(lx, ly, lz), kind});
      ++it;
    }
  }
  for (auto& a : work) {
    int x = a[0], y = a[1], z = a[2], m = a[4], kind = a[5];
    BlockId id = (BlockId)a[3];
    if (w.getBlock(x, y, z) != id) continue;
    m = w.getMeta(x, y, z);
    if (kind == 1) { if (now % 8 == 0) hopperTick(x, y, z, id, m); continue; }
    if (kind == 2) { if (now % 20 == 0) daylightTick(x, y, z, id, m); continue; }
    if (kind == 3) plateTick(x, y, z, id, m);
    else if (kind == 4) hookTick(x, y, z, id, m);
    else if (kind == 5) detectorTick(x, y, z, id, m);
  }
}
void Redstone::plateTick(int x, int y, int z, BlockId id, int m) {
  std::string kind = reg().block(id).str("plate");
  int n = countIn(vec3(x + 1 / 16.f, y, z + 1 / 16.f), vec3(x + 15 / 16.f, y + 0.3f, z + 15 / 16.f), kind == "mobs" ? 0 : -1);
  int p = 0;
  if (n > 0) p = kind == "light" ? std::min(15, n) : kind == "heavy" ? std::min(15, (n + 9) / 10) : 15;
  u64 k = posKey(x, y, z);
  int cur = m & 15;
  if (p > 0) plateSeen[k] = now;
  // pressing takes effect at once; letting go waits half a second
  auto it = plateSeen.find(k);
  i64 seen = it == plateSeen.end() ? 0 : it->second;
  if (p > cur || (p < cur && now - seen >= 10)) {
    w.setBlock(x, y, z, id, (m & ~15) | p);
    lv.fx->sound(p > cur ? "click" : "click_off", vec3(x, y, z) + vec3(0.5f), 16);
  }
}
void Redstone::hookTick(int x, int y, int z, BlockId id, int m) {
  const Tables& t = T();
  int f = m & 3, h = (f + 2) & 3, dx = DIRX[h], dz = DIRZ[h];
  bool strung = false, tripped = false;
  std::vector<ivec2> wires;
  for (int i = 1; i <= 41; i++) {
    int cx = x + dx * i, cz = z + dz * i;
    BlockId cid = w.getBlock(cx, y, cz);
    if (reg().block(cid).has("tripwire")) { wires.push_back({cx, cz}); continue; }
    if (t.role[cid] == HOOK && (w.getMeta(cx, y, cz) & 3) == h && !wires.empty()) strung = true;
    break;
  }
  if (strung) for (auto& c : wires) if (countIn(vec3(c.x, y, c.y), vec3(c.x + 1, y + 0.5f, c.y + 1), -1)) { tripped = true; break; }
  int nm = (m & ~12) | (strung ? 4 : 0) | (tripped ? 8 : 0);
  if (nm != m) {
    w.setBlock(x, y, z, id, nm);
    if ((nm & 8) != (m & 8)) lv.fx->sound(tripped ? "click" : "click_off", vec3(x, y, z) + vec3(0.5f), 16);
    // the wire sags when it's tied off
    for (auto& c : wires) {
      int wm = w.getMeta(c.x, y, c.y), want = (wm & ~3) | (strung ? 2 : 0) | (tripped ? 1 : 0);
      if (wm != want) w.setBlock(c.x, y, c.y, t.tripwire, want);
    }
  }
}
void Redstone::detectorTick(int x, int y, int z, BlockId id, int m) {
  int n = countIn(vec3(x + 0.1f, y, z + 0.1f), vec3(x + 0.9f, y + 0.9f, z + 0.9f), 2);
  bool on = n > 0;
  if (on != !!(m & 8)) w.setBlock(x, y, z, id, on ? m | 8 : m & ~8);
  else if (on) queueAround(x, y, z);   // comparators reading a cart's chest
}
void Redstone::queueAround(int x, int y, int z) {
  for (int d = 0; d < 6; d++) { int nx = x + D6X[d], ny = y + D6Y[d], nz = z + D6Z[d]; if (T().role[w.getBlock(nx, ny, nz)] == COMPARATOR) queue(nx, ny, nz); }
}
void Redstone::daylightTick(int x, int y, int z, BlockId id, int m) {
  int p = 0;
  if (w.realm == REALM_MAIN && dimOfY(y) == DIM_OVERWORLD) {
    int sky = w.getLight(x, y + 1, z) >> 4;
    // env.day = 0.2 + 0.8 * smoothstep(-0.2, 0.25, sin(t * TAU)) (game.js); sun = (day - 0.2) / 0.8
    float sun = smoothstepf(-0.2f, 0.25f, std::sin((float)lv.dayFraction() * TAU));
    p = (int)std::round(sky * (m & 16 ? 1 - sun : sun));
  }
  if (p != (m & 15)) w.setBlock(x, y, z, id, (m & ~15) | p);
}

// ------------------------------------------------------------------------------------------ containers & hoppers
Container Redstone::inv(int x, int y, int z) {
  Container c; c.x = x; c.y = y; c.z = z;
  BlockId id = w.getBlock(x, y, z);
  const BlockDef& b = reg().block(id);
  if (b.has("machine")) if (const json* tab = machineTable(b.str("mech"))) {
    json* e = w.blockEntity(x, y, z, true);
    if (!e) return c;
    if (!e->contains("type")) { (*e)["type"] = "machine"; (*e)["progress"] = 0; }
    if (!(*e)["items"].is_array()) (*e)["items"] = json::array({nullptr, nullptr});
    while ((*e)["items"].size() < 2) (*e)["items"].push_back(nullptr);
    c.kind = Container::Machine; c.items = &(*e)["items"]; c.table = tab;
    return c;
  }
  if (b.has("container")) {
    int n = (int)b.num("container", 27);
    json* e = w.blockEntity(x, y, z, true);
    if (!e) return c;
    if (!e->contains("type")) (*e)["type"] = "chest";
    json& it = (*e)["items"];
    if (!it.is_array()) it = json::array();
    while ((int)it.size() < n) it.push_back(nullptr);
    c.kind = Container::Box; c.items = &it;
    return c;
  }
  if (b.has("furnace")) {
    json* e = w.blockEntity(x, y, z, true);
    if (!e) return c;
    if (!e->contains("type")) { (*e)["type"] = "furnace"; (*e)["burn"] = 0; (*e)["burnMax"] = 0; (*e)["cook"] = 0; }
    json& it = (*e)["items"];
    if (!it.is_array()) it = json::array();
    while (it.size() < 3) it.push_back(nullptr);
    c.kind = Container::Furnace; c.items = &it;
    return c;
  }
  if (b.has("composter")) { c.kind = Container::Composter; return c; }
  if (Minecart* cart = cartAt(lv, w.realm, x, y, z, true)) { c.kind = Container::Box; c.items = &cart->data["items"]; c.cart = cart; }
  return c;
}
std::vector<int> Redstone::insertSlots(Container& c, int from, const ItemStack& s) {
  std::vector<int> v;
  if (c.kind == Container::Furnace) {
    if (from == 1) v = {0};
    else if (from != 0) { const ItemDef* d = s.def(); if (d && d->num("fuel") > 0) v = {1}; }
    return v;
  }
  if (c.kind == Container::Machine) { if (from != 0 && c.table && c.table->contains(s.id)) v = {0}; return v; }
  for (int i = 0; i < c.size(); i++) v.push_back(i);
  return v;
}
std::vector<int> Redstone::extractSlots(Container& c, int to) {
  if (c.kind == Container::Furnace) return to == 0 ? std::vector<int>{2} : std::vector<int>{};
  if (c.kind == Container::Machine) return to == 1 ? std::vector<int>{} : std::vector<int>{1};
  std::vector<int> v;
  for (int i = 0; i < c.size(); i++) v.push_back(i);
  return v;
}
bool Redstone::insertOne(Container& c, int from, const ItemStack& s) {
  if (s.empty()) return false;
  if (c.kind == Container::Composter) {
    int m = w.getMeta(c.x, c.y, c.z), lvl = m & 15;
    float ch = compostChance(s.id);
    if (from != 1 || ch <= 0 || lvl >= 7) return false;
    if (rnd() < ch) w.setBlock(c.x, c.y, c.z, w.getBlock(c.x, c.y, c.z), lvl + 1 == 7 ? 8 : lvl + 1);
    return true;
  }
  if (!c.items) return false;
  std::vector<int> slots = insertSlots(c, from, s);
  ItemStack one = s; one.n = 1;
  for (int i : slots) { ItemStack t = c.get(i); if (!t.empty() && (t.canMerge(one) || (t.id == one.id && t.tag == one.tag && t.d == one.d && t.maxStack() > 1)) && t.n < t.maxStack()) { t.n++; c.set(i, t); return true; } }
  for (int i : slots) if (c.get(i).empty()) { c.set(i, one); return true; }
  return false;
}
void Redstone::changedInv(Container& c) {
  if (c.cart || c.kind == Container::Composter) return;
  w.blockEntityChanged(c.x, c.y, c.z);
  queueAround(c.x, c.y, c.z);
}
bool Redstone::moveOne(Container& a, int to, Container& b, int from) {
  if (a.kind == Container::Composter) {
    int m = w.getMeta(a.x, a.y, a.z);
    if (to != 0 || (m & 15) < 8) return false;
    if (!insertOne(b, from, ItemStack("bone_meal", 1))) return false;
    w.setBlock(a.x, a.y, a.z, w.getBlock(a.x, a.y, a.z), 0);
    return true;
  }
  if (!a.items) return false;
  for (int i : extractSlots(a, to)) {
    ItemStack s = a.get(i);
    if (s.empty()) continue;
    if (insertOne(b, from, s)) { s.n--; a.set(i, s); return true; }
  }
  return false;
}
bool Redstone::moveSlot(Container& a, int slot, Container& b, int from) {
  ItemStack s = a.get(slot);
  if (s.empty() || !insertOne(b, from, s)) return false;
  s.n--; a.set(slot, s);
  return true;
}
void Redstone::hopperTick(int x, int y, int z, BlockId, int m) {
  if (m & 8) return;   // locked by redstone
  const Registry& R = reg();
  Container me = inv(x, y, z);
  if (!me || !me.items) return;
  bool changed = false;
  // push out of the spout
  int sp = m & 7, out = sp == 1 ? 0 : sp % 6;
  Container dst = inv(x + D6X[out], y + D6Y[out], z + D6Z[out]);
  bool any = false;
  for (auto& j : *me.items) if (!j.is_null()) { any = true; break; }
  if (dst && any && moveOne(me, out, dst, OPP6[out])) { changed = true; changedInv(dst); }
  // pull from above: a container, or items lying on top
  Container src = inv(x, y + 1, z);
  if (src) { if (moveOne(src, 0, me, 1)) { changed = true; changedInv(src); } }
  else {
    BlockId above = w.getBlock(x, y + 1, z);
    if (!R.tSolid[above] || R.tShape[above]) {
      for (auto& e : lv.entities()) {
        if (e->removed || e->type != "item" || e->realm != w.realm) continue;
        if (e->pos.x < x || e->pos.x > x + 1 || e->pos.z < z || e->pos.z > z + 1 || e->pos.y < y + 0.5f || e->pos.y > y + 2) continue;
        ItemStack st = ItemStack::fromJson(e->data["stack"]);
        if (st.empty()) continue;
        if (insertOne(me, 1, st)) { st.n--; if (st.n <= 0) e->removed = true; else e->data["stack"] = st.toJson(); changed = true; break; }
      }
    }
  }
  if (changed) changedInv(me);
}

// ------------------------------------------------------------------------------------------ dispensers & droppers
void Redstone::spit(int x, int y, int z, int dir, const ItemStack& s) {
  float sp = 3.5f + rnd();
  vec3 p(x + 0.5f + D6X[dir] * 0.7f, y + 0.5f + D6Y[dir] * 0.7f - (dir == 0 ? 0.2f : 0.15f), z + 0.5f + D6Z[dir] * 0.7f);
  vec3 v(D6X[dir] * sp + (rnd() - 0.5f) * 0.6f, D6Y[dir] * sp + (dir > 1 ? 1.2f : 0), D6Z[dir] * sp + (rnd() - 0.5f) * 0.6f);
  dropStack(lv, w.realm, s, p, v, 0.3f);
}
void Redstone::dispense(int x, int y, int z) {
  const Registry& R = reg();
  const Tables& t = T();
  BlockId id = w.getBlock(x, y, z);
  int m = w.getMeta(x, y, z), dir = (m & 7) % 6;
  vec3 c = vec3(x, y, z) + vec3(0.5f);
  Container me = inv(x, y, z);
  if (!me || !me.items) return;
  std::vector<int> full;
  for (int i = 0; i < me.size(); i++) if (!me.get(i).empty()) full.push_back(i);
  if (full.empty()) { lv.fx->sound("dispense_fail", c, 20); return; }
  int slot = full[std::min((int)full.size() - 1, (int)(rnd() * full.size()))];
  ItemStack s = me.get(slot);
  int fx = x + D6X[dir], fy = y + D6Y[dir], fz = z + D6Z[dir];
  vec3 fc = vec3(fx, fy, fz) + vec3(0.5f);
  auto take = [&] { ItemStack q = me.get(slot); q.n--; me.set(slot, q); changedInv(me); };
  auto smoke = [&] { lv.fx->particles("smoke", c + vec3(D6X[dir], D6Y[dir], D6Z[dir]) * 0.6f, 5, json{{"vel", {D6X[dir] * 0.8, D6Y[dir] * 0.8 + 0.2, D6Z[dir] * 0.8}}}); };
  auto one = [&] { ItemStack q = s; q.n = 1; return q; };
  if (t.role[id] == DROPPER) {
    Container dst = inv(fx, fy, fz);
    if (dst) {
      if (moveSlot(me, slot, dst, OPP6[dir])) { changedInv(dst); changedInv(me); lv.fx->sound("dispense", c, 20); }
      else lv.fx->sound("dispense_fail", c, 20);
      return;
    }
    spit(x, y, z, dir, one()); take(); smoke(); lv.fx->sound("dispense", c, 20);
    return;
  }
  const ItemDef* def = s.def();
  BlockId cur = w.getBlock(fx, fy, fz);
  bool freeCell = cur == AIR || (R.tReplaceable[cur] && !R.tLiquid[cur]);
  auto ok = [&] { smoke(); lv.fx->sound("dispense", c, 20); };
  std::string bucket = def ? def->str("bucket") : "";
  if (s.id == "arrow") {
    float sp = 22;
    auto a = createEntity("arrow");
    a->type = "arrow"; a->realm = w.realm; a->w = 0.1f; a->h = 0.1f;
    a->pos = vec3(x + 0.5f + D6X[dir] * 0.7f, y + 0.5f + D6Y[dir] * 0.7f, z + 0.5f + D6Z[dir] * 0.7f);
    a->vel = vec3(D6X[dir] * sp + (rnd() - 0.5f), D6Y[dir] * sp + 2, D6Z[dir] * sp + (rnd() - 0.5f));
    a->data["damage"] = 5; a->data["pickup"] = true;
    lv.spawn(std::move(a));
    take(); lv.fx->sound("bow", c, 24);
    return;
  }
  if (bucket == "water" || bucket == "lava") {
    if (!freeCell) { lv.fx->sound("dispense_fail", c, 20); return; }
    if (bucket == "water" && dimOfY(fy) == DIM_NETHER && w.realm == REALM_MAIN) lv.fx->sound("fizz", fc);
    else w.setBlock(fx, fy, fz, bucket == "water" ? t.water : t.lava, 0);
    me.set(slot, ItemStack("bucket", 1)); changedInv(me); lv.fx->sound("bucket", fc);
    return;
  }
  if (bucket == "empty" && R.tLiquid[cur] && (w.getMeta(fx, fy, fz) & 15) == 0) {
    w.setBlock(fx, fy, fz, AIR, 0);
    ItemStack full2(R.tLiquid[cur] == 1 ? "water_bucket" : "lava_bucket", 1);
    if (s.n == 1) me.set(slot, full2);
    else { take(); if (!insertOne(me, -1, full2)) spit(x, y, z, dir, full2); }
    changedInv(me); lv.fx->sound("bucket", fc);
    return;
  }
  if (s.id == "bone_meal" && cur) {
    // (LocalPlayer.useBonemeal on a dispenser's behalf) crops grow a few stages
    const BlockDef& cb = R.block(cur);
    if (cb.props.contains("crop") && cb.props["crop"].is_object()) {
      int stages = cb.props["crop"].value("stages", 8), cm = w.getMeta(fx, fy, fz);
      if (cm < stages - 1) {
        w.setBlock(fx, fy, fz, cur, std::min(stages - 1, cm + 2 + (int)(rnd() * 3)));
        lv.fx->particles("bonemeal", fc, 8);
        take(); return;
      }
    }
    lv.fx->sound("dispense_fail", c, 20); return;
  }
  if (s.id == "tnt" && freeCell) { take(); primeTnt(fx, fy, fz, 4); ok(); return; }
  if (def && def->str("igniter") == "flint") {
    if (R.block(cur).has("tnt")) { w.setBlock(fx, fy, fz, AIR, 0); primeTnt(fx, fy, fz, 4); }
    else if (cur == AIR && R.tSolid[w.getBlock(fx, fy - 1, fz)]) w.setBlock(fx, fy, fz, t.fire, 0);
    else { lv.fx->sound("dispense_fail", c, 20); return; }
    ItemStack q = me.get(slot); q.d++;
    if (def->hasTool && def->tool.durability > 0 && q.d >= def->tool.durability) q.clear();
    me.set(slot, q);
    lv.fx->sound("ignite", fc); changedInv(me);
    return;
  }
  if (def && def->has("cart") && t.rail[cur]) { if (placeCart(lv, w.realm, def->str("cart"), fx, fy, fz, dir)) { take(); ok(); return; } }
  if (def && def->has("boat") && R.tLiquid[cur] == 1) {
    static const float YAW[6] = {0, 0, 0, PI, PI / 2, -PI / 2};
    if (spawnBoat(lv, w.realm, def->str("boat"), vec3(fx + 0.5f, fy + 0.6f, fz + 0.5f), YAW[dir])) { take(); ok(); return; }
  }
  if (def && def->has("shears")) {
    for (auto& e : lv.entities()) {
      if (e->removed || e->realm != w.realm || !R.mobs.is_object() || !R.mobs.contains(e->type) || e->hp <= 0) continue;
      const json& mt = R.mobs[e->type];
      if (!mt.value("woolly", false) || e->data.value("sheared", false) || e->data.value("baby", false)) continue;
      if (std::fabs(e->pos.x - fx - 0.5f) >= 1 || std::fabs(e->pos.z - fz - 0.5f) >= 1 || std::fabs(e->pos.y - fy) >= 1.5f) continue;
      e->data["sheared"] = true;
      std::string col = e->data.value("color", std::string());
      dropStack(lv, w.realm, ItemStack(col.empty() ? "wool" : col + "_wool", 1 + (int)(rnd() * 3)), e->pos + vec3(0, 1, 0), vec3((rnd() - 0.5f) * 2, 3, (rnd() - 0.5f) * 2));
      lv.fx->sound("shear", e->pos);
      ItemStack q = me.get(slot); q.d++; me.set(slot, q); changedInv(me);
      return;
    }
  }
  // anything else is simply thrown out
  spit(x, y, z, dir, one()); take(); ok();
}

// ------------------------------------------------------------------------------------------ the level side
RedstoneLevel::RedstoneLevel(Level& l) : lv(l) {
  if (lv.events) useHandler = lv.events->on("itemUse", [this](const json& a) { return rsItemUse(lv, a); });
  // netAction [player, "gateLever", [x, y, z]]: a guest pulled a town gate's lever
  if (lv.events) actHandler = lv.events->on("netAction", [this](const json& a) {
    if (!a.is_array() || a.size() < 3 || a[1] != "gateLever" || !a[2].is_array() || a[2].size() < 3) return true;
    Player* p = a[0].is_string() ? playerNamed(lv, a[0].get<std::string>()) : nullptr;
    gateLever(lv, p, lv.world(p ? p->realm : REALM_MAIN), a[2][0].get<int>(), a[2][1].get<int>(), a[2][2].get<int>());
    return false;
  });
}
RedstoneLevel::~RedstoneLevel() { if (lv.events && useHandler) lv.events->off(useHandler); if (lv.events && actHandler) lv.events->off(actHandler); }
Redstone* RedstoneLevel::of(int realm) {
  if (lv.isRemote()) return nullptr;
  auto it = per.find(realm);
  if (it != per.end()) return it->second.get();
  World* w = lv.worldIfOpen(realm);
  if (!w) return nullptr;
  auto r = std::make_shared<Redstone>(lv, *w);
  w->addSystem(r);
  for (auto& [k, c] : w->chunks()) r->scanTickers(*c);
  per[realm] = r;
  return r.get();
}
void RedstoneLevel::tick(Level& l, double) {
  if (l.isRemote()) return;
  for (auto& [realm, w] : l.allRealms()) of(realm);
}
void RedstoneLevel::save(Level&) { for (auto& [r, p] : per) p->finishAnims(); }
Redstone* redstoneOf(Level& lv, int realm) { auto* s = lv.system<RedstoneLevel>(); return s ? s->of(realm) : nullptr; }

} // namespace dq
