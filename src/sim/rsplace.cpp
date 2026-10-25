// sim/rsplace.cpp — placing and using redstone parts, rails, minecarts, boats and mech blocks (game/rsplace.js,
// the vehicle parts of local.js use(), trains.js/ships.js use overrides, build.js mech placement), all through
// the EventBus "itemUse" event. Also registerRedstone() (level systems and entity factories).
#include "sim/redstone.h"
#include "sim/redstone_entities.h"
#include "sim/minecart.h"
#include "sim/vehicles.h"
#include "sim/machines.h"
#include "sim/player.h"
#include "core/init.h"
#include "sim/physics.h"

namespace dq {
using namespace rs;

namespace {
ivec3 faceN(int f) { static const ivec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}; return f >= 0 && f < 6 ? N[f] : ivec3(0); }
vec3 lookVec(const Player& p) { return vec3(-std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch), -std::cos(p.yaw) * std::cos(p.pitch)); }
int lookDir(const Player& p) { return dirOf(-std::sin(p.yaw), -std::cos(p.yaw)); }
// Six-way direction the player is looking: 0 down, 1 up, or N/E/S/W.
int lookDir6(const Player& p) { float dy = lookVec(p).y; if (dy < -0.72f) return 0; if (dy > 0.72f) return 1; return H2D6[lookDir(p)]; }
void consume(Player& p, ItemStack* held) { if (held && !p.creative()) { held->n--; if (held->n <= 0) held->clear(); } }
float blockDist(vec3 eye, vec3 dir, ivec3 b) {
  float t0 = 0, t1 = 1e9f;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(dir[a]) < 1e-6f) { if (eye[a] < b[a] || eye[a] > b[a] + 1) return 1e9f; continue; }
    float ta = (b[a] - eye[a]) / dir[a], tb = (b[a] + 1 - eye[a]) / dir[a];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
    if (t0 > t1) return 1e9f;
  }
  return t0;
}
void screenOpen(Level& lv, Player& p, json opts) {
  lv.fx->soundUi(p.id, "chest");
  if (lv.events) lv.events->emit("screenOpen", json::array({p.name, "container", opts}));
}
} // namespace

// Can dust, plates, rails and repeaters sit on the block at (x,y,z)?
bool rsFloorOK(const World& w, int x, int y, int z, bool sink) {
  const Registry& R = reg();
  BlockId id = w.getBlock(x, y, z);
  int k = R.tShape[id], m = w.getMeta(x, y, z);
  if (!R.tSolid[id]) return false;
  if (!k) return R.block(id).render == Render::Cube || R.block(id).render == Render::Cutout;
  // on a bottom half slab too: it just sits half a block lower
  if (k == ShapeKind::Slab) return (m & 7) == 1 || (m & 7) == 6 || ((m & 7) == 0 && sink);
  if (k == ShapeKind::Stairs) return (m & 4) != 0;
  return R.block(id).key == "hopper" || k == ShapeKind::Scaffold;
}

// Right click on a redstone part. True when it did something (rsplace.js useRedstone).
static bool useRedstone(Level& lv, Player& p, World& w, ivec3 hp, const ItemDef* def) {
  const Tables& t = T();
  BlockId id = w.getBlock(hp.x, hp.y, hp.z);
  const BlockDef& b = reg().block(id);
  int m = w.getMeta(hp.x, hp.y, hp.z);
  vec3 c = vec3(hp) + vec3(0.5f);
  if (id == t.ore && t.oreLit) w.setBlock(hp.x, hp.y, hp.z, t.oreLit, 0);
  // holding a block and sneaking: place against it instead
  if (p.sneaking && def && !def->block.empty()) return false;
  Redstone* rsys = redstoneOf(lv, w.realm);
  switch (t.role[id]) {
    case LEVER:
      w.setBlock(hp.x, hp.y, hp.z, id, m ^ 8);
      lv.fx->sound(m & 8 ? "click_off" : "click", c, 16);
      return true;
    case BUTTON:
      if (!(m & 8)) { w.setBlock(hp.x, hp.y, hp.z, id, m | 8); if (rsys) rsys->schedule(hp.x, hp.y, hp.z, b.has("wooden") ? 30 : 20, "button"); lv.fx->sound("click", c, 16); }
      return true;
    case REPEATER:
      w.setBlock(hp.x, hp.y, hp.z, id, (m & ~12) | (((((m >> 2) & 3) + 1) & 3) << 2));
      lv.fx->sound("tick", c, 12);
      return true;
    case COMPARATOR:
      w.setBlock(hp.x, hp.y, hp.z, id, m ^ 4);
      lv.fx->sound(m & 4 ? "click_off" : "click", c, 12);
      return true;
    case DAYLIGHT:
      w.setBlock(hp.x, hp.y, hp.z, id, m ^ 16);
      if (rsys) rsys->daylightTick(hp.x, hp.y, hp.z, id, m ^ 16);
      lv.fx->sound("click", c, 12);
      return true;
    case HOPPER: case DISPENSER: case DROPPER:
      screenOpen(lv, p, json{{"pos", {hp.x, hp.y, hp.z}}});
      return true;
  }
  if ((b.key == "iron_door" || b.key == "iron_trapdoor") && !p.creative()) { toast(lv, p, "Iron doors open with redstone \xE2\x80\x94 try a button or lever beside it"); return true; }
  return false;
}

