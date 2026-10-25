// sim/camping.cpp — campfires, the kit, rope, the grappling hook, the sleeping bag (game/camping.js).
#include "sim/camping.h"
#include "sim/physics.h"

namespace dq {

const vec2 CAMP_SPOTS[4] = {{0.28f, 0.2f}, {0.72f, 0.2f}, {0.72f, 0.8f}, {0.28f, 0.8f}};

namespace {
struct Ids { BlockId campfire = 0, soul = 0, rope = 0, anchor = 0, bag = 0, hay = 0; };
const Ids& ids() {
  static Ids I; static size_t n = 0;
  if (n != reg().blocks.size()) {
    n = reg().blocks.size();
    I.campfire = reg().blockId("campfire"); I.soul = reg().blockId("soul_campfire"); I.rope = reg().blockId("rope");
    I.anchor = reg().blockId("grapple_anchor"); I.bag = reg().blockId("sleeping_bag"); I.hay = reg().blockId("hay_bale");
  }
  return I;
}
bool solidAt(World& w, int x, int y, int z) { return reg().tSolid[w.getBlock(x, y, z)] != 0; }
bool blockDrops(Level& lv) { return !lv.info.rules.is_object() || lv.info.rules.value("blockDrops", true); }
vec3 C(int x, int y, int z) { return vec3(x + 0.5f, y + 0.5f, z + 0.5f); }

// ---- the index of campfires per realm (kept by a hook on each World)
struct CampState;
class CampWorldHook : public WorldSystem {
public:
  explicit CampWorldHook(CellSet* s) : set(s) {}
  const char* name() const override { return "camping"; }
  void onChunkLoaded(World& w, Chunk& c) override {
    for (int sy = 0; sy < SECS; sy++) {
      const Section* s = c.section(sy); if (!s) continue;
      for (int i = 0; i < 4096; i++) if (isCampfire(s->blocks[i])) set->insert(ivec3(c.worldX() + (i & 15), c.baseY() + sy * 16 + (i >> 8), c.worldZ() + ((i >> 4) & 15)));
    }
  }
  void onChunkUnloading(World& w, Chunk& c) override {
    for (auto it = set->begin(); it != set->end();) {
      if (dimOfY(it->y) == c.dim && (it->x >> 4) == c.cx && (it->z >> 4) == c.cz) it = set->erase(it); else ++it;
    }
  }
  void onBlockChanged(World& w, int x, int y, int z, BlockId o, BlockId n) override {
    if (isCampfire(n)) set->insert(ivec3(x, y, z)); else if (isCampfire(o)) set->erase(ivec3(x, y, z));
  }
  CellSet* set;
};
class CampState : public LevelSystem {
public:
  const char* name() const override { return "camping"; }
  std::map<int, std::unique_ptr<CellSet>> fires;
  std::unordered_map<u32, float> burnT;
  CellSet& attach(World& w) {
    auto& s = fires[w.realm];
    if (!s) {
      s = std::make_unique<CellSet>();
      auto hook = std::make_shared<CampWorldHook>(s.get());
      w.addSystem(hook);
      for (auto& [k, c] : w.chunks()) hook->onChunkLoaded(w, *c);
    }
    return *s;
  }
};
CampState& campState(Level& lv) {
  if (auto s = lv.system<CampState>()) return *s;
  static CampState none; return none;   // (registered with every level by registerCampingEntities)
}
} // namespace

bool isCampfire(BlockId id) { const Ids& I = ids(); return id && (id == I.campfire || id == I.soul); }
bool campfireLit(World& w, int x, int y, int z) { return isCampfire(w.getBlock(x, y, z)) && !(w.getMeta(x, y, z) & CAMPFIRE_OUT); }
const CellSet& campfiresIn(Level& lv, int realm) {
  if (World* w = lv.worldIfOpen(realm)) return campState(lv).attach(*w);
  static CellSet none; return none;
}

std::optional<vec3> campFlue(World& w, int x, int y, int z) {
  struct E { double t; std::optional<vec3> f; };
  static std::unordered_map<ivec3, E, IVec3Hash> cache;
  double now = nowSeconds();
  auto it = cache.find(ivec3(x, y, z));
  if (it != cache.end() && now - it->second.t < 4) return it->second.f;
  auto solid = [&](int X, int Y, int Z) { BlockId id = w.getBlock(X, Y, Z); return id && reg().tSolid[id] && !reg().tLiquid[id]; };
  std::optional<vec3> f;
  int bandTop = dimBase(dimOfY(y)) + DIM_H - 1;
  if (solid(x, y + 1, z)) {
    int best = y + 3;
    for (auto [dx, dz] : {std::pair{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
      int X = x + dx, Z = z + dz, Y = dx || dz ? y : y + 1;
      if (!solid(X, Y, Z) && !solid(X, ++Y, Z)) continue;   // a stack beside the fire starts at the fire or just above it
      while (Y < bandTop && solid(X, Y + 1, Z)) Y++;
      if (Y > best && Y + 1 <= bandTop && w.getBlock(X, Y + 1, Z) == AIR) { best = Y; f = vec3((float)X, Y + 1.05f, (float)Z); }
    }
  }
  if (cache.size() > 400) cache.clear();
  cache[ivec3(x, y, z)] = {now, f};
  return f;
}

// ------------------------------------------------------------------ food on the fire
static json& campEntity(World& w, int x, int y, int z) {
  json* e = w.blockEntity(x, y, z, true);
  if (!e->is_object() || !e->contains("type")) *e = {{"type", "campfire"}};
  if (!(*e)["items"].is_array() || (*e)["items"].size() != 4) (*e)["items"] = json::array({nullptr, nullptr, nullptr, nullptr});
  if (!(*e)["t"].is_array() || (*e)["t"].size() != 4) (*e)["t"] = json::array({0, 0, 0, 0});
  return *e;
}
static void dropCampFood(Level& lv, Player* p, World& w, int x, int y, int z, bool give) {
  json* e = w.blockEntity(x, y, z);
  if (!e || !e->is_object() || !(*e)["items"].is_array()) return;
  for (auto& s : (*e)["items"]) {
    ItemStack st = ItemStack::fromJson(s);
    if (st.empty()) continue;
    if (give && p) giveOrDrop(lv, *p, st); else dropItem(lv, w.realm, st, vec3(x + 0.5f, y + 0.6f, z + 0.5f));
    s = nullptr;
  }
  w.blockEntityChanged(x, y, z);
}

// ------------------------------------------------------------------ rope
bool hangRope(UseCtx& u) {
  Level& lv = u.lv; Player& p = u.p; World& w = u.world();
  const Ids& I = ids();
  if (!I.rope) return false;
  BlockId hitId = w.getBlock(u.hit.x, u.hit.y, u.hit.z);
  int x, y, z;
  if (hitId == I.rope) { x = u.hit.x; z = u.hit.z; y = u.hit.y; while (w.getBlock(x, y - 1, z) == I.rope) y--; y--; }
  else { ivec3 t = reg().tReplaceable[hitId] ? u.hit : u.placeAt(); x = t.x; y = t.y; z = t.z; }
  BlockId there = w.getBlock(x, y, z);
  if (there && !isAirOrSoft(there)) return false;
  int ny = u.normal().y;
  // it needs something to hang from (or to stand on)
  if (hitId != I.rope && ny != -1 && !solidAt(w, x, y + 1, z) && !solidAt(w, x, y - 1, z) && ny != 0) return false;
  int n = 0, have = creativeOf(p) ? 64 : p.held().n, floorY = dimBase(dimOfY(y));
  while (n < have && n < 32 && y > floorY && isAirOrSoft(w.getBlock(x, y, z))) {
    w.setBlock(x, y, z, I.rope, 0); n++; y--;
    if (solidAt(w, x, y, z)) break;
  }
  if (!n) return false;
  useUpHeld(p, n);
  sfxAt(lv, "rope_place", vec3(x + 0.5f, y + 1.f, z + 0.5f));
  if (n > 1) toast(lv, p, "The rope hangs " + std::to_string(n) + " blocks down");
  return true;
}

// ------------------------------------------------------------------ the grappling hook
GrappleHook* grappleOf(Level& lv, const Player& p) {
  for (auto& e : lv.entities()) if (!e->removed && e->type == "grappling_hook") if (auto* h = dynamic_cast<GrappleHook*>(e.get())) if (h->owner == p.id) return h;
  return nullptr;
}
void GrappleHook::tick(Level& lv, double dtd) {
  float dt = (float)dtd;
  Player* p = dynamic_cast<Player*>(lv.entity(owner));
  if (!p) { removed = true; return; }
  if (back || glm::distance(pos, p->pos + vec3(0, 1.4f, 0)) > HOOK_RANGE || age > 4 || p->realm != realm) {
    // nothing caught: it comes back to you
    back = true;
    vec3 t = p->pos + vec3(0, 1.2f, 0) - pos; float l = glm::length(t); if (l == 0) l = 1;
    if (l < 1.2f || age > 9 || p->realm != realm) { removed = true; giveOrDrop(lv, *p, ItemStack::fromJson(stack)); return; }
    pos += t / l * 24.f * dt;
    return;
  }
  World& w = lv.world(realm);
  for (int s = 0; s < 4; s++) {
    float h = dt / 4; vel.y -= 12 * h;
    ivec3 o(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z));
    vec3 n = pos + vel * h;
    ivec3 b(ifloor(n.x), ifloor(n.y), ifloor(n.z));
    BlockId id = w.getBlock(b.x, b.y, b.z);
    if (reg().tSolid[id] && !reg().block(id).has("climb")) {
      // which face it struck: the way it came in from
      ivec3 nn = o.x != b.x ? ivec3(o.x > b.x ? 1 : -1, 0, 0) : o.z != b.z ? ivec3(0, 0, o.z > b.z ? 1 : -1) : ivec3(0, o.y < b.y ? -1 : 1, 0);
      sfxAt(lv, "hook_clank", n, 20);
      lv.fx->particles(reg().block(id).key, n, 6, {{"spread", 0.6}});
      if (grappleLodge(lv, *p, stack, b.x, b.y, b.z, nn)) removed = true;
      else { back = true; toast(lv, *p, "The hook found nothing to catch on"); }
      return;
    }
    pos = n;
  }
}
bool grappleLodge(Level& lv, Player& p, const json& stack, int hx, int hy, int hz, ivec3 n) {
  World& w = lv.world(p.realm);
  const Ids& I = ids();
  if (!I.anchor || !I.rope) return false;
  auto air = [&](int x, int y, int z) { return isAirOrSoft(w.getBlock(x, y, z)); };
  ivec3 A; int rx, rz, top, f;
  if (n.y == 1) {
    // on top of a block: over the edge nearest you
    float dx = p.pos.x - (hx + 0.5f), dz = p.pos.z - (hz + 0.5f);
    int sx = std::fabs(dx) >= std::fabs(dz) ? (dx > 0 ? 1 : dx < 0 ? -1 : 0) : 0, sz = sx ? 0 : (dz > 0 ? 1 : dz < 0 ? -1 : 1);
    if (!air(hx, hy + 1, hz) || !air(hx + sx, hy, hz + sz)) return false;
    A = ivec3(hx, hy + 1, hz); rx = hx + sx; rz = hz + sz; top = hy;
    f = sx > 0 ? 3 : sx < 0 ? 1 : sz > 0 ? 0 : 2;   // the claws grip back over the block, away from the drop
  } else if (n.y == -1) {
    if (!air(hx, hy - 1, hz)) return false;
    A = ivec3(hx, hy - 1, hz); rx = hx; rz = hz; top = hy - 2; f = 0;
  } else {
    // a wall: hang from the cell in front of it, sliding up to the lip if that's close
    int ay = hy, ax = hx + n.x, az = hz + n.z;
    if (!air(ax, ay, az)) return false;
    for (int k = 0; k < 3; k++) { if (solidAt(w, hx, ay + 1, hz) && air(ax, ay + 1, az)) ay++; else break; }
    A = ivec3(ax, ay, az); rx = ax; rz = az; top = ay - 1;
    f = n.x > 0 ? 3 : n.x < 0 ? 1 : n.z > 0 ? 0 : 2;   // facing the wall
  }
  w.setBlock(A.x, A.y, A.z, I.anchor, (u8)f);
  int y = top, k = 0, floorY = dimBase(dimOfY(top));
  while (y > floorY && k < 48 && air(rx, y, rz)) { w.setBlock(rx, y, rz, I.rope, HOOK_ROPE); y--; k++; }
  w.setBlockEntity(A.x, A.y, A.z, {{"type", "grapple"}, {"stack", stack}, {"col", {rx, rz, top, y + 1}}});
  sfxAt(lv, "rope_place", vec3(rx + 0.5f, (float)top, rz + 0.5f), 20);
  if (k > 1) toast(lv, p, "The hook catches. " + std::to_string(k) + " blocks of rope drop to the ground", "good");
  return true;
}
static void removeHookRope(World& w, const json& col) {
  if (!col.is_array() || col.size() < 4) return;
  int rx = col[0], rz = col[1], top = col[2], bot = col[3];
  BlockId rope = ids().rope;
  for (int y = top; y >= bot; y--) if (w.getBlock(rx, y, rz) == rope && (w.getMeta(rx, y, rz) & HOOK_ROPE)) w.setBlock(rx, y, rz, AIR, 0);
}
void grappleTake(Level& lv, Player& p, ivec3 a, bool give) {
  World& w = lv.world(p.realm);
  json* e = w.blockEntity(a.x, a.y, a.z);
  json col = e && e->is_object() ? e->value("col", json()) : json();
  ItemStack st = e && e->is_object() && e->contains("stack") ? ItemStack::fromJson((*e)["stack"]) : ItemStack();
  if (st.empty()) st = ItemStack("grappling_hook", 1);
  removeHookRope(w, col);
  if (w.getBlock(a.x, a.y, a.z) == ids().anchor) { w.setBlockEntity(a.x, a.y, a.z, nullptr); w.setBlock(a.x, a.y, a.z, AIR); }
  if (give) giveOrDrop(lv, p, st); else dropItem(lv, p.realm, st, vec3(a.x + 0.5f, a.y + 0.3f, a.z + 0.5f));
  sfxAt(lv, "rope_place", C(a.x, a.y, a.z), 16);
}
std::optional<ivec3> grappleFind(World& w, int x, int y, int z) {
  const Ids& I = ids();
  if (w.getBlock(x, y, z) == I.anchor) return ivec3(x, y, z);
  int yy = y; while (w.getBlock(x, yy + 1, z) == I.rope) yy++;
  static const int D[9][3] = {{0, 1, 0}, {1, 1, 0}, {-1, 1, 0}, {0, 1, 1}, {0, 1, -1}, {1, 2, 0}, {-1, 2, 0}, {0, 2, 1}, {0, 2, -1}};
  for (auto& d : D) if (w.getBlock(x + d[0], yy + d[1], z + d[2]) == I.anchor) return ivec3(x + d[0], yy + d[1], z + d[2]);
  return std::nullopt;
}
static void grappleThrow(Level& lv, Player& p) {
  if (grappleOf(lv, p)) return;
  ItemStack st = p.held();
  st.d += 1;
  p.held().clear();
  const ItemDef* def = st.def();
  if (def && def->hasTool && def->tool.durability > 0 && st.d >= def->tool.durability) { toast(lv, p, "Your grappling hook broke", "warn"); sfxUi(lv, p, "hit"); return; }
  vec3 l = lookDir(p);
  auto h = std::make_unique<GrappleHook>();
  h->owner = p.id; h->realm = p.realm; h->stack = st.toJson();
  h->pos = p.pos + vec3(0, EYE - 0.1f, 0) + l * 0.6f;
  h->vel = l * 30.f + vec3(0, 2, 0);
  lv.spawn(std::move(h));
  sfxUi(lv, p, "hook_throw");
}

// ------------------------------------------------------------------ using things
bool campingUse(UseCtx& u) {
  Level& lv = u.lv; Player& p = u.p; World& w = u.world();
  const Ids& I = ids();
  ItemStack& held = p.held();
  const ItemDef* def = held.empty() ? nullptr : held.def();
  BlockId hitId = u.hasHit() ? w.getBlock(u.hit.x, u.hit.y, u.hit.z) : AIR;
  // using the hook's block or its rope takes it back
  if (u.hasHit() && (hitId == I.anchor || (hitId == I.rope && (w.getMeta(u.hit.x, u.hit.y, u.hit.z) & HOOK_ROPE)))) {
    if (auto a = grappleFind(w, u.hit.x, u.hit.y, u.hit.z)) { grappleTake(lv, p, *a, true); return true; }
  }
  // the grappling hook in hand: throw it
  if (def && def->has("grapple")) { grappleThrow(lv, p); return true; }
  if (u.hasHit() && isCampfire(hitId)) {
    int x = u.hit.x, y = u.hit.y, z = u.hit.z;
    u8 meta = w.getMeta(x, y, z); bool out = meta & CAMPFIRE_OUT;
    std::string tool = def && def->hasTool ? def->tool.type : "";
    // put it out: a shovel or water
    if (!out && (tool == "shovel" || (def && def->str("bucket") == "water"))) {
      w.setBlock(x, y, z, hitId, meta | CAMPFIRE_OUT);
      sfxAt(lv, "fizz", C(x, y, z));
      lv.fx->particles("campfire_smoke", vec3(x + 0.5f, y + 0.4f, z + 0.5f), 14, {{"spread", 0.4}, {"vel", {0, 1.5, 0}}, {"life", 2}, {"size", 0.18}, {"grow", 1.5}, {"rise", true}});
      if (tool == "shovel") wearHeld(lv, p, 1);
      else if (!creativeOf(p)) held = ItemStack("bucket", 1);
      dropCampFood(lv, &p, w, x, y, z, false);
      return true;
    }
    // light it again
    if (out && def && (def->has("igniter") || held.id == "fire_charge")) {
      w.setBlock(x, y, z, hitId, meta & ~CAMPFIRE_OUT);
      sfxAt(lv, "ignite", C(x, y, z));
      if (def->hasTool) wearHeld(lv, p, 1); else if (held.id == "fire_charge") useUpHeld(p);
      return true;
    }
    // pack a kit back up
    if (held.empty() && p.sneaking && (meta & CAMPFIRE_KIT)) {
      dropCampFood(lv, &p, w, x, y, z, true);
      w.setBlockEntity(x, y, z, nullptr);
      w.setBlock(x, y, z, AIR);
      giveOrDrop(lv, p, ItemStack("campfire_kit", 1));
      lv.fx->material("wood", "break", C(x, y, z));
      return true;
    }
    // food on to cook
    auto sm = def ? reg().smelting.find(held.id) : reg().smelting.end();
    const ItemDef* cdef = sm != reg().smelting.end() ? reg().item(sm->second) : nullptr;
    if (!out && cdef && cdef->has("food") && def->has("food")) {
      json& e = campEntity(w, x, y, z);
      int k = -1;
      for (int i = 0; i < 4; i++) if (e["items"][i].is_null()) { k = i; break; }
      if (k < 0) { toast(lv, p, "There's no more room on the fire"); return true; }
      e["items"][k] = ItemStack(held.id, 1).toJson(); e["t"][k] = 0;
      useUpHeld(p);
      w.blockEntityChanged(x, y, z);
      sfxAt(lv, "fizz", vec3(x + 0.5f, y + 0.6f, z + 0.5f), 10);
      return true;
    }
  }
  // rope: hang it down from here
  if (!held.empty() && held.id == "rope" && u.hasHit() && hangRope(u)) return true;
  // the campfire kit sets down a campfire you can pack up again
  if (def && def->has("campKit") && u.hasHit() && u.face == FACE_PY && I.campfire) {
    ivec3 t = reg().tReplaceable[hitId] ? u.hit : u.placeAt();
    AABB body{p.pos - vec3(p.w, 0, p.w), p.pos + vec3(p.w, p.h, p.w)};
    if (w.getBlock(t.x, t.y, t.z) == AIR && solidAt(w, t.x, t.y - 1, t.z) && !body.overlaps({vec3(t), vec3(t) + vec3(1)})) {
      w.setBlock(t.x, t.y, t.z, I.campfire, CAMPFIRE_KIT);
      lv.fx->material("wood", "place", C(t.x, t.y, t.z));
      useUpHeld(p);
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------ breaking
bool campingBreak(Level& lv, Player& p, int x, int y, int z, BlockId id, u8 meta) {
  const Ids& I = ids();
  World& w = lv.world(p.realm);
  bool drops = !creativeOf(p) && blockDrops(lv);
  if (id == I.rope && (meta & HOOK_ROPE)) {
    // a hook's rope cut lower down: the rest below goes, and no rope drops
    int yy = y;
    while (w.getBlock(x, yy, z) == I.rope && (w.getMeta(x, yy, z) & HOOK_ROPE)) { w.setBlock(x, yy, z, AIR, 0); yy--; }
    lv.fx->material("wool", "dig", C(x, y, z));
    return false;
  }
  if (id == I.rope) {
    // cut a rope and the rest below comes down too
    int n = 0, yy = y - 1;
    while (w.getBlock(x, yy, z) == I.rope) { w.setBlock(x, yy, z, AIR, 0); n++; yy--; }
    if (n && drops) dropItem(lv, p.realm, ItemStack("rope", n), vec3(x + 0.5f, yy + 1.5f, z + 0.5f));
    return true;
  }
  if (isCampfire(id)) {
    dropCampFood(lv, nullptr, w, x, y, z, false);
    if (meta & CAMPFIRE_KIT) {   // a portable campfire gives its kit back
      w.setBlockEntity(x, y, z, nullptr);
      w.setBlock(x, y, z, AIR);
      if (drops) dropItem(lv, p.realm, ItemStack("campfire_kit", 1), vec3(x + 0.5f, y + 0.4f, z + 0.5f));
      lv.fx->material("wood", "dig", C(x, y, z));
      return false;
    }
    return true;
  }
  if (id == I.anchor && I.anchor) {   // the hook drops (with its record), and its rope comes down
    grappleTake(lv, p, ivec3(x, y, z), false);
    lv.fx->material("metal", "dig", C(x, y, z));
    return false;
  }
  if (id == I.bag && I.bag) {   // the sleeping bag's spawn goes with it
    vec3 sp = p.spawnPoint;
    if (sp.y >= 0 && ifloor(sp.x) == x && ifloor(sp.z) == z && std::fabs(sp.y - y) < 1.5f) {
      const json b = p.stats.value("bagRestore", json());
      if (b.is_object() && b.contains("spawn") && b["spawn"].is_array()) { p.spawnPoint = vec3(b["spawn"][0], b["spawn"][1], b["spawn"][2]); p.spawnRealm = b.value("realm", 0); }
      else { p.spawnPoint = vec3(0, -1, 0); if (b.is_object()) p.spawnRealm = b.value("realm", 0); }
      p.stats.erase("bagRestore");
    }
  }
  return true;
}

// ------------------------------------------------------------------ the sleeping bag
bool sleepingBagSleep(Level& lv, Player& p, int x, int y, int z) {
  World& w = lv.world(p.realm);
  bool bag = w.getBlock(x, y, z) == ids().bag && ids().bag;
  if (!bag) return true;
  if (jsDimOf(p) != JS_OVERWORLD) { toast(lv, p, "You can't settle down to sleep here", "warn"); return false; }
  if (!lv.isNight()) { toast(lv, p, "You can only sleep at night"); return false; }
  if (!p.stats.contains("bagRestore")) {
    bool here = p.spawnPoint.y >= 0 && ifloor(p.spawnPoint.x) == x && ifloor(p.spawnPoint.z) == z;
    json old = p.spawnPoint.y >= 0 && !here ? json::array({p.spawnPoint.x, p.spawnPoint.y, p.spawnPoint.z}) : json();
    p.stats["bagRestore"] = {{"spawn", old}, {"realm", p.spawnRealm}, {"day", lv.info.day}};
  }
  return true;
}

// ------------------------------------------------------------------ ticking
void campingTick(Level& lv, double dt) {
  CampState& st = campState(lv);
  for (auto& [r, w] : lv.allRealms()) st.attach(*w);
  if (!lv.isRemote()) {
    // cooking on the fire (the host)
    for (auto& [realm, set] : st.fires) {
      World* w = lv.worldIfOpen(realm);
      if (!w) continue;
      for (const ivec3& c : *set) {
        json* e = w->blockEntity(c.x, c.y, c.z);
        if (!e || !e->is_object() || e->value("type", "") != "campfire" || !(*e)["items"].is_array() || (w->getMeta(c.x, c.y, c.z) & CAMPFIRE_OUT)) continue;
        json& items = (*e)["items"];
        if (!(*e)["t"].is_array() || (*e)["t"].size() != 4) (*e)["t"] = json::array({0, 0, 0, 0});
        for (int k = 0; k < 4 && k < (int)items.size(); k++) {
          if (items[k].is_null()) continue;
          float t = (*e)["t"][k].get<float>() + (float)dt;
          (*e)["t"][k] = t;
          if (t < CAMP_COOK) continue;
          ItemStack s = ItemStack::fromJson(items[k]);
          items[k] = nullptr; (*e)["t"][k] = 0;
          auto sm = reg().smelting.find(s.id);
          if (sm != reg().smelting.end()) {
            vec2 sp = CAMP_SPOTS[k];
            dropItem(lv, realm, ItemStack(sm->second, 1), vec3(c.x + sp.x, c.y + 0.7f, c.z + sp.y), vec3((sp.x - 0.5f) * 2, 3, (sp.y - 0.5f) * 2));
          }
          sfxAt(lv, "pop", vec3(c.x + 0.5f, c.y + 0.6f, c.z + 0.5f));
          w->blockEntityChanged(c.x, c.y, c.z);
        }
      }
    }
  }
  for (Player* p : levelPlayers(lv)) {
    if (p->dead) continue;
    World& w = lv.world(p->realm);
    // stepping on a lit fire burns
    if (!creativeOf(*p) && p->onGround) {
      int bx = ifloor(p->pos.x), bz = ifloor(p->pos.z);
      if (campfireLit(w, bx, ifloor(p->pos.y - 0.1f), bz) || campfireLit(w, bx, ifloor(p->pos.y), bz)) {
        float& t = st.burnT[p->id]; t += (float)dt;
        if (t > 0.8f) { t = 0; hurtPlayer(lv, *p, 1, "burn"); }
      }
    }
    // the sleeping bag was only for the night
    const json b = p->stats.is_object() ? p->stats.value("bagRestore", json()) : json();
    if (b.is_object() && lv.info.day > b.value("day", (i64)0)) {
      if (b.contains("spawn") && b["spawn"].is_array()) p->spawnPoint = vec3(b["spawn"][0], b["spawn"][1], b["spawn"][2]);
      else p->spawnPoint = vec3(0, -1, 0);
      p->spawnRealm = b.value("realm", 0);
      p->stats.erase("bagRestore");
      toast(lv, *p, "You roll up your sleeping bag. Your bed is home again.");
    }
  }
}

void registerCampingEntities() {
  registerEntityFactory("grappling_hook", [](const std::string&) { return std::make_unique<GrappleHook>(); });
  registerLevelSystem([](Level&) { return std::make_shared<CampState>(); });
  // an out campfire gives no light (camping.js metaLight)
  for (const char* k : {"campfire", "soul_campfire"}) {
    BlockId id = reg().blockId(k);
    if (!id) continue;
    reg().blocks[id].props["lightMeta"] = "campfire";
    reg().tLMeta[id] = 1;
  }
}

} // namespace dq
