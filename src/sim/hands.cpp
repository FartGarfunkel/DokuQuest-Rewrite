// sim/hands.cpp — see hands-on.h
#include "sim/hands.h"
#include "sim/skills.h"
#include "sim/blockinfo.h"
#include "sim/worldsys.h"
#include "sim/gameplay.h"
#include "sim/player.h"
#include "sim/controller.h"
#include "core/registry.h"
#include "world/world.h"
#include "sim/anvil_work.h"
#include "sim/station_work.h"
#include "sim/brewing.h"
#include "sim/magic_combat.h"
#include "sim/enchanting.h"
#include "sim/stations.h"
#include "sim/slot_work.h"

namespace dq {

int handsFacing(World& w, ivec3 at) {
  const BlockDef& b = reg().block(w.getBlock(at.x, at.y, at.z));
  const u8 m = w.getMeta(at.x, at.y, at.z);
  static const int FM[4] = {2, 1, 0, 3};   // a turned model's meta (north, east, south, west)
  if (b.props.contains("tex") && b.props["tex"].is_object() && b.props["tex"].contains("front")) return m & 3;
  if (b.has("facingModel")) return b.key == "brewing_stand" || b.key == "enchanting_table" ? (FM[m & 3] + 2) & 3 : FM[m & 3];   // (the still's book, the table's socket: toward whoever placed it)
  return 2;
}
float handsFacingDegrees(int f) { static const float D[4] = {180.f, -90.f, 0.f, 90.f}; return D[f & 3]; }
vec3 handsNorthLocal(int f, vec3 p) {   // (the hands-on unrotate: the turn undone about the middle)
  const float a = -glm::radians(handsFacingDegrees(f)), c = std::cos(a), s = std::sin(a);
  const float x = p.x - 0.5f, z = p.z - 0.5f;
  return vec3(0.5f + x * c + z * s, p.y, 0.5f - x * s + z * c);
}
vec3 handsFromNorth(int f, vec3 p) {
  const float a = glm::radians(handsFacingDegrees(f)), c = std::cos(a), s = std::sin(a);
  const float x = p.x - 0.5f, z = p.z - 0.5f;
  return vec3(0.5f + x * c + z * s, p.y, 0.5f - x * s + z * c);
}

bool handsDoorOpen(World& w, ivec3 at) {
  const json* e = w.blockEntity(at.x, at.y, at.z);
  return e && e->is_object() && e->value("door", false);
}

namespace {
bool inBox(vec3 n, const float b[6]) {   // (pixels, the hands-on boxes)
  auto within = [](float v, float lo, float hi) { return v >= lo / 16.f - 1e-4f && v <= hi / 16.f + 1e-4f; };
  return within(n.x, b[0], b[3]) && within(n.y, b[1], b[4]) && within(n.z, b[2], b[5]);
}
// (FumoirBlock, HautFourneauBlock: the door's box shut and open, the mould's)
const float SMOKER_DOOR_SHUT[6] = {3, 8, -1, 13, 12, 2}, SMOKER_DOOR_OPEN[6] = {-6, 8, -4, 4, 12, 2};
const float BLAST_DOOR_SHUT[6] = {3, 14, 3, 13, 22, 7}, BLAST_DOOR_OPEN[6] = {-2, 14, -3, 5, 22, 6}, BLAST_MOLD[6] = {5, 10, -4, 11, 13, 2};
}  // namespace

bool handsFurnaceUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held, bool all) {
  const ivec3 at = hit.block;
  const BlockInfo& bi = binfo(w.getBlock(at.x, at.y, at.z));
  const std::string kind = bi.furnace;
  if (kind != "furnace" && kind != "smoker" && kind != "blast") return false;
  json* e = containerEntity(w, at.x, at.y, at.z);
  if (!e) return false;
  if (!e->contains("items") || !(*e)["items"].is_array()) (*e)["items"] = json::array({nullptr, nullptr, nullptr});
  while ((*e)["items"].size() < 9) (*e)["items"].push_back(nullptr);   // (the hands-on furnace's spots: sim/worldsys.cpp tickFurnace)
  const int f = handsFacing(w, at);
  const vec3 n = handsNorthLocal(f, hit.point - vec3(at));
  const vec3 c = vec3(at) + vec3(0.5f);
  auto changed = [&] { w.blockEntityChanged(at.x, at.y, at.z); };
  // the door (the smoker's on its front, the blast furnace's hatch on top): used, it opens or shuts
  if (kind != "furnace") {
    const bool open = e->value("door", false);
    const float* box = kind == "smoker" ? (open ? SMOKER_DOOR_OPEN : SMOKER_DOOR_SHUT) : (open ? BLAST_DOOR_OPEN : BLAST_DOOR_SHUT);
    if (inBox(n, box)) {
      (*e)["door"] = !open; changed();
      sfxAt(lv, open ? "door_close" : "door_open", c);
      return true;
    }
  }
  json& items = (*e)["items"];
  auto slot = [&](int k) { return ItemStack::fromJson(items[k]); };
  auto take = [&](int k) {
    ItemStack s = slot(k); if (s.empty()) return false;
    items[k] = nullptr;
    static const int SPOT[9] = {0, -1, -1, 1, 2, 3, -1, -1, -1};   // (an input spot's cooking starts over)
    if (k == 0) (*e)["cook"] = 0.0;
    if (SPOT[k] >= 0 && e->contains("cooks") && (*e)["cooks"].is_array() && (int)(*e)["cooks"].size() > SPOT[k]) (*e)["cooks"][SPOT[k]] = 0.0;
    if (k == 2 || k >= 6) onSmeltedTaken(lv, p, s.id, s.n);   // its output taken by hand: Cooking / Smithing, as from its screen (sim/skills.h)
    if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 0.f); }
    changed(); sfxAt(lv, "pop", c);
    return true;
  };
  auto put = [&](int k) {   // one of what's held (all: as much as there's room for)
    ItemStack s = slot(k);
    if (!s.empty() && !(s.id == held.id && s.canMerge(held))) return false;
    const int room = s.empty() ? held.maxStack() : s.maxStack() - s.n;
    const int n = std::min(all ? room : std::min(room, 1), held.n);
    if (n <= 0) return false;
    if (s.empty()) { s = held; s.n = 0; }
    s.n += n; items[k] = s.toJson();
    if ((held.n -= n) <= 0) held = ItemStack();
    changed(); sfxAt(lv, "pop", c);
    return true;
  };
  static const int IN[4] = {0, 3, 4, 5}, OUT[4] = {2, 6, 7, 8};
  // (FourBlockEntity.placeOneIngredient: one at a time, onto the first spot that's free or holds the same, 16 a spot)
  auto putCook = [&] {
    int placed = 0;
    for (int n = 0; n < (all ? 64 : 1) && !held.empty(); n++) {
      bool ok = false;
      for (int k : IN) {
        ItemStack sl = slot(k);
        if (sl.empty()) { sl = held; sl.n = 1; items[k] = sl.toJson(); ok = true; break; }
        if (sl.id == held.id && sl.canMerge(held) && sl.n < 16) { sl.n++; items[k] = sl.toJson(); ok = true; break; }
      }
      if (!ok) break;
      if (--held.n <= 0) held = ItemStack();
      placed++;
    }
    if (placed) { changed(); sfxAt(lv, "pop", c); }
    return placed > 0;
  };
  auto takeFirst = [&](const int (&ks)[4]) { for (int k : ks) if (take(k)) return true; return false; };
  const bool doorOpen = kind == "furnace" || e->value("door", false);
  // the blast furnace's mould: its ingots taken from it
  if (kind == "blast" && inBox(n, BLAST_MOLD)) return held.empty() ? takeFirst(OUT) : false;
  const bool upper = n.y >= (kind == "blast" ? 0.6f : 0.45f);
  if (!held.empty()) {
    if (upper) {
      if (smeltResult(held.id, kind).empty()) {   // (hands-on says nothing; a word on why helps)
        if (fuelValue(held.id) <= 0) toast(lv, p, std::string("That won't cook in the ") + (kind == "blast" ? "blast furnace" : kind));
        return false;
      }
      if (!doorOpen) { toast(lv, p, kind == "blast" ? "Open its hatch first (use it)" : "Open its door first (use it)"); return true; }
      return putCook();
    }
    if (fuelValue(held.id) <= 0) return false;
    return put(1);
  }
  if (upper) {
    if (!doorOpen) return false;
    return takeFirst(OUT) || takeFirst(IN);
  }
  return take(1);
}

