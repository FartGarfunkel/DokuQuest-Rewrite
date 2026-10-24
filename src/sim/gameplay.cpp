// sim/gameplay.cpp — feedback, items on the ground (DroppedItem), block drops (game.js + obtain.js),
// falling trees, XP, and the per-level gameplay system (clock, item merging, crowding, world systems).
#include "sim/gameplay.h"
#include "sim/rules.h"
#include "sim/worldsys.h"
#include "sim/blockfx.h"
#include "sim/combat.h"
#include "sim/controller.h"
#include <random>
#include <unordered_set>

namespace dq {

static double rnd() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }
static json v3(vec3 v) { return json::array({v.x, v.y, v.z}); }

GameplayHooks& hooks() { static GameplayHooks h; return h; }

// ---- feedback ---------------------------------------------------------------------------------
void toast(Level& lv, const Player& p, const std::string& msg, const std::string& kind) {
  lv.fx->message(p.id, msg, kind);
  if (lv.events) lv.events->emit("toast", json::array({p.name, msg, kind}));
}
void toastAll(Level& lv, const std::string& msg, const std::string& kind) { lv.fx->message(0, msg, kind); }
void sfxUi(Level& lv, const Player& p, const std::string& name) { lv.fx->soundUi(p.id, name); }
void sfxAt(Level& lv, const std::string& name, vec3 pos, float range) { lv.fx->sound(name, pos, range); }
void sfxMaterial(Level& lv, const std::string& mat, const std::string& kind, vec3 pos) { lv.fx->material(mat, kind, pos); }
void burst(Level& lv, vec3 pos, const std::string& tile, int count, float speed) { lv.fx->particles("burst", pos, count, {{"tile", tile}, {"speed", speed}}); }
void floatText(Level& lv, const Player& p, vec3 pos, const std::string& text, const std::string& color) {
  lv.fx->particles("text", pos, 1, {{"text", text}, {"color", color}, {"player", p.id}});
}
void openScreen(Level& lv, const Player& p, const std::string& kind, const json& opts) {
  if (lv.events) lv.events->emit("screenOpen", json::array({p.name, kind, opts}));
}

int dirOf(float nx, float nz) { return std::fabs(nx) > std::fabs(nz) ? (nx > 0 ? 1 : 3) : (nz > 0 ? 2 : 0); }
int lookDir(const Player& p) { return dirOf(-std::sin(p.yaw), -std::cos(p.yaw)); }
int facingMeta(const Player& p) {
  float fx = -std::sin(p.yaw), fz = -std::cos(p.yaw);
  return std::fabs(fx) > std::fabs(fz) ? (fx > 0 ? 3 : 1) : (fz > 0 ? 2 : 0);
}

std::vector<Player*> playersIn(Level& lv, int realm) {
  std::vector<Player*> out;
  for (auto& e : lv.entities()) if (!e->removed && e->isPlayer() && e->realm == realm) out.push_back(static_cast<Player*>(e.get()));
  return out;
}
bool isMob(const Entity& e) { return !e.isPlayer() && reg().mobs.is_object() && reg().mobs.contains(e.type); }

const std::vector<BlockId>& soilIds() {
  static std::vector<BlockId> v; static size_t built = 0;
  if (built != reg().blocks.size()) {
    const KnownBlocks& B = KB();
    v.clear();
    for (BlockId id : {B.grass, B.dirt, B.snowy_grass, B.sky_grass, B.podzol, B.coarse_dirt, B.moss_block, B.rooted_dirt, B.mycelium, B.farmland, B.mud,
                       B.dirt_with_dry_grass, B.dry_dirt_with_dry_grass, B.dirt_with_coniferous_litter, B.dirt_with_rainforest_litter, B.dry_dirt, B.peat}) if (id) v.push_back(id);
    built = reg().blocks.size();
  }
  return v;
}

// ---- items ----------------------------------------------------------------------------------
int giveItem(Level& lv, Player& p, ItemStack s) {
  int n0 = s.n;
  int left = p.give(s);
  if (left < n0) {
    sfxUi(lv, p, "pop");
    if (lv.events) lv.events->emit("pickup", json::array({p.name, s.id, n0 - left}));
  }
  return left;
}

