// sim/treasure_maps.cpp — see treasure_maps.h.
#include "sim/treasure_maps.h"
#include "sim/player.h"
#include "sim/content.h"
#include "sim/maps.h"
#include "sim/events.h"
#include "gen/generator.h"
#include "world/dims.h"
#include "core/registry.h"
#include "gen/gendefs.h"
#include "common/threadpool.h"
#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <cmath>
#include <random>

namespace dq {

bool interactable(BlockId id);   // (sim/controller.cpp: a block with a use of its own)

namespace {
json& treasures(Level& lv) { json& T = lv.info.data["treasures"]; if (!T.is_object()) T = json::object(); return T; }


json loot(std::mt19937& r) {
  json items = json::array(); for (int i = 0; i < 27; i++) items.push_back(nullptr);
  auto roll = [&](int lo, int hi) { return lo + (int)(r() % (unsigned)(hi - lo + 1)); };
  std::vector<ItemStack> v = {ItemStack("gold_coin", roll(12, 40)), ItemStack("gold_nugget", roll(4, 12)), ItemStack("copper_ingot", roll(2, 6))};
  if ((r() % 100) < 18 && reg().item("gold_ingot")) v.push_back(ItemStack("gold_ingot", roll(1, 2)));
  if ((r() % 100) < 10 && reg().item("enchanted_book")) v.push_back(ItemStack("enchanted_book", 1));
  if ((r() % 100) < 10 && reg().item("golden_apple")) v.push_back(ItemStack("golden_apple", 1));
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
      // it's an item like any other first: crouched it's set down on the ground, and used on a chest (or anything with a
      // use of its own) it goes in -- the map is only opened when it's used at the land or the sky
      if (p->sneaking) return true;
      if (a[5].is_number() && a[5].get<int>() >= 0 && a[2].is_number() && a[3].is_number() && a[4].is_number())
        if (interactable(lv.world(p->realm).getBlock(a[2].get<int>(), a[3].get<int>(), a[4].get<int>()))) return true;
      ItemStack& h = p->held();
      if (!h.tag.is_object() || !h.tag.contains("tx")) if (!markX(*p, h)) { lv.fx->message(p->id, "The map's too faded to read here", ""); return false; }
      const int tx = h.tag["tx"], tz = h.tag["tz"];
      json& seen = p->stats["treasureSeen"]; if (!seen.is_object()) seen = json::object();
      seen[h.tag.value("tid", std::string())] = true;
      sfxUi(lv, *p, "map_paper");
      openContentScreen(lv, *p, "treasure_map", {{"slot", p->selected}, {"x", tx}, {"z", tz}});   // (ui/treasure_map_screen.cpp)
      return false;
    });
    addMapMarkerSource([this](Level& L, const PaperMap& m, const Player* viewer, std::vector<MapMarker>& out) {
      if (m.id.rfind("treasure~", 0) == 0) {   // (a treasure map's own picture: its X in the middle)
        MapMarker k; k.cell = 16; k.u = MAP_N * 0.5f; k.v = MAP_N * 0.5f; out.push_back(k); return;
      }
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

std::string treasureDirection(float dx, float dz) {   // (north is -z)
  static const char* N[8] = {"north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"};
  float a = std::atan2(dx, -dz); if (a < 0) a += TAU;
  return N[(int)std::lround(a / (TAU / 8)) % 8];
}

namespace {
// a picture being drawn on a worker: the generator's land, handed over when it's done
struct Drawing { PaperMap gen; std::atomic<bool> done{false}; int cursor = 0; };
std::unordered_map<const PaperMap*, std::shared_ptr<Drawing>>& drawings() { static std::unordered_map<const PaperMap*, std::shared_ptr<Drawing>> d; return d; }
}

PaperMap* treasureMapPicture(Level& lv, int tx, int tz) {
  MapStore& st = mapStore(lv);
  const std::string id = "treasure~" + std::to_string(tx) + "," + std::to_string(tz);
  auto& D = drawings();
  if (auto it = st.all().find(id); it != st.all().end()) {
    PaperMap* m = it->second.get();
    auto d = D.find(m);
    if (d != D.end() && (d->second->gen.x0 != m->x0 || d->second->gen.z0 != m->z0)) { D.erase(d); d = D.end(); }   // (another world's, gone)
    if (d != D.end()) {
      Drawing& dr = *d->second;
      if (dr.done.load()) {   // the generator's land first, then what's loaded over it, as it is
        for (int i = 0; i < MAP_PIX; i++) if (dr.gen.h[i] && !m->h[i]) m->setPixel(i, dr.gen.ids[i], dr.gen.h[i], dr.gen.cl[i]);
        int cur = 0; mapPaintLoaded(lv, *m, cur, MAP_PIX);
        D.erase(d);
      } else mapPaintLoaded(lv, *m, dr.cursor, 512);   // (meanwhile the land that's loaded, a little each call)
      st.dirty.erase(id);
    }
    return m;
  }
  PaperMap* m = st.create(id, JS_OVERWORLD, tx, tz, TREASURE_MAP_SCALE);
  const int half = (MAP_N << TREASURE_MAP_SCALE) / 2;
  m->x0 = tx - half; m->z0 = tz - half;   // (centred on the X, not on the grid the drawn maps keep)
  st.dirty.erase(id);   // (never saved: it's made again from the X)
  std::shared_ptr<WorldGen> gen = lv.gen;
  if (!gen || !gen->forDim(DIM_OVERWORLD)) { int cur = 0; mapPaintLoaded(lv, *m, cur, MAP_PIX); st.dirty.erase(id); return m; }
  auto dr = std::make_shared<Drawing>();
  dr->gen.dim = m->dim; dr->gen.x0 = m->x0; dr->gen.z0 = m->z0; dr->gen.scale = m->scale;
  D[m] = dr;
  auto job = [gen, dr] { mapGeneratedPixels(*gen->forDim(DIM_OVERWORLD), dr->gen, dr->gen); dr->done = true; };
  if (lv.pool) lv.pool->submit(job, -1);   // (a worker: the generator's caches are its own there)
  else job();                              // (no workers: a test, at once)
  return treasureMapPicture(lv, tx, tz);
}

PaperMap* readableMapFor(Level& lv, Player& p, ItemStack& s) {
  int tx, tz;
  if (treasureTarget(s, tx, tz)) return treasureMapPicture(lv, tx, tz);
  return isMapStack(s) ? mapStore(lv).forStack(p, s) : nullptr;
}

std::string treasureHint(Level& lv, int tx, int tz, float px, float pz) {
  const float dx = tx + 0.5f - px, dz = tz + 0.5f - pz, d = std::hypot(dx, dz);
  if (d < 4) return "The X is right here: dig!";
  std::string t = "Buried " + treasureDirection(dx, dz) + " of here, ";
  if (d < 24) t += "only a few steps away";
  else if (d < 100) t += "under a hundred blocks away";
  else t += "about " + std::to_string((int)std::lround(d / 50) * 50) + " blocks away";
  DimGenerator* g = lv.gen ? lv.gen->forDim(DIM_OVERWORLD) : nullptr;
  if (!g) return t;
  // what lies between (water on the way: a river, or the open sea; high ground) and the land at the X
  int wet = 0, run = 0, longest = 0, high = 0;
  const int steps = std::clamp((int)(d / 12), 1, 64);
  for (int i = 1; i < steps; i++) {
    ColumnSample c; g->sample((int)std::lround(px + dx * i / steps), (int)std::lround(pz + dz * i / steps), c);
    if (c.water >= 0 && c.water >= c.height) { wet++; run++; longest = std::max(longest, run); } else run = 0;
    if (c.height > 120) high++;
  }
  ColumnSample at; g->sample(tx, tz, at);
  std::vector<std::string> notes;
  if (wet) notes.push_back(longest * d / steps > 60 ? "across open water" : "past the river");
  else if (high * 4 > steps) notes.push_back("over the high ground");
  switch (at.biome) {
    case BIOME_FOREST: notes.push_back("in the woods"); break;
    case BIOME_DESERT: notes.push_back("in the sands"); break;
    case BIOME_TAIGA: notes.push_back("among the pines"); break;
    case BIOME_MOUNTAINS: notes.push_back("up in the hills"); break;
    case BIOME_PLAINS: notes.push_back("out on the grassland"); break;
    case BIOME_BEACH: notes.push_back("by the shore"); break;
    default: break;
  }
  for (size_t i = 0; i < notes.size(); i++) t += (i == 0 ? " \xE2\x80\x94 " : ", ") + notes[i];
  return t;
}

void registerTreasureMaps() { registerLevelSystem([](Level& lv) { return std::make_shared<TreasureMaps>(lv); }); }

} // namespace dq