bool handsTrommelUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held) {
  if (binfo(w.getBlock(hit.block.x, hit.block.y, hit.block.z)).furnace != "trommel") return false;
  const bool firebox = hit.normal.y <= 0 && (hit.point.y - hit.block.y) < 0.45f;   // the opening low in the front
  const int spot = firebox ? 1 : 0;   // 0 the hopper on top, 1 the firebox
  if (held.empty()) slotTake(lv, p, w, hit.block, spot);
  else slotPut(lv, p, w, hit.block, spot, held);
  return true;
}

bool handsAnvilUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held) {
  const ivec3 at = hit.block;
  if (reg().block(w.getBlock(at.x, at.y, at.z)).str("station") != "anvil") return false;
  const std::vector<ItemStack> v = anvilWorkItems(w, at);
  const vec3 c = vec3(at) + vec3(0.5f, 1.f, 0.5f);
  if (!held.empty()) {
    if (anvilWorkPut(lv, p, w, at, v[0].empty() ? 0 : 1, held)) { if (json* e = w.blockEntity(at.x, at.y, at.z)) { (*e)["hits"] = 0; w.blockEntityChanged(at.x, at.y, at.z); } sfxAt(lv, "pop", c); }
    return true;
  }
  const int k = !v[1].empty() ? 1 : !v[0].empty() ? 0 : -1;
  if (k >= 0 && anvilWorkTake(lv, p, w, at, k)) { if (json* e = w.blockEntity(at.x, at.y, at.z)) { (*e)["hits"] = 0; w.blockEntityChanged(at.x, at.y, at.z); } sfxAt(lv, "pop", c); }
  return true;
}
bool handsAnvilStrike(Level& lv, Player& p, World& w, ivec3 at, double now) {
  const std::vector<ItemStack> v = anvilWorkItems(w, at);
  if (v[0].empty()) return false;
  json* e = w.blockEntity(at.x, at.y, at.z);
  if (!e) return false;
  const vec3 c = vec3(at) + vec3(0.5f, 1.f, 0.5f);
  if (now - e->value("lastHit", -10.0) < 0.5) return true;   // (a blow at most every half second)
  const StationResult r = anvilWorkResult(w, at);
  if (!r.ok) { sfxAt(lv, "anvil_land", c); toast(lv, p, v[1].empty() ? "Lay what goes into it beside it" : "That won't forge into anything"); return true; }
  const int req = !v[1].empty() && v[0].id == v[1].id ? 5 : std::max(2, 2 * std::max(1, r.useB));
  const int hits = e->value("hits", 0) + 1;
  (*e)["lastHit"] = now; (*e)["req"] = req; (*e)["hits"] = hits;
  w.blockEntityChanged(at.x, at.y, at.z);
  sfxAt(lv, "anvil", c); burst(lv, c, "iron_block", 5, 0.4f);
  if (hits >= req) {
    if (anvilWorkForge(lv, p, w, at, true)) { burst(lv, c, "iron_block", 18, 0.6f); if (json* e2 = w.blockEntity(at.x, at.y, at.z)) { (*e2)["hits"] = 0; w.blockEntityChanged(at.x, at.y, at.z); } }
    else (*e)["hits"] = req;
  }
  return true;
}