void grantXP(Level& lv, Player& p, int n, const vec3* at) {
  int ups = p.addXP(n);
  if (at) floatText(lv, p, *at, "+" + std::to_string(n) + " XP", "#b6f06a");
  if (ups) { sfxUi(lv, p, "levelup"); toast(lv, p, "Level " + std::to_string(p.level) + "!", "good"); }
}

// A dropped item: floats, gets pulled in by players near it, merges, takes root, fades after 5 minutes.
static const std::unordered_set<std::string> KEEP_DROPS = {"realm_fragment", "heart_crystal", "watcher_blade", "rift_staff", "ember_blade", "cinder_core", "elytra", "dragon_egg_ender", "pocket_key"};
class ItemEntity : public Entity {
public:
  ItemEntity() { type = "item"; w = 0.15f; h = 0.3f; }
  ItemStack stack;
  float pickupDelay = 0.6f, plantT = -1;
  bool floating = false;
  void sync() { data["stack"] = stack.toJson(); }
  bool fireproof() const {
    const std::string& id = stack.id;
    return startsWith(id, "cinderite") || id == "ember_blade" || id == "cinder_core" || id == "realm_fragment" || id == "heart_crystal" || id == "watcher_blade" || id == "rift_staff";
  }
  void load(const json& j) override { Entity::load(j); type = "item"; stack = ItemStack::fromJson(data.value("stack", json())); pickupDelay = data.value("pickupDelay", 0.0f); }
  json save() const override { json j = Entity::save(); j["data"]["stack"] = stack.toJson(); return j; }
  void unstick(const World& w) {
    auto freeAt = [&](float x, float y, float z) { return !collides(w, x - this->w, y, z - this->w, x + this->w, y + h, z + this->w); };
    for (int up = 1; up <= 12; up++) { float y = std::floor(pos.y) + up + 0.01f; if (freeAt(pos.x, y, pos.z)) { pos.y = y; vel.y = 0; return; } }
    for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) if (freeAt(pos.x + dx, pos.y, pos.z + dz)) { pos.x += dx; pos.z += dz; return; }
  }
  void takeRoot(Level& lv, World& w) {
    auto* d = reg().item(stack.id);
    if (!d || d->block.empty() || !rule(lv, "saplingsPlant")) return;
    BlockId bid = reg().blockId(d->block);
    if (!bid) return;
    const BlockDef& b = reg().block(bid);
    bool fungus = endsWith(b.key, "_fungus");
    if (!(bhas(bid, BF::Tree) || endsWith(b.key, "_propagule") || fungus)) return;
    int x = ifloor(pos.x), y = ifloor(pos.y + 0.1f), z = ifloor(pos.z);
    BlockId cur = w.getBlock(x, y, z);
    if (cur != AIR && !(reg().tReplaceable[cur] && !reg().tLiquid[cur])) return;
    BlockId below = w.getBlock(x, y - 1, z);
    const KnownBlocks& B = KB();
    bool ok;
    if (fungus) ok = below && (below == B.crimson_nylium || below == B.warped_nylium || below == B.soul_soil);
    else { auto& s = soilIds(); ok = std::find(s.begin(), s.end(), below) != s.end() && below != B.farmland; }
    if (!ok) return;
    for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) { BlockId n = w.getBlock(x + dx, y, z + dz); if (bhas(n, BF::Tree) || bhas(n, BF::IsLog)) return; }
    w.setBlock(x, y, z, bid, 0);
    if (--stack.n <= 0) removed = true;
    sync();
  }
  void tick(Level& lv, double dtd) override {
    float dt = (float)dtd;
    World& w = lv.world(realm);
    if (stack.empty()) { removed = true; return; }
    if (!w.isLoaded(ifloor(pos.x), ifloor(pos.y), ifloor(pos.z))) return;
    if (pickupDelay > 0) pickupDelay -= dt;
    if (collides(w, pos.x - this->w, pos.y, pos.z - this->w, pos.x + this->w, pos.y + h, pos.z + this->w)) unstick(w);
    Contacts c = sampleContacts(w, *this);
    if (c.lava && !fireproof()) { removed = true; sfxAt(lv, "fizz", pos); return; }
    if (c.water) {
      // float up to the surface and bob there
      int by = ifloor(pos.y + 0.15f), bx = ifloor(pos.x), bz = ifloor(pos.z);
      float top = by + liquidHeight(w.getBlock(bx, by, bz), w.getMeta(bx, by, bz), w.getBlock(bx, by + 1, bz));
      bool under = pos.y + 0.12f < top;
      vel.y += (under ? 14.f : -6.f) * dt;
      vel.y *= std::max(0.f, 1 - dt * 3);
      vel.x *= std::max(0.f, 1 - dt * 1.5f); vel.z *= std::max(0.f, 1 - dt * 1.5f);
      floating = true;
    } else { vel.y -= 18 * dt; floating = false; }
    inWater = c.water;
    if (data.value("floating", false) != floating) data["floating"] = floating;
    float k = onGround ? std::min(1.f, dt * 6) : std::min(1.f, dt * 0.5f);
    vel.x -= vel.x * k; vel.z -= vel.z * k;
    // any player nearby pulls the item in
    if (pickupDelay <= 0) {
      Player* best = nullptr; float bd = 2.4f;
      for (Player* q : playersIn(lv, realm)) {
        if (q->dead || !q->carrying.is_null()) continue;
        float d = glm::distance(q->pos + vec3(0, 0.8f, 0), pos);
        if (d < bd) { bd = d; best = q; }
      }
      if (best) {
        vec3 dv = best->pos + vec3(0, 0.8f, 0) - pos; float d = std::max(bd, 0.01f);
        float s = (2.4f - d) * 10 * dt;
        vel += dv / d * s * 8.f;
        if (d < 0.9f) {
          int left = giveItem(lv, *best, stack);
          if (left <= 0) { removed = true; return; }
          stack.n = left; pickupDelay = 1; sync();
        }
      }
    }
    moveEntity(w, *this, dt, {0.6f, 0, false});
    // a sapling left lying on soil takes root, so forests grow back by themselves
    if (onGround && !floating) {
      if (plantT < 0) plantT = 10 + (float)rnd() * 25;
      if (age > plantT) { plantT = age + 15 + (float)rnd() * 30; takeRoot(lv, w); if (removed) return; }
    }
    float voidY = realm == REALM_MAIN ? -30.f : dimBase(DIM_OVERWORLD) - 30.f;
    if ((age > 300 && !KEEP_DROPS.count(stack.id)) || pos.y < voidY) removed = true;
  }
};