// Placement rules (rsplace.js placeRedstone + the mech blocks). 1 placed, 0 refused, -1 the usual rules apply.
static int placeRedstone(Level& lv, Player& p, World& w, ivec3 hit, int face, BlockId bid, ItemStack* held) {
  const Tables& t = T();
  const Registry& R = reg();
  const BlockDef& d = R.block(bid);
  u8 r = t.role[bid];
  bool isMech = t.mech[bid], isRail = t.rail[bid], isTrip = d.has("tripwire");
  if (!r && !isMech && !isRail && !isTrip) return -1;
  ivec3 n = faceN(face);
  BlockId hid = w.getBlock(hit.x, hit.y, hit.z);
  ivec3 pt = R.tReplaceable[hid] && !R.tLiquid[hid] ? hit : hit + n;
  int x = pt.x, y = pt.y, z = pt.z;
  if (!inWorldY(y)) return 0;
  int look = lookDir(p);
  auto floor = [&] { if (rsFloorOK(w, x, y - 1, z, R.tSink[bid])) return true; toast(lv, p, "This needs solid ground under it"); return false; };
  int meta = -1;
  if (isRail) { if (!floor()) return 0; meta = railShapeFor(w, x, y, z, bid, look); }
  else if (isTrip) { if (!floor()) return 0; meta = 0; }
  else if (isMech) meta = 0;
  else switch (r) {
    case WIRE: case PLATE: if (!floor()) return 0; meta = 0; break;
    case REPEATER: case COMPARATOR: if (!floor()) return 0; meta = look; break;
    case LEVER: case BUTTON: {
      meta = n.y == 1 ? 0 : n.y == -1 ? 1 : 2 + dirOf((float)-n.x, (float)-n.z);
      int s = leverSup(meta);
      BlockId sid = w.getBlock(x + D6X[s], y + D6Y[s], z + D6Z[s]);
      if (!R.tSolid[sid] || (R.tShape[sid] && R.tShape[sid] != ShapeKind::Slab && R.tShape[sid] != ShapeKind::Stairs)) { toast(lv, p, "Fix it to a solid block"); return 0; }
      if (meta < 2 && (look & 1)) meta |= 16;
      break;
    }
    case HOOK: {
      if (n.y != 0) { toast(lv, p, "Tripwire hooks go on the side of a block"); return 0; }
      int f = dirOf((float)-n.x, (float)-n.z);
      if (!R.tSolid[w.getBlock(x + DIRX[f], y, z + DIRZ[f])]) return 0;
      meta = f; break;
    }
    case OBSERVER: meta = lookDir6(p); break;
    case PISTON: case DISPENSER: case DROPPER: meta = OPP6[lookDir6(p)]; break;
    case HOPPER: meta = n.y != 0 ? 0 : H2D6[dirOf((float)-n.x, (float)-n.z)]; break;
    case DAYLIGHT: meta = 0; break;
    default: return -1;   // torches, lamps, note blocks, TNT, doors...: the usual rules
  }
  // (LocalPlayer.placeBlock) the cell must be free, and not inside someone
  BlockId cur = w.getBlock(x, y, z);
  if (cur != AIR && !R.tReplaceable[cur]) return 0;
  if (R.tSolid[bid]) {
    AABB cell{vec3(x, y, z), vec3(x + 1, y + 1, z + 1)};
    for (auto& e : lv.entities()) {
      if (e->removed || e->realm != p.realm) continue;
      bool body = e->isPlayer() || (R.mobs.is_object() && R.mobs.contains(e->type));
      if (!body) continue;
      float hw = e->isPlayer() ? 0.3f : e->w, h = e->isPlayer() ? 1.8f : e->h;
      if (cell.overlaps({e->pos - vec3(hw, 0, hw), e->pos + vec3(hw, h, hw)})) return 0;
    }
  }
  if (lv.events && !lv.events->emit("blockPlace", json::array({p.name, x, y, z, d.key}))) return 0;
  w.setBlock(x, y, z, bid, (u8)meta, SetFlags::Urgent);
  consume(p, held);
  lv.fx->material(d.sound, "place", vec3(x, y, z) + vec3(0.5f));
  if (isRail) railRefreshAround(w, x, y, z);
  if (isMech) mechPlaced(lv, &p, w, x, y, z, bid, face);
  return 1;
}