namespace {
float grindWear(const ItemStack& s) { return s.tag.is_object() ? s.tag.value("gw", 0.f) : 0.f; }   // (MeuleBlockEntity's wear: damage that won't grind out)
}
bool handsGrindTarget(const ItemStack& s) {
  if (s.empty()) return false;
  const ItemDef* d = reg().item(s.id);
  if (d && maxDurabilityOf(d) > 0 && s.d > (int)std::lround(grindWear(s))) return true;
  const json en = s.tag.is_object() && s.tag.contains("en") ? s.tag["en"] : json();
  return en.is_object() && !en.empty();
}
bool handsGrind(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held, double now, GrindSession& gs) {
  if (reg().block(w.getBlock(at.x, at.y, at.z)).str("station") != "grindstone") return false;
  if (!handsGrindTarget(held)) { toast(lv, p, "Hold a worn or enchanted piece to the wheel"); return true; }
  const bool broke = gs.at != at || now - gs.last > 0.35;   // (a fresh session: MeuleBlockEntity's six ticks)
  const double el = broke ? 0.2 : std::clamp(now - gs.last, 0.05, 0.4);
  if (broke) { gs = GrindSession(); gs.at = at; }
  gs.last = now;
  // the wheel turns (everyone sees it): the record's grind count goes up
  json* e = w.blockEntity(at.x, at.y, at.z, true);
  if (e) {
    if (!e->is_object() || !e->contains("type")) *e = json{{"type", "grindwork"}, {"items", json::array({nullptr, nullptr})}};
    (*e)["grind"] = e->value("grind", 0) + 1;
    w.blockEntityChanged(at.x, at.y, at.z);
  }
  const vec3 c = vec3(at) + vec3(0.5f, 0.875f, 0.5f);
  const ItemDef* d = reg().item(held.id);
  const int maxD = d ? maxDurabilityOf(d) : 0;
  const int floor = (int)std::lround(grindWear(held));
  if (maxD > 0 && held.d > floor) {
    if (!p.creative() && p.level <= 0 && p.xp <= 0) { toast(lv, p, "It takes experience to grind it back"); return true; }
    gs.repair += maxD * 0.0333 * el;
    const int heal = std::min((int)gs.repair, held.d - floor);
    if (heal > 0) {
      gs.repair -= heal; held.d -= heal;
      if (!p.creative()) {
        gs.xp += heal / 12.0;
        for (int pay = (int)gs.xp; pay > 0; pay--) {   // (experience points, down through the levels)
          if (p.xp <= 0) { if (p.level <= 0) break; p.level--; p.xp = xpForLevel(p.level); }
          p.xp--; gs.xp -= 1;
        }
      }
    }
    if (!held.tag.is_object()) held.tag = json::object();
    held.tag["gw"] = std::min<float>((float)maxD, grindWear(held) + (float)(maxD * 0.0005 * el));
    burst(lv, c, "iron_block", 2, 0.5f);
    if (broke || std::fmod(now, 0.6) < el) sfxAt(lv, "grind", c);
    return true;
  }
  gs.dis += el;   // (an enchanted piece: ten seconds at the wheel)
  burst(lv, c, "enchant", 2, 0.4f);
  if (broke || std::fmod(now, 0.6) < el) sfxAt(lv, "grind", c);
  if (gs.dis >= 10.0) {
    StationResult r = grindstoneDisenchant(&held, nullptr);
    if (r.ok) { held = r.out; if (r.xp > 0) addXP(p, r.xp); sfxAt(lv, "anvil_land", c); }
    gs.dis = 0;
  }
  return true;
}