Entity* dropItem(Level& lv, int realm, ItemStack s, vec3 p, bool scatter, float speed, const vec3* vel, float delay) {
  if (s.empty() || !reg().item(s.id)) return nullptr;
  auto e = std::make_unique<ItemEntity>();
  e->realm = realm; e->pos = p; e->stack = s;
  e->vel = {(float)(rnd() - 0.5) * 3, 3.5f, (float)(rnd() - 0.5) * 3};
  if (!scatter) { e->vel.x *= 0.3f; e->vel.z *= 0.3f; e->vel.y = 2; }
  if (speed) { e->vel.x *= speed / 2; e->vel.z *= speed / 2; }
  if (vel) e->vel = *vel;
  if (delay) e->pickupDelay = delay;
  e->yaw = (float)rnd() * TAU;
  e->sync();
  return lv.spawn(std::move(e));
}

std::vector<ItemStack> blockDrops(Level& lv, World& w, int x, int y, int z, BlockId id, u8 meta, const ToolInfo* tool, bool washed) {
  std::vector<ItemStack> out;
  if (!rule(lv, "blockDrops")) return out;
  const BlockDef& b = reg().block(id);
  const KnownBlocks& B = KB();
  const Registry& R = reg();
  bool shears = tool && tool->type == "shears";
  auto mk = [&](const std::string& k, int n) { if (n > 0 && R.item(k)) out.push_back(ItemStack(k, n)); };
  auto r = rnd;
  auto is = [&](BlockId v) { return v && id == v; };
  if (b.has("crop")) {
    const json& c = b.props["crop"];
    bool ripe = (meta & 15) >= c.value("stages", 1) - 1;
    std::string seed = c.value("seed", std::string()), drop = c.value("drop", std::string());
    if (b.key == "wheat_crop") { mk("wheat_seeds", ripe ? 1 + (int)(r() * 3) : 1); if (ripe) mk("wheat", 1); }
    else if (b.key == "beetroot_crop") { mk("beetroot_seeds", ripe ? 1 + (int)(r() * 3) : 1); if (ripe) mk("beetroot", 1); }
    else if (!seed.empty()) { mk(seed, ripe ? 1 + (int)(r() * 2) : 1); if (ripe) mk(drop, 1 + (int)(r() * 3)); }
    else mk(drop, ripe ? 2 + (int)(r() * 3) : 1);
  } else if (bhas(id, BF::IsLeaves)) {
    if (shears) mk(b.key, 1);
    else {
      if (r() < 0.06) mk(b.str("sapling", "sapling"), 1);
      if ((is(B.leaves) || is(B.dark_oak_leaves)) && r() < 0.025) mk("apple", 1);
      if (r() < 0.02) mk("stick", 1);
    }
  } else if (is(B.tall_grass) || is(B.fern) || is(B.tall_grass_double) || is(B.large_fern)) {
    if (shears) mk(is(B.tall_grass_double) ? "tall_grass_double" : b.key, 1);
    else { if (r() < 0.12) mk("wheat_seeds", 1); if (r() < 0.18) mk("straw", 1); }
  } else if (is(B.vine) || is(B.seagrass) || is(B.cobweb)) {
    if (shears) mk(b.key, 1); else if (is(B.cobweb)) mk("string", 1);
  } else if (is(B.gravel)) {
    mk(r() < 0.1 ? "flint" : "gravel", 1);
  } else if (R.tShape[id] == ShapeKind::Slab && (meta & 7) == 6) {
    mk(b.key, 2);
  } else if (R.tShape[id] == ShapeKind::Candle) {
    mk(b.key, (meta & 3) + 1);
  } else if (b.has("leafy") || is(B.glow_lichen) || is(B.hanging_roots) || is(B.weeping_vines) || is(B.twisting_vines) || is(B.cave_vines) || is(B.nether_sprouts)) {
    if (shears || is(B.weeping_vines) || is(B.twisting_vines)) mk(b.key, 1);
  } else if (is(B.cave_vines_lit)) {
    mk("glow_berries", 1);
  } else if (is(B.dry_grass) || is(B.marram_grass) || is(B.jungle_grass)) {
    if (shears) mk(b.key, 1); else if (r() < 0.15) mk("straw", 1);
  } else if (is(B.gilded_blackstone)) {
    if (r() < 0.3) mk("gold_nugget", 2 + (int)(r() * 4)); else mk("gilded_blackstone", 1);
  } else if (is(B.bee_nest) || is(B.beehive)) {
    mk(b.key, 1); if ((meta & 0x0c) == 0x0c) mk("honeycomb", 1);
  } else if (bhas(id, BF::Door) || bhas(id, BF::Bed) || bhas(id, BF::Double)) {
    if (!(bhas(id, BF::Double) && b.drop.empty())) mk(b.key, 1);
  } else if (b.props.contains("extraDrops") && b.props["extraDrops"].is_array()) {
    for (auto& e : b.props["extraDrops"]) if (e.is_array() && e.size() >= 4 && r() < e[1].get<double>()) { int a = e[2], bb = e[3]; mk(e[0], a + (int)(r() * (bb - a + 1))); }
  } else if (!b.drop.empty() && (!b.props.contains("dropChance") || r() < b.num("dropChance", 1))) {
    int n = 1;
    if (b.props.contains("dropCount") && b.props["dropCount"].is_array()) { int a = b.props["dropCount"][0], c = b.props["dropCount"][1]; n = a + (int)(r() * (c - a + 1)); }
    mk(b.drop, n);
  }
  // obtain.js: wild carrots and potatoes in the grass, nether wart among the Depths' roots
  if (!washed) {
    if (!shears && (is(B.tall_grass) || is(B.fern) || is(B.tall_grass_double) || is(B.large_fern))) {
      if (r() < 0.025) mk("carrot", 1); else if (r() < 0.025) mk("potato", 1);
    }
    if ((is(B.crimson_roots) || is(B.warped_roots) || is(B.nether_sprouts) || is(B.soul_sand) || is(B.soul_soil)) && r() < 0.08) mk("nether_wart_item", 1);
  }
  for (auto& h : hooks().drops) h(lv, w, x, y, z, id, meta, tool, washed, out);
  return out;
}

