// sim/treasure_maps.cpp — see treasure_maps.h.
#include "sim/treasure_maps.h"
#include "sim/player.h"
#include "sim/content.h"
#include "sim/maps.h"
#include "sim/events.h"
#include "gen/generator.h"
#include "world/dims.h"
#include "core/registry.h"
#include <cmath>
#include <random>

namespace dq {

namespace {
json& treasures(Level& lv) { json& T = lv.info.data["treasures"]; if (!T.is_object()) T = json::object(); return T; }

std::string direction(float dx, float dz) {   // (north is -z)
  static const char* N[8] = {"north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"};
  float a = std::atan2(dx, -dz); if (a < 0) a += TAU;
  return N[(int)std::lround(a / (TAU / 8)) % 8];
}

json loot(std::mt19937& r) {
  json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr);
  auto roll = [&](int lo, int hi) { return lo + (int)(r() % (unsigned)(hi - lo + 1)); };
  std::vector<ItemStack> v = {ItemStack("gold_coin", roll(12, 40)), ItemStack("gold_ingot", roll(2, 6)), ItemStack("iron_ingot", roll(3, 9))};
  const char* rare[] = {"diamond", "emerald", "ruby", "sapphire", "enchanted_book", "golden_apple"};
  for (int k = 0, n = roll(2, 3); k < n; k++) { const std::string id = rare[r() % 6]; if (reg().item(id)) v.push_back(ItemStack(id, id == "enchanted_book" || id == "golden_apple" ? 1 : roll(1, 3))); }
  std::vector<int> slots(27); for (int i = 0; i < 27; i++) slots[i] = i;
  std::shuffle(slots.begin(), slots.end(), r);
  for (size_t i = 0; i < v.size(); i++) if (reg().item(v[i].id)) items[slots[i]] = v[i].toJson();
  return items;
}

class TreasureMaps : public LevelSystem {
public:
  explicit TreasureMaps(Level& l) : lv(l) {
    if (!lv.events) return;
    hUse = lv.events->on("itemUse", [this](const json& a) {
      if (!a.is_array() || a.size() < 6 || !a[1].is_string() || a[1].get<std::string>() != "treasure_map" || lv.isRemote()) return true;
      Player* p = findPlayer(lv, a[0].is_string() ? a[0].get<std::string>() : "");
      if (!p || p->held().id != "treasure_map") return true;
      ItemStack& h = p->held();
      if (!h.tag.is_object() || !h.tag.contains("tx")) if (!markX(*p, h)) { lv.fx->message(p->id, "The map's too faded to read here", ""); return false; }
      const int tx = h.tag["tx"], tz = h.tag["tz"];
      const float dx = tx + 0.5f - p->pos.x, dz = tz + 0.5f - p->pos.z, d = std::hypot(dx, dz);
      json& seen = p->stats["treasureSeen"]; if (!seen.is_object()) seen = json::object();
      seen[h.tag.value("tid", std::string())] = true;
      lv.fx->message(p->id, d < 4 ? "The X is right here: dig!" : "The X lies about " + std::to_string((int)std::lround(d)) + " blocks " + direction(dx, dz) + " of here", "");
      return false;
    });
    addMapMarkerSource([this](Level& L, const PaperMap& m, const Player* viewer, std::vector<MapMarker>& out) {
      if (&L != &lv || !viewer || m.dim != JS_OVERWORLD || !viewer->stats.contains("treasureSeen")) return;
      const float s = (float)(1 << m.scale);
      for (auto& [tid, r] : treasures(lv).items()) {
        if (r.value("found", false) || !viewer->stats["treasureSeen"].contains(tid)) continue;
        const float u = (r.value("x", 0.f) + 0.5f - m.x0) / s, v = (r.value("z", 0.f) + 0.5f - m.z0) / s;
        if (u < 0 || v < 0 || u > MAP_N || v > MAP_N) continue;
        MapMarker k; k.cell = 16; k.u = u; k.v = v; k.label = "Treasure"; out.push_back(k);
      }
    });
  }
  ~TreasureMaps() override { if (lv.events) lv.events->off(hUse); }
  const char* name() const override { return "treasureMaps"; }

  void tick(Level&, double dt) override {
    if (lv.isRemote() || (acc += dt) < 1) return;
    acc = 0;
    World& w = lv.world(REALM_MAIN);
    for (auto& [tid, r] : treasures(lv).items()) {
      if (r.value("found", false)) continue;
      const int x = r.value("x", 0), z = r.value("z", 0);
      if (!r.value("buried", false)) {   // the land at the X has come in: bury it
        if (!w.isLoaded(x, dimBase(DIM_OVERWORLD) + 100, z)) continue;
        const int top = w.surfaceY(x, z, DIM_OVERWORLD);
        if (top <= dimBase(DIM_OVERWORLD)) continue;
        const int y = top - 3;
        w.setBlock(x, y, z, reg().blockId("chest"));
        std::mt19937 rr((u32)std::hash<std::string>()(tid));
        w.setBlockEntity(x, y, z, json{{"type", "chest"}, {"items", loot(rr)}});
        r["buried"] = true; r["y"] = y;
        continue;
      }
      // dug up: the chest gone or emptied
      const int y = r.value("y", 0);
      if (!w.isLoaded(x, y, z)) continue;
      const bool gone = reg().block(w.getBlock(x, y, z)).key != "chest";
      bool empty = !gone;
      if (const json* e = w.blockEntity(x, y, z); e && e->contains("items")) for (auto& it : (*e)["items"]) if (!it.is_null()) empty = false;
      if (gone || empty) {
        r["found"] = true;
        for (auto& up : lv.entities()) if (auto* p = dynamic_cast<Player*>(up.get()); p && !p->dead && glm::distance(p->pos, vec3(x, y, z)) < 8) lv.fx->message(p->id, "You found the treasure!", "good");
      }
    }
  }

private:
  Level& lv; int hUse = -1; double acc = 0;

  // the X: a dry spot some way off, not too high (the first one found going round)
  bool markX(Player& p, ItemStack& h) {
    DimGenerator* g = lv.gen ? lv.gen->forDim(DIM_OVERWORLD) : nullptr;
    if (!g || dimOfY(ifloor(p.pos.y)) != DIM_OVERWORLD) return false;
    std::mt19937 r((u32)(std::random_device{}() ^ (u32)(p.pos.x * 31 + p.pos.z)));
    for (int tries = 0; tries < 64; tries++) {
      const float a = (r() % 3600) / 3600.f * TAU, d = 300 + (r() % 400);
      const int x = (int)std::lround(p.pos.x + std::sin(a) * d), z = (int)std::lround(p.pos.z - std::cos(a) * d);
      ColumnSample s; g->sample(x, z, s);
      if (s.height <= 0 || s.water >= 0 || s.height > 180) continue;
      const std::string tid = std::to_string(x) + "," + std::to_string(z);
      treasures(lv)[tid] = json{{"x", x}, {"z", z}};
      if (!h.tag.is_object()) h.tag = json::object();
      h.tag["tx"] = x; h.tag["tz"] = z; h.tag["tid"] = tid;
      return true;
    }
    return false;
  }
};
}

void registerTreasureMaps() { registerLevelSystem([](Level& lv) { return std::make_shared<TreasureMaps>(lv); }); }

} // namespace dq
