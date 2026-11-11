// sim/panning.cpp — see panning.h
#include "sim/panning.h"
#include "sim/player.h"
#include "sim/gameplay.h"
#include "core/registry.h"
#include <random>
#include <set>
#include "world/chunk.h"

namespace dq {

namespace {
float g_roll = -1;
float roll() { static thread_local std::mt19937 g{std::random_device{}()}; return g_roll >= 0 ? g_roll : std::uniform_real_distribution<float>(0, 1)(g); }
float roll2() { static thread_local std::mt19937 g{12345u}; return std::uniform_real_distribution<float>(0, 1)(g); }
bool siltAt(World& w, int x, int y, int z) { const std::string& k = reg().block(w.getBlock(x, y, z)).key; return k == "gravel" || k == "sand" || k == "red_sand"; }
bool waterBy(World& w, int x, int y, int z) {
  const int N[5][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}};
  for (auto& n : N) if (reg().tLiquid[w.getBlock(x + n[0], y + n[1], z + n[2])] == 1) return true;
  return false;
}
json& sluices(Level& lv) { json& l = lv.info.data["sluices"]; if (!l.is_array()) l = json::array(); return l; }
void addSluice(Level& lv, int realm, int x, int y, int z) {
  json& l = sluices(lv);
  for (auto& k : l) if (k[0] == realm && k[1] == x && k[2] == y && k[3] == z) return;
  l.push_back({realm, x, y, z});
}
// sluices as they're set down or loaded (a hook on each World, as sim/camping.cpp keeps its campfires)
class SluiceHook : public WorldSystem {
public:
  SluiceHook(Level& lv, BlockId s) : lv(lv), S(s) {}
  const char* name() const override { return "sluices"; }
  void onBlockChanged(World& w, int x, int y, int z, BlockId, BlockId n) override { if (n == S) addSluice(lv, w.realm, x, y, z); }
  void onChunkLoaded(World& w, Chunk& c) override {
    for (int sy = 0; sy < SECS; sy++) {
      const Section* sec = c.section(sy); if (!sec) continue;
      for (int i = 0; i < 4096; i++) if (sec->blocks[i] == S) addSluice(lv, w.realm, c.worldX() + (i & 15), c.baseY() + sy * 16 + (i >> 8), c.worldZ() + ((i >> 4) & 15));
    }
  }
  Level& lv; BlockId S;
};
}

void setPanRoll(float r) { g_roll = r; }
ItemStack panFind(float r, float r2) {
  if (r < 0.30f) {
    static const std::pair<const char*, float> T[] = {{"copper", 0.3f}, {"tin", 0.2f}, {"gold", 0.15f}, {"silver", 0.1f}, {"zinc", 0.1f}, {"bismuth", 0.1f}, {"iron", 0.05f}};
    float a = 0; for (auto& [m, f] : T) { a += f; if (r2 < a) return ItemStack(std::string(m) + "_ore_sample", 1); }
    return ItemStack("copper_ore_sample", 1);
  }
  if (r < 0.33f) { const char* g = r2 < 0.3f ? "emerald" : r2 < 0.6f ? "ruby" : r2 < 0.9f ? "sapphire" : "diamond"; if (reg().item(g)) return ItemStack(g, 1); }
  return {};
}

bool panUse(Level& lv, Player& p, const RayHit& hit, bool repeat, float step) {
  if (p.held().id != "ceramic_pan") return false;
  World& w = lv.world(p.realm);
  const ivec3 b = hit.block;
  if (!siltAt(w, b.x, b.y, b.z)) { if (!repeat) toast(lv, p, "Pan gravel or sand by the water"); return true; }
  json& d = p.data; if (!d.is_object()) d = json::object();
  const json key = {b.x, b.y, b.z};
  if (!repeat || d.value("panAt", json()) != key) { d["panAt"] = key; d["panT"] = 0.0; return true; }
  const float t = d.value("panT", 0.f) + step;
  d["panT"] = t;
  if (t < PAN_HOLD) return true;
  d["panT"] = 0.0;
  if (!waterBy(w, b.x, b.y, b.z)) { toast(lv, p, "It needs water beside it to wash"); return true; }
  ItemStack f = panFind(roll(), roll2());
  if (!f.empty()) giveItem(lv, p, f);
  if (roll2() < 0.25f) w.setBlock(b.x, b.y, b.z, AIR);
  lv.fx->sound("splash", vec3(b) + vec3(0.5f, 1, 0.5f), 12);
  return true;
}

ItemStack sluiceTake(World& w, int x, int y, int z) {
  json* e = w.blockEntity(x, y, z);
  if (!e || !(*e)["out"].is_array() || (*e)["out"].empty()) return {};
  ItemStack s = ItemStack::fromJson((*e)["out"].back()); (*e)["out"].erase((*e)["out"].size() - 1);
  w.blockEntityChanged(x, y, z);
  return s;
}

void sluiceTick(Level& lv, double dt) {
  if (lv.isRemote()) return;
  static BlockId S = 0; if (!S) S = reg().blockId("sluice");
  if (!S) return;
  // the world itself says whether it's hooked (a process-wide pointer set went stale when a new world reused an address)
  for (auto& [r, w] : lv.allRealms()) if (!w->system<SluiceHook>()) { auto h = std::make_shared<SluiceHook>(lv, S); w->addSystem(h); for (auto& [k, c] : w->chunks()) h->onChunkLoaded(*w, *c); }
  json& l = sluices(lv);
  for (size_t i = 0; i < l.size();) {
    World* w = lv.worldIfOpen(l[i][0].get<int>());
    const int x = l[i][1], y = l[i][2], z = l[i][3];
    if (!w || !w->isLoaded(x, y, z)) { i++; continue; }
    if (w->getBlock(x, y, z) != S) { l.erase(l.begin() + i); continue; }
    json& e = *w->blockEntity(x, y, z, true);
    if (!e.is_object() || e.value("type", "") != "sluice") e = json{{"type", "sluice"}, {"t", 0.0}, {"finds", 0}, {"out", json::array()}};
    bool silt = false; for (int dx = -2; dx <= 2 && !silt; dx++) for (int dz = -2; dz <= 2 && !silt; dz++) for (int dy = -1; dy <= 0 && !silt; dy++) if (siltAt(*w, x + dx, y + dy, z + dz)) silt = true;
    if (silt && waterBy(*w, x, y, z)) {
      const double t = e.value("t", 0.0) + dt;
      e["t"] = t;
      if (t >= SLUICE_EVERY) {
        e["t"] = 0.0;
        ItemStack f = panFind(roll(), roll2());
        if (!f.empty() && e["out"].size() < 16) e["out"].push_back(f.toJson());
        e["finds"] = e.value("finds", 0) + 1;
        if (e.value("finds", 0) >= SLUICE_LIFE) { spillEntity(lv, *w, e, x, y, z); w->setBlockEntity(x, y, z, nullptr); w->setBlock(x, y, z, AIR); dropItem(lv, w->realm, ItemStack("lumber", 3), vec3(x + 0.5f, y + 0.5f, z + 0.5f), true); l.erase(l.begin() + i); continue; }
        w->blockEntityChanged(x, y, z);
      }
    }
    i++;
  }
}

} // namespace dq
