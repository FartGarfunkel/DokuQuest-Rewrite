// sim/trough.cpp — see trough.h
#include "sim/trough.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/creature.h"
#include "core/registry.h"
#include "world/chunk.h"
#include <cmath>
#include <unordered_set>

namespace dq {

// what some creature eats (mobs.json "food"): only that goes in a trough
static bool isAnimalFood(const std::string& id) {
  static const std::unordered_set<std::string> foods = [] {
    std::unordered_set<std::string> s;
    for (auto& [k, m] : reg().mobs.items()) if (m.is_object() && m.contains("food") && m["food"].is_array()) for (auto& f : m["food"]) if (f.is_string()) s.insert(f.get<std::string>());
    return s;
  }();
  return foods.count(id) > 0;
}
static bool isTrough(World& w, int x, int y, int z) { return reg().block(w.getBlock(x, y, z)).has("trough"); }

ItemStack troughStack(World& w, int x, int y, int z) {
  const json* e = w.blockEntity(x, y, z);
  if (!e || !e->is_object() || e->value("type", "") != "trough" || !e->contains("items") || !(*e)["items"].is_array() || (*e)["items"].empty()) return {};
  return ItemStack::fromJson((*e)["items"][0]);
}

int troughLevel(int n) { return n <= 0 ? 0 : std::min(4, n / 16 + 1); }

static void troughsChanged();
static void setStack(World& w, int x, int y, int z, const ItemStack& s) {
  troughsChanged();   // (the animals look again)
  if (s.empty()) w.setBlockEntity(x, y, z, {{"type", "trough"}, {"items", json::array({nullptr})}});
  else w.setBlockEntity(x, y, z, {{"type", "trough"}, {"items", json::array({s.toJson()})}});
}

bool troughUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held) {
  const int x = hit.block.x, y = hit.block.y, z = hit.block.z;
  if (!isTrough(w, x, y, z)) return false;
  ItemStack in = troughStack(w, x, y, z);
  const vec3 at(x + 0.5f, y + 0.6f, z + 0.5f);
  if (!held.empty()) {   // tip it in: the same food tops it up, as much as a stack holds (animal food only; anything else as ever)
    if (!isAnimalFood(held.id)) return false;
    if (!in.empty() && !in.canMerge(held)) { toast(lv, p, "There's " + (in.def() ? in.def()->name : in.id) + " in it already"); return true; }
    const int room = (in.empty() ? held.maxStack() : in.maxStack() - in.n);
    if (room <= 0) { toast(lv, p, "The trough is full"); return true; }
    const int put = std::min(room, held.n);
    if (in.empty()) { in = held; in.n = 0; }
    in.n += put;
    if (!p.creative() && (held.n -= put) <= 0) held = ItemStack();
    setStack(w, x, y, z, in);
    sfxMaterial(lv, "grass", "place", at);
    return true;
  }
  if (in.empty()) return false;   // an empty hand on an empty trough: nothing to do
  setStack(w, x, y, z, ItemStack());
  if (int left = giveItem(lv, p, in); left > 0) { in.n = left; dropItem(lv, p.realm, in, p.pos + vec3(0, 1, 0), true, 0.f); }
  sfxMaterial(lv, "wood", "hit", at);
  return true;
}

namespace {
// the troughs with food in them near the players, looked for every couple of seconds (one list a level)
struct Found { ivec3 at; int realm; std::string food; };
struct Cache { const Level* lv = nullptr; double t = -1e9; std::vector<Found> list; };
Cache& cache() { static Cache c; return c; }

const std::vector<Found>& troughsNear(Level& lv) {
  Cache& C = cache();
  const double now = nowSeconds();   // (real seconds: the day can stand still)
  if (C.lv == &lv && now - C.t < 2) return C.list;
  C.lv = &lv; C.t = now; C.list.clear();
  static const BlockId trough = reg().blockId("feeding_trough");
  if (!trough) return C.list;
  std::vector<std::pair<int, ivec2>> seen;
  for (auto& up : lv.entities()) {
    Player* p = dynamic_cast<Player*>(up.get());
    if (!p || p->removed) continue;
    World& w = lv.world(p->realm);
    const int dim = dimOfY(ifloor(p->pos.y)), pcx = ifloor(p->pos.x) >> 4, pcz = ifloor(p->pos.z) >> 4;
    for (int cz = pcz - 4; cz <= pcz + 4; cz++) for (int cx = pcx - 4; cx <= pcx + 4; cx++) {
      bool dup = false; for (auto& s : seen) if (s.first == p->realm && s.second == ivec2(cx, cz)) dup = true;
      if (dup) continue;
      seen.push_back({p->realm, ivec2(cx, cz)});
      Chunk* c = w.chunk(dim, cx, cz);
      if (!c || c->blockEntities.empty()) continue;
      for (auto& [idx, e] : c->blockEntities) {
        if (!e.is_object() || e.value("type", "") != "trough") continue;
        const ivec3 at(cx * 16 + (idx & 15), c->baseY() + (idx >> 8), cz * 16 + ((idx >> 4) & 15));
        if (w.getBlock(at.x, at.y, at.z) != trough) continue;
        const ItemStack s = troughStack(w, at.x, at.y, at.z);
        if (!s.empty()) C.list.push_back({at, p->realm, s.id});
      }
    }
  }
  return C.list;
}
}  // namespace
static void troughsChanged() { cache().t = -1e9; }

std::optional<ivec3> troughFor(Level& lv, const Creature& c, float r) {
  if (!c.T || c.T->food.empty()) return std::nullopt;
  std::optional<ivec3> best; float bd = r;
  for (auto& f : troughsNear(lv)) {
    if (f.realm != c.realm || !c.T->eats(f.food)) continue;
    const float d = glm::distance(vec3(f.at) + vec3(0.5f, 0.5f, 0.5f), c.pos);
    if (d < bd) { bd = d; best = f.at; }
  }
  return best;
}

bool troughBite(Level& lv, World& w, ivec3 at, const Creature& c) {
  ItemStack s = troughStack(w, at.x, at.y, at.z);
  if (s.empty() || !c.T || !c.T->eats(s.id)) { troughsChanged(); return false; }   // (gone or emptied: look again, don't stand about)
  if (--s.n <= 0) s = ItemStack();
  setStack(w, at.x, at.y, at.z, s);
  return true;
}

}  // namespace dq
