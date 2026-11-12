// sim/charcoal_pit.cpp — see charcoal_pit.h
#include "sim/charcoal_pit.h"
#include "sim/piles.h"
#include "sim/firepit.h"
#include "sim/blockinfo.h"
#include "core/registry.h"
#include <random>

namespace dq {

namespace {
json& litList(Level& lv) { json& l = lv.info.data["charcoalPits"]; if (!l.is_array()) l = json::array(); return l; }
bool burns(BlockId b) {
  if (bhas(b, BF::IsLog) || bhas(b, BF::IsLeaves)) return true;
  const std::string& k = reg().block(b).key;
  for (const char* s : {"planks", "wool", "thatch", "hay", "wood", "carpet"}) if (k.find(s) != std::string::npos) return true;
  return false;
}
bool covers(World& w, int x, int y, int z) {
  if (!w.isLoaded(x, y, z)) return true;
  const BlockId b = w.getBlock(x, y, z);
  if (pileKindOfBlock(b) == PileKind::Logs || reg().block(b).has("pile")) return true;   // another pile, or a heap of charcoal one became
  return reg().tSolid[b] && reg().tOpaque[b] && !burns(b);
}
float rnd01() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<float>(0, 1)(g); }
const int N6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
}

bool charcoalCovered(World& w, int x, int y, int z) {
  for (auto& n : N6) if (!covers(w, x + n[0], y + n[1], z + n[2])) return false;
  return true;
}

bool charcoalLight(Level& lv, World& w, int x, int y, int z) {
  if (pileKindOfBlock(w.getBlock(x, y, z)) != PileKind::Logs) return false;
  std::vector<ivec3> q = {{x, y, z}}; std::vector<ivec3> all;
  auto seen = [&](ivec3 p) { for (auto& a : all) if (a == p) return true; return false; };
  while (!q.empty() && all.size() < 64) {
    const ivec3 p = q.back(); q.pop_back();
    if (seen(p) || pileKindOfBlock(w.getBlock(p.x, p.y, p.z)) != PileKind::Logs) continue;
    all.push_back(p);
    for (auto& n : N6) q.push_back(p + ivec3(n[0], n[1], n[2]));
  }
  for (const ivec3& p : all) {
    json* e = w.blockEntity(p.x, p.y, p.z);
    if (!e || e->value("lit", false)) continue;
    (*e)["lit"] = true; (*e)["t"] = 0.0;
    litList(lv).push_back({w.realm, p.x, p.y, p.z});
    w.blockEntityChanged(p.x, p.y, p.z);
  }
  return !all.empty();
}

void charcoalTick(Level& lv, double dt) {
  if (lv.isRemote()) return;
  json& list = litList(lv);
  if (list.empty()) return;
  static double acc = 0;
  acc += dt;
  const bool check = acc >= 2;
  if (check) acc = 0;
  const double need = gameHourSecs(lv) * CHARCOAL_HOURS;
  const BlockId PILE = reg().blockId("item_pile");
  for (size_t i = 0; i < list.size();) {
    const json& k = list[i];
    World* w = lv.worldIfOpen(k[0].get<int>());
    const int x = k[1], y = k[2], z = k[3];
    if (!w || !w->isLoaded(x, y, z)) { i++; continue; }
    json* e = w->blockEntity(x, y, z);
    if (pileKindOfBlock(w->getBlock(x, y, z)) != PileKind::Logs || !e || !e->value("lit", false)) { list.erase(list.begin() + i); continue; }
    const double t = e->value("t", 0.0) + dt;
    (*e)["t"] = t;
    if (check && std::fmod(t, 4.0) < 2.0) lv.fx->particles("campfire_smoke", vec3(x + 0.5f, y + 1.2f, z + 0.5f), 1, {{"spread", 0.3}, {"vel", {0, 1.2, 0}}, {"life", 3}, {"size", 0.2}, {"grow", 1.5}, {"rise", true}});
    if (check && !charcoalCovered(*w, x, y, z)) {   // open to the air: it burns away
      lv.fx->particles("flame", vec3(x + 0.5f, y + 0.6f, z + 0.5f), 12, {{"spread", 0.45}, {"vel", {0, 1.2, 0}}, {"life", 1}, {"size", 0.15}, {"rise", true}});
      w->setBlockEntity(x, y, z, nullptr); w->setBlock(x, y, z, AIR, 0, SetFlags::Urgent);
      list.erase(list.begin() + i); continue;
    }
    if (t >= need) {
      const int logs = ItemStack::fromJson((*e)["item"]).n;
      const int n = std::max(1, (int)std::round(logs * (0.25f + 0.25f * rnd01())));
      w->setBlockEntity(x, y, z, nullptr);
      w->setBlock(x, y, z, PILE, 0, SetFlags::Urgent);
      w->setBlockEntity(x, y, z, json{{"type", "pile"}, {"items", {ItemStack("charcoal", n).toJson(), nullptr, nullptr, nullptr}}, {"rot", {0, 0, 0, 0}}});
      list.erase(list.begin() + i); continue;
    }
    i++;
  }
}

} // namespace dq