bool handsBrewUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held, bool all) {
  const ivec3 at = hit.block;
  if (reg().block(w.getBlock(at.x, at.y, at.z)).key != "brewing_stand") return false;
  // (AlambicBlockEntity.zoneAt / bottleIndexAt: the rack left of 7.5, its spots split at 2 and 4; the dome over 0.16 up)
  const vec3 n = handsNorthLocal(handsFacing(w, at), hit.point - vec3(at));
  const float lx = n.x * 16;
  // (its recipe book, lying in front: AlambicBookLayout's buttons turn its pages)
  if (lx >= 1 && lx <= 15 && n.y * 16 <= 2.2f && n.z * 16 <= 7.6f) {
    json& e = brewEntity(w, at.x, at.y, at.z);
    const int pages = std::max(1, ((int)brewRecipes().size() + 9) / 10), page = e.value("bookPage", 0);
    const float bx = lx, bz = n.z * 16;
    auto in = [&](float x0, float z0, float x1, float z1) { return bx >= x0 - 0.4f && bx <= x1 + 0.4f && bz >= z0 - 0.4f && bz <= z1 + 0.4f; };
    int to = page;
    if (in(11.9f, 0.1f, 12.9f, 1.1f)) to = page - 1; else if (in(3.1f, 0.1f, 4.1f, 1.1f)) to = page + 1; else if (in(8.f, 0.1f, 9.f, 1.1f)) to = 0;
    else to = bx >= 8 ? page - 1 : page + 1;   // (anywhere on a half: that way)
    to = std::clamp(to, 0, pages - 1);
    if (to != page) { e["bookPage"] = to; w.blockEntityChanged(at.x, at.y, at.z); sfxAt(lv, "book_page", vec3(at) + vec3(0.5f, 0.1f, 0.3f)); }
    return true;
  }
  int slot = lx < 7.5f ? (lx < 2.f ? 0 : lx < 4.f ? 1 : 2) : n.y >= 0.16f ? 3 : 4;
  if (!held.empty()) {
    if (slot <= 2) {   // (a bottle: the spot looked at, else the first free)
      if (!brewAccepts(0, held.id)) { toast(lv, p, "Only bottles go on the rack"); return true; }
      const json& e = brewEntity(w, at.x, at.y, at.z);
      if (!ItemStack::fromJson(e["items"][slot]).empty()) for (int k = 0; k < 3; k++) if (ItemStack::fromJson(e["items"][k]).empty()) { slot = k; break; }
      brewPut(lv, p, w, at, slot, held);
      return true;
    }
    if (brewAccepts(4, held.id)) slot = 4; else if (isBrewIngredient(held.id)) slot = 3;   // (forgiving: each finds its own place)
    if (!brewAccepts(slot, held.id)) { toast(lv, p, "That won't brew"); return true; }
    if (slot == 4 && !all) { ItemStack one = held; one.n = 1; if (brewPut(lv, p, w, at, 4, one)) held.n--; if (held.n <= 0) held = ItemStack(); return true; }
    brewPut(lv, p, w, at, slot, held);
    return true;
  }
  brewTake(lv, p, w, at, slot);
  return true;
}