bool rsItemUse(Level& lv, const json& a) {
  if (!a.is_array() || a.size() < 6) return true;
  std::string name = a[0].is_string() ? a[0].get<std::string>() : "", key = a[1].is_string() ? a[1].get<std::string>() : "";
  Player* pp = playerNamed(lv, name);
  if (!pp) return true;
  Player& p = *pp;
  World& w = lv.world(p.realm);
  int face = a[5].is_number() ? a[5].get<int>() : -1;
  ivec3 hit(a[2].is_number() ? a[2].get<int>() : 0, a[3].is_number() ? a[3].get<int>() : 0, a[4].is_number() ? a[4].get<int>() : 0);
  bool hasBlock = face >= 0 && face < 6;
  ItemStack* held = nullptr;
  if (!key.empty()) { if (p.held().id == key) held = &p.held(); else if (!p.offhand.empty() && p.offhand[0].id == key) held = &p.offhand[0]; }
  const ItemDef* def = key.empty() ? nullptr : reg().item(key);
  bool remote = lv.isRemote();
  bool coal = held && (key == "coal" || key == "charcoal" || key == "coal_block");

  if (!remote) {
    VehicleSystem* vs = vehicleSystem(lv);
    // feeding the furnace engine from its own seat (trains.js)
    if (Vehicle* v = ridingOf(lv, p)) {
      if (auto* c = dynamic_cast<Minecart*>(v); c && c->kind == "furnace" && coal) {
        float add = key == "coal_block" ? 1620.f : 180.f;
        if (c->fuel >= 3600 - 1) { toast(lv, p, "The furnace is full"); return false; }
        c->fuel = std::min(3600.f, c->fuel + add);
        consume(p, held);
        lv.fx->sound("ignite", c->pos);
        toast(lv, p, "Coal for " + std::to_string((int)std::round(c->fuel / 60)) + " minutes of driving", "good");
        return false;
      }
      // from your seat in a boat with a chest: open it (ships.js)
      if (auto* b = dynamic_cast<Boat*>(v); b && b->items.is_array() && !held && !p.sneaking) { screenOpen(lv, p, json{{"entity", b->id}, {"name", b->ship() ? "Ship's Chest" : "Boat Chest"}}); return false; }
    }
    // on a ship's deck, beside the wheel: take it (or open the ship's chest)
    if (vs && !held && !p.sneaking && !p.riding) if (auto* s = dynamic_cast<Boat*>(lv.entity(vs->ps(p.id).aboard))) {
      vec2 h = s->toWorld(SHIP.helm.x, SHIP.helm.y);
      if (std::hypot(h.x - p.pos.x, h.y - p.pos.z) < 1.8f) { s->mount(lv, p); return false; }
      if (s->items.is_array()) { vec2 c = s->toWorld(SHIP.cargo.x, SHIP.cargo.y); if (std::hypot(c.x - p.pos.x, c.y - p.pos.z) < 1.8f) { screenOpen(lv, p, json{{"entity", s->id}, {"name", "Ship's Chest"}}); return false; } }
    }
    // a vehicle under the crosshair, nearer than the block (local.js use: boats mount, carts use)
    float vd = 0;
    if (Vehicle* v = vehiclePick(lv, p, 4.5f, &vd)) {
      vec3 eye = p.pos + vec3(0, p.sneaking ? 1.5f : 1.62f, 0);
      float bd = hasBlock ? blockDist(eye, lookVec(p), hit) : 1e9f;
      if (vd <= bd + 0.01f) {
        if (dynamic_cast<Boat*>(v)) { if (!p.sneaking && !p.riding) { v->mount(lv, p); return false; } }
        else if (!p.sneaking && v->use(lv, p, held)) return false;
      }
    }
  }
  // redstone parts under the crosshair
  if (hasBlock && useRedstone(lv, p, w, hit, def)) return false;
  if (!def || !held) return true;
  // boats on water, carts on rails
  if (def->has("boat")) { if (!remote) placeBoatItem(lv, p, *held); return false; }
  if (def->has("cart")) { if (!remote) { if (hasBlock) placeCartItem(lv, p, *held, hit); else toast(lv, p, "Set a minecart on a rail"); } return false; }
  // placing redstone parts, rails and mech blocks
  if (hasBlock && !def->block.empty()) {
    BlockId bid = reg().blockId(def->block);
    if (bid && placeRedstone(lv, p, w, hit, face, bid, held) >= 0) return false;
  }
  return true;
}

void registerRedstone() {
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> {
    auto r = std::make_shared<RedstoneLevel>(lv);
    r->of(REALM_MAIN);
    return r;
  });
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { return std::make_shared<VehicleSystem>(lv); });
  registerLevelSystem([](Level& lv) -> std::shared_ptr<LevelSystem> { auto m = std::make_shared<MechLevel>(lv); m->of(REALM_MAIN); return m; });
  registerEntityFactory("minecart", [](const std::string&) -> std::unique_ptr<Entity> { return std::make_unique<Minecart>(); });
  registerEntityFactory("boat", [](const std::string&) -> std::unique_ptr<Entity> { return std::make_unique<Boat>(); });
  registerEntityFactory("piston_anim", [](const std::string&) -> std::unique_ptr<Entity> { auto a = std::make_unique<PistonAnim>(); a->removed = true; return a; });
  registerEntityFactory("primed_tnt", [](const std::string&) -> std::unique_ptr<Entity> { return std::make_unique<PrimedTnt>(); });
}

} // namespace dq