void dropBlockItems(Level& lv, World& w, int x, int y, int z, BlockId id, bool washed, u8 meta, const ToolInfo* tool) {
  for (auto& s : blockDrops(lv, w, x, y, z, id, meta, tool, washed)) dropItem(lv, w.realm, s, vec3(x + 0.5f, y + 0.4f, z + 0.5f), true);
}

void spillEntity(Level& lv, World& w, const json& ent, int x, int y, int z) {
  if (!ent.is_object()) return;
  std::vector<ItemStack> st;
  if (ent.contains("items") && ent["items"].is_array()) for (auto& s : ent["items"]) { ItemStack is = ItemStack::fromJson(s); if (!is.empty()) st.push_back(is); }
  if (ent.contains("stack")) { ItemStack is = ItemStack::fromJson(ent["stack"]); if (!is.empty()) st.push_back(is); }
  if (ent.contains("item")) { ItemStack is = ItemStack::fromJson(ent["item"]); if (!is.empty()) st.push_back(is); }
  if (ent.contains("disc") && ent["disc"].is_string()) st.push_back(ItemStack(ent["disc"], 1));
  if (ent.value("book", false)) st.push_back(ItemStack("book", 1));
  for (auto& s : st) dropItem(lv, w.realm, s, vec3(x + 0.5f, y + 0.5f, z + 0.5f), true);
}