bool handsJukeboxPlaying(World& w, ivec3 at) {
  const json* e = w.blockEntity(at.x, at.y, at.z);
  return e && e->is_object() && e->contains("disc") && !e->value("stopped", false);
}
bool handsJukeboxUse(Level& lv, Player& p, World& w, const RayHit& hit, ItemStack& held) {
  const ivec3 at = hit.block;
  if (!reg().block(w.getBlock(at.x, at.y, at.z)).has("jukebox")) return false;
  const vec3 c = vec3(at) + vec3(0.5f, 1.f, 0.5f);
  const std::string disc = jukeboxDisc(w, at);
  if (disc.empty()) {   // (a record onto the platter, waiting)
    const ItemDef* d = held.empty() ? nullptr : reg().item(held.id);
    if (!d || !d->has("disc")) { if (!held.empty()) toast(lv, p, "Only a record goes on it"); return true; }
    w.setBlockEntity(at.x, at.y, at.z, {{"type", "jukebox"}, {"disc", held.id}, {"stopped", true}});
    if (--held.n <= 0) held = ItemStack();
    sfxAt(lv, "click", c);
    return true;
  }
  // (JukeboxBlock.isNeedleHit: on the top, the arm's strip at the left)
  const vec3 n = handsNorthLocal(handsFacing(w, at), hit.point - vec3(at));
  const bool needle = n.y * 16 >= 15.9f && n.x * 16 >= 1 && n.x * 16 <= 7 && n.z * 16 >= 6 && n.z * 16 <= 10;
  if (needle) {
    json* e = w.blockEntity(at.x, at.y, at.z);
    const bool playing = !e->value("stopped", false);
    (*e)["stopped"] = playing;
    w.blockEntityChanged(at.x, at.y, at.z);
    const ItemDef* d = reg().item(disc);
    if (lv.events) lv.events->emit("jukebox", playing ? json::array({at.x, at.y, at.z, nullptr}) : json::array({at.x, at.y, at.z, d ? d->str("disc") : disc}));
    sfxAt(lv, "click", c);
    return true;
  }
  jukeboxEject(lv, p, w, at, true);
  return true;
}

void handsGive(Level& lv, Player& p, ItemStack s) {
  if (int left = giveItem(lv, p, s); left > 0) { s.n = left; dropItem(lv, p.realm, s, p.pos + vec3(0, 1, 0), true, 0.f); }
}

} // namespace dq
