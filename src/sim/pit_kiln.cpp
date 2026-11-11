// sim/pit_kiln.cpp — see pit_kiln.h
#include "sim/pit_kiln.h"
#include "sim/firepit.h"
#include "sim/heat.h"
#include "core/registry.h"

namespace dq {

namespace {
BlockId kilnId() { return reg().blockId("pit_kiln"); }
json* kilnEnt(World& w, int x, int y, int z) {
  if (w.getBlock(x, y, z) != kilnId() || !kilnId()) return nullptr;
  json* e = w.blockEntity(x, y, z, true);
  if (!e->is_object() || e->value("type", "") != "kiln") *e = json{{"type", "kiln"}, {"items", {nullptr, nullptr, nullptr, nullptr}}, {"straw", 0}, {"logs", 0}, {"lit", false}, {"t", 0}};
  if (!(*e)["items"].is_array() || (*e)["items"].size() != 4) (*e)["items"] = {nullptr, nullptr, nullptr, nullptr};
  return e;
}
void reheight(World& w, int x, int y, int z, const json& e) {
  const int straw = e.value("straw", 0), logs = e.value("logs", 0);
  const int px = 4 + (straw > 0 ? 2 : 0) + logs * 10 / KILN_LOGS;   // the pots, a layer of straw, the logs piled over
  w.setMeta(x, y, z, (u8)std::clamp((px + 1) / 2, 1, 8), SetFlags::Urgent);
}
json& litList(Level& lv) { json& l = lv.info.data["kilns"]; if (!l.is_array()) l = json::array(); return l; }
bool solidAt(World& w, int x, int y, int z) {
  if (!w.isLoaded(x, y, z)) return true;   // (a side in a chunk not loaded counts as solid: never put out on loading)
  const BlockId b = w.getBlock(x, y, z);
  return reg().tSolid[b] && (reg().tOpaque[b] || reg().tShape[b] == ShapeKind::Pile);
}
}

bool isUnfired(const std::string& id) { const ItemDef* d = reg().item(id); return d && !d->str("fires").empty(); }

bool kilnPlace(World& w, int x, int y, int z, int q, ItemStack& held) {
  if (held.empty() || !isUnfired(held.id) || q < 0 || q > 3) return false;
  json* e = kilnEnt(w, x, y, z);
  if (!e) {
    const BlockId cur = w.getBlock(x, y, z);
    if (cur != AIR && !(reg().tReplaceable[cur] && !reg().tLiquid[cur])) return false;
    if (!reg().tSolid[w.getBlock(x, y - 1, z)]) return false;
    w.setBlock(x, y, z, kilnId(), 2, SetFlags::Urgent);
    e = kilnEnt(w, x, y, z);
    if (!e) return false;
  }
  if (e->value("straw", 0) > 0 || e->value("lit", false)) return false;
  json& slot = (*e)["items"][q];
  ItemStack in = slot.is_null() ? ItemStack() : ItemStack::fromJson(slot);
  if (!in.empty() && !in.canMerge(held)) return false;
  const int room = in.empty() ? held.n : in.maxStack() - in.n;
  if (room <= 0) return false;
  if (in.empty()) { in = held; in.n = 0; }
  const int k = std::min(room, held.n);
  in.n += k; held.n -= k; if (held.n <= 0) held = ItemStack();
  slot = in.toJson();
  reheight(w, x, y, z, *e);
  w.blockEntityChanged(x, y, z);
  return true;
}

bool kilnAdd(World& w, int x, int y, int z, ItemStack& held, std::string* why) {
  json* e = kilnEnt(w, x, y, z);
  if (!e || held.empty() || e->value("lit", false)) return false;
  int straw = e->value("straw", 0), logs = e->value("logs", 0);
  if (held.id == "straw") {
    if (straw >= KILN_STRAW) { if (why) *why = "It has all the straw it needs: now logs"; return false; }
    (*e)["straw"] = straw + 1;
  } else if (reg().itemMatches(held.id, "#log")) {
    if (straw < KILN_STRAW) { if (why) *why = "Straw first: eight of it"; return false; }
    if (logs >= KILN_LOGS) { if (why) *why = "It's ready: light it with a firestarter"; return false; }
    (*e)["logs"] = logs + 1;
  } else return false;
  if (--held.n <= 0) held = ItemStack();
  reheight(w, x, y, z, *e);
  w.blockEntityChanged(x, y, z);
  return true;
}

ItemStack kilnTake(World& w, int x, int y, int z, int q) {
  json* e = kilnEnt(w, x, y, z);
  if (!e || e->value("lit", false)) return {};
  auto& items = (*e)["items"];
  std::vector<int> order = q >= 0 && q < 4 && !items[q].is_null() ? std::vector<int>{q} : std::vector<int>{3, 2, 1, 0};
  for (int i : order) {
    if (items[i].is_null()) continue;
    ItemStack s = ItemStack::fromJson(items[i]); items[i] = nullptr;
    bool any = false; for (auto& it : items) if (!it.is_null()) any = true;
    if (!any) { const int straw = e->value("straw", 0), logs = e->value("logs", 0); w.setBlockEntity(x, y, z, nullptr); w.setBlock(x, y, z, AIR, 0, SetFlags::Urgent); (void)straw; (void)logs; }
    else { reheight(w, x, y, z, *e); w.blockEntityChanged(x, y, z); }
    return s;
  }
  return {};
}

bool kilnWalled(World& w, int x, int y, int z) {
  return solidAt(w, x + 1, y, z) && solidAt(w, x - 1, y, z) && solidAt(w, x, y, z + 1) && solidAt(w, x, y, z - 1) && solidAt(w, x, y - 1, z);
}

bool kilnLight(Level& lv, World& w, int x, int y, int z) {
  json* e = kilnEnt(w, x, y, z);
  if (!e || e->value("lit", false) || e->value("straw", 0) < KILN_STRAW || e->value("logs", 0) < KILN_LOGS || !kilnWalled(w, x, y, z)) return false;
  (*e)["lit"] = true; (*e)["t"] = 0.0;
  litList(lv).push_back({w.realm, x, y, z});
  w.blockEntityChanged(x, y, z);
  return true;
}

void pitKilnTick(Level& lv, double dt) {
  if (lv.isRemote()) return;
  json& list = litList(lv);
  if (list.empty()) return;
  const double need = gameHourSecs(lv) * KILN_HOURS;
  for (size_t i = 0; i < list.size();) {
    const json& k = list[i];
    World* w = lv.worldIfOpen(k[0].get<int>());
    const int x = k[1], y = k[2], z = k[3];
    if (!w || !w->isLoaded(x, y, z)) { i++; continue; }
    json* e = kilnEnt(*w, x, y, z);
    bool done = !e || !e->value("lit", false);
    if (!done) {
      if (!kilnWalled(*w, x, y, z)) { (*e)["lit"] = false; (*e)["straw"] = 0; (*e)["logs"] = 0; reheight(*w, x, y, z, *e); w->blockEntityChanged(x, y, z); done = true; }   // a side opened: out, the fuel lost
      else {
        const double t = e->value("t", 0.0) + dt;
        (*e)["t"] = t;
        if (std::fmod(t, 0.5) < dt) lv.fx->particles("flame", vec3(x + 0.5f, y + 0.6f, z + 0.5f), 2, {{"spread", 0.35}, {"vel", {0, 0.8, 0}}, {"life", 0.8}, {"size", 0.12}, {"rise", true}});
        if (t >= need) {
          for (auto& it : (*e)["items"]) {
            if (it.is_null()) continue;
            ItemStack s = ItemStack::fromJson(it);
            if (const ItemDef* d = s.def(); d && !d->str("fires").empty()) { s.id = d->str("fires"); setItemHeat(s, 600); }
            it = s.toJson();
          }
          (*e)["lit"] = false; (*e)["straw"] = 0; (*e)["logs"] = 0; (*e)["t"] = 0;
          reheight(*w, x, y, z, *e); w->blockEntityChanged(x, y, z);
          done = true;
        }
      }
    }
    if (done) list.erase(list.begin() + i); else i++;
  }
}

} // namespace dq