// ---- a felled tree topples, then its logs and leaves land as drops -------------------------------
class FallingTree : public Entity {
public:
  FallingTree() { type = "falling_tree"; w = 0.5f; h = 1; noGravity = true; }
  float t = 0;
  void tick(Level& lv, double dt) override {
    t += (float)dt;
    if (t < 1.1f || removed) return;
    World& w = lv.world(realm);
    float dirYaw = data.value("dirYaw", 0.0f), height = data.value("height", 1.0f);
    sfxAt(lv, "fell", pos);
    for (Player* p : playersIn(lv, realm)) if (glm::distance(p->pos, pos) < 24) lv.fx->shake(p->id, 0.25f);
    float fx = -std::sin(dirYaw), fz = -std::cos(dirYaw);
    auto landingY = [&](float x, float z) {
      int bx = ifloor(x), bz = ifloor(z);
      for (int y = std::min(WORLD_H - 2, ifloor(pos.y + height + 3)); y > 0; y--) { BlockId id = w.getBlock(bx, y, bz); if (reg().tSolid[id] || reg().tLiquid[id]) return y + 1.05f; }
      return pos.y + 1;
    };
    if (data.contains("drops")) for (auto& s : data["drops"]) {
      ItemStack st = ItemStack::fromJson(s);
      float d = 1 + (float)rnd() * std::max(1.f, height - 1);
      float x = pos.x + 0.5f + fx * d + (float)(rnd() - 0.5), z = pos.z + 0.5f + fz * d + (float)(rnd() - 0.5);
      dropItem(lv, realm, st, vec3(x, landingY(x, z), z), false);
    }
    std::string tile = "oak_log";
    if (data.contains("blocks") && data["blocks"].is_array() && !data["blocks"].empty()) tile = data["blocks"][0].value("key", tile);
    for (int i = 0; i < 6; i++) { float d = (float)rnd() * height; float x = pos.x + 0.5f + fx * d, z = pos.z + 0.5f + fz * d; burst(lv, vec3(x, landingY(x, z) + 0.2f, z), tile, 4, 0.8f); }
    removed = true;
  }
};
Entity* spawnFallingTree(Level& lv, int realm, const std::vector<std::array<int, 4>>& blocks, ivec3 pivot, float dirYaw, const std::vector<ItemStack>& drops) {
  auto e = std::make_unique<FallingTree>();
  e->realm = realm; e->pos = vec3(pivot);
  json bl = json::array(); int top = pivot.y;
  for (auto& b : blocks) { bl.push_back({{"x", b[0]}, {"y", b[1]}, {"z", b[2]}, {"key", reg().block((BlockId)b[3]).key}}); top = std::max(top, b[1]); }
  json dr = json::array(); for (auto& s : drops) dr.push_back(s.toJson());
  e->data = {{"blocks", bl}, {"pivot", {pivot.x, pivot.y, pivot.z}}, {"dirYaw", dirYaw}, {"drops", dr}, {"height", top - pivot.y + 1}};
  return lv.spawn(std::move(e));
}

// ---- the per-level gameplay system --------------------------------------------------------------
class GameplaySystem : public LevelSystem {
public:
  explicit GameplaySystem(Level& lv) { rules(lv); }
  const char* name() const override { return "gameplay"; }
  double clock = 0, mergeT = 0, randT = 0;
  std::unordered_map<std::string, std::shared_ptr<void>> slots;
  void tick(Level& lv, double dt) override {
    clock += dt;
    for (auto& [r, w] : lv.allRealms()) attachWorldSystems(lv, *w);
    // merge dropped items lying together
    if ((mergeT += dt) > 1) {
      mergeT = 0;
      std::vector<ItemEntity*> items;
      for (auto& e : lv.entities()) if (!e->removed && e->type == "item") if (auto* it = dynamic_cast<ItemEntity*>(e.get())) items.push_back(it);
      for (size_t i = 0; i < items.size(); i++) for (size_t j = i + 1; j < items.size(); j++) {
        ItemEntity* a = items[i]; ItemEntity* b = items[j];
        if (a->removed || b->removed || a->realm != b->realm || !canMergeStacks(a->stack, b->stack)) continue;
        vec3 d = glm::abs(a->pos - b->pos);
        if (d.x < 0.8f && d.y < 0.8f && d.z < 0.8f && a->stack.n + b->stack.n <= maxStackOf(a->stack.id)) { a->stack.n += b->stack.n; a->sync(); b->removed = true; }
      }
    }
    // random ticks near every player, once a second
    if ((randT += dt) > 1) { randT = 0; for (auto& e : lv.entities()) if (e->isPlayer() && !e->removed) randomTicks(lv, lv.world(e->realm), e->pos.x, e->pos.z); }
    tickFurnaces(lv, dt);
    tickSleep(lv, dt);
    crowdPush(lv, (float)dt);
  }
};
void registerArrowEntity();
double gameClock(Level& lv) { if (auto* s = lv.system<GameplaySystem>()) return s->clock; return 0; }
std::shared_ptr<void>& levelSlotRaw(Level& lv, const std::string& key) {
  if (auto* s = lv.system<GameplaySystem>()) return s->slots[key];
  static std::unordered_map<std::string, std::shared_ptr<void>> orphan;   // (a level without the gameplay system)
  return orphan[key];
}

void registerGameplay() {
  registerLevelSystem([](Level& lv) { return std::make_shared<GameplaySystem>(lv); });
  registerEntityFactory("item", [](const std::string&) { return std::unique_ptr<Entity>(new ItemEntity()); });
  registerEntityFactory("falling_tree", [](const std::string&) { return std::unique_ptr<Entity>(new FallingTree()); });
  registerBlockFxEntities();
  registerBuildEntities();
  registerArrowEntity();
  registerCombat();
}

} // namespace dq
