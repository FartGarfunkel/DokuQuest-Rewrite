// sim/villager_jobs.cpp — each trade's work shown in the world, round its workplace (in their working hours, before the
// trade's ordinary chores: sim/villager_chores.cpp npcChore):
//   fisherman     brings the catch to a barrel by the workplace and puts it in
//   cartographer  walks out past the houses to look at the land, comes back, hangs a real filled map
//                 (tag.map -> PaperMap of the village, painted from loaded land, wider every third trip) in a frame;
//   librarian     a book and quill on the lectern; writes in it (a page a visit) about the village's days, and when it's
//                 full signs it and shelves it in the bookcase, and starts the next
//   butcher       takes one of a pen's grown animals when the pen has plenty of that kind (meat and leather to the
//                 stores); at the end of the working day sets two of a short pen to breeding
//   shepherd      walks to the sheep with shears and shears the ones with wool (wool to the stores)
//   cleric        keeps the brewing stand by the chapel going: water bottles, the ingredient, potions taken off
//   armorer / weaponsmith / toolsmith / leatherworker
//                 an armour stand by the workplace dressed in their work (leather dyed by the leatherworker), changed
//                 now and then; the weapon- and toolsmith forge at the anvil and hang their work on a wall rack
//   mason         sets one fine block by the stonecutter, and another in its place now and then
//   fletcher      a target set out before the workplace and a frame of arrows on the wall; shoots at the target
// What they set out is remembered with the village (villageRec(...)["show"][i]) so it's the same pieces each day.
#include "sim/villagers_internal.h"
#include "sim/maps.h"
#include "sim/creature.h"
#include "sim/creature_entities.h"
#include "sim/brewing.h"
#include "core/registry.h"
#include "world/world.h"
#include "world/dirs.h"

namespace dq {
json* containerEntity(World& w, int x, int y, int z);   // sim/build.cpp

namespace {
float rnd01() { return frand(); }
void stockAdd(json& S, const std::string& k, int n) { if (!k.empty()) S[k] = std::max(0, S.value(k, 0) + n); }
bool by(const Creature& e, vec3 at, float r) { return std::hypot(at.x - e.pos.x, at.z - e.pos.z) < r && std::fabs(at.y - e.pos.y) < 2.6f; }
const VBld* nearestOf(const Village& v, const char* type, vec3 to, const char* animals, bool notAnimals) {
  const VBld* best = nullptr; float bd = 1e9f;
  for (const VBld& b : v.blds) if (b.type == type) {
    if (animals && ((b.animals == animals) == notAnimals)) continue;
    const float d = std::hypot((b.x0 + b.x1) * 0.5f - to.x, (b.z0 + b.z1) * 0.5f - to.z);
    if (d < bd) { bd = d; best = &b; }
  }
  return best;
}
BlockId B(const char* k) { return reg().blockId(k); }
bool freeCell(World& w, int x, int y, int z) {
  const Registry& R = reg();
  const BlockId a = w.getBlock(x, y, z), b = w.getBlock(x, y + 1, z), g = w.getBlock(x, y - 1, z);
  return (a == AIR || R.tReplaceable[a]) && (b == AIR || R.tReplaceable[b]) && R.tSolid[g] && R.tOpaque[g] && !w.blockEntity(x, y, z);
}
// a free cell by the workplace (not the work itself, not in front of it where they stand), on the ground
bool spotBy(World& w, ivec3 work, int workF, int r, ivec3& out, int skip = 0) {
  const ivec3 stand = work + ivec3(DIRX[workF & 3], 0, DIRZ[workF & 3]);
  int found = 0;
  for (int d = 1; d <= r; d++) for (int dz = -d; dz <= d; dz++) for (int dx = -d; dx <= d; dx++) {
    if (std::max(std::abs(dx), std::abs(dz)) != d) continue;
    for (int dy = -1; dy <= 1; dy++) {
      const ivec3 c = work + ivec3(dx, dy, dz);
      if (c == stand || c == work || !freeCell(w, c.x, c.y, c.z)) continue;
      if (found++ < skip) continue;
      out = c; return true;
    }
  }
  return false;
}
// a wall cell by the workplace to hang something on: air with a solid wall beside it; f: the way to the wall
bool wallBy(World& w, ivec3 work, int r, ivec3& out, int& f) {
  const Registry& R = reg();
  for (int d = 1; d <= r; d++) for (int dz = -d; dz <= d; dz++) for (int dx = -d; dx <= d; dx++) {
    if (std::max(std::abs(dx), std::abs(dz)) != d) continue;
    for (int dy = 0; dy <= 1; dy++) {
      const ivec3 c = work + ivec3(dx, dy, dz);
      const BlockId a = w.getBlock(c.x, c.y, c.z);
      if (a != AIR || w.blockEntity(c.x, c.y, c.z)) continue;
      for (int k = 0; k < 4; k++) {
        const BlockId wl = w.getBlock(c.x + DIRX[k], c.y, c.z + DIRZ[k]);
        if (R.tSolid[wl] && R.tOpaque[wl]) { out = c; f = k; return true; }
      }
    }
  }
  return false;
}
json& showRec(Level& lv, const Village& v, int idx) {
  json& s = villageRec(lv, v.id)["show"];
  if (!s.is_object()) s = json::object();
  json& r = s[std::to_string(idx)];
  if (!r.is_object()) r = json::object();
  return r;
}
// a piece they set out before (still there), or a new one put down: its place in `at`
bool piece(Level& lv, World& w, json& rec, const char* key, const char* block, ivec3 work, int workF, ivec3& at, bool wall = false) {
  const BlockId id = B(block);
  if (!id) return false;
  if (rec.contains(key) && rec[key].is_array()) {
    at = ivec3(rec[key][0].get<int>(), rec[key][1].get<int>(), rec[key][2].get<int>());
    if (w.isLoaded(at.x, at.y, at.z) && w.getBlock(at.x, at.y, at.z) == id) return true;
    if (!w.isLoaded(at.x, at.y, at.z)) return false;
  }
  int f = 0;
  if (wall ? !wallBy(w, work, 3, at, f) : !spotBy(w, work, workF, 3, at, (int)rec.size())) return false;
  w.setBlock(at.x, at.y, at.z, id, (u8)(wall ? f : (workF & 3)));
  rec[key] = {at.x, at.y, at.z};
  lv.fx->material("wood", "place", vec3(at) + vec3(0.5f));
  return true;
}
std::string pick(const std::vector<std::string>& L) { return L[(size_t)(rnd01() * L.size()) % L.size()]; }
}  // namespace

// the work round the workplace: a move (walking there, or doing it) or nothing (the ordinary chores go on)
OptMove npcJobWork(Level& lv, Creature& e, NpcMind& M, const Village& v, const VFolk& f, int idx, float ph, float dt) {
  if (lv.isRemote() || !f.hasWork) return std::nullopt;
  World& w = lv.world(e.realm);
  const std::string& job = f.job;
  if (!w.isLoaded(f.work.x, f.work.y, f.work.z)) return std::nullopt;
  json& S = villageStock(lv, v);
  json& rec = showRec(lv, v, idx);
  const float speed = e.T->speed * 0.45f;
  const vec3 workAt = vec3(f.work) + vec3(0.5f, 0, 0.5f);
  auto go = [&](vec3 at) -> OptMove { OptMove m = npcGo(lv, e, M, at, dt, speed); return m ? m : OptMove(Move{speed, std::atan2(-(at.x - e.pos.x), -(at.z - e.pos.z))}); };
  auto face = [&](vec3 at) { e.yaw = std::atan2(-(at.x - e.pos.x), -(at.z - e.pos.z)); };
  auto atPiece = [&](ivec3 p, OptMove& out) -> bool {   // walk up to it; true once there
    const vec3 c = vec3(p) + vec3(0.5f, 0, 0.5f);
    if (!by(e, c, 1.7f)) { out = go(c); return false; }
    face(c); return true;
  };
  float& T = M.jobT;
  OptMove mv;

  // ---- the fisherman: the catch to the barrel
  if (job == "fisherman") {
    if (S.value("cod", 0) + S.value("salmon", 0) < 4 || rec.value("stored", 0.0) > npcPhase(lv) - 0.05) return std::nullopt;
    ivec3 bp; if (!piece(lv, w, rec, "barrel", "barrel", f.work, f.workF, bp)) return std::nullopt;
    if (!atPiece(bp, mv)) return mv;
    json* be = containerEntity(w, bp.x, bp.y, bp.z);
    if (be && (*be)["items"].is_array()) {
      json& it = (*be)["items"];
      for (const char* fish : {"cod", "salmon"}) {
        int n = std::min(S.value(fish, 0), 8);
        for (auto& s : it) {
          if (n <= 0) break;
          ItemStack st = ItemStack::fromJson(s);
          if (st.empty()) { s = ItemStack(fish, n).toJson(); n = 0; }
          else if (st.id == fish && st.n < 64) { const int m = std::min(n, 64 - st.n); st.n += m; n -= m; s = st.toJson(); }
        }
        stockAdd(S, fish, -(std::min(S.value(fish, 0), 8) - n));
      }
      w.blockEntityChanged(bp.x, bp.y, bp.z);
    }
    lv.fx->sound("chest_close", vec3(bp) + vec3(0.5f), 10); e.swingT = 0.3f;
    rec["stored"] = npcPhase(lv);
    return Move{0, e.yaw};
  }

  // ---- the cartographer: out to look at the land, back, a real map in the frame (tag.map -> PaperMap; the frame draws it)
  if (job == "cartographer") {
    MapStore& maps = mapStore(lv);
    auto survey = [&]() -> PaperMap* {
      std::string id = rec.value("map", "");
      PaperMap* m = id.empty() ? nullptr : maps.get(id);
      if (!m) {
        id = newId("vm");
        m = maps.create(id, jsDimOf(e), (double)v.x + 0.5, (double)v.z + 0.5, 0);
        rec["map"] = id;
        rec["mc"] = 0;
      }
      int cursor = rec.value("mc", 0);
      mapPaintLoaded(lv, *m, cursor, std::max(8, (int)(dt * 1600)));
      rec["mc"] = cursor;
      return m;
    };
    PaperMap* m = survey();
    int stage = rec.value("st", 0);
    if (stage == 0) {   // out
      if (!rec.contains("out")) { const float a = rnd01() * 6.2831853f; rec["out"] = {v.x + std::cos(a) * (v.r + 10), v.z + std::sin(a) * (v.r + 10)}; }
      const vec3 out((float)rec["out"][0].get<double>() + 0.5f, e.pos.y, (float)rec["out"][1].get<double>() + 0.5f);
      if (std::hypot(out.x - e.pos.x, out.z - e.pos.z) > 2.5f && M.stuckGo < 8) return go(out);
      T += dt; M.pose |= NP_READ;   // looking at the land, drawing
      if (T < 6) return Move{0, e.yaw};
      T = 0; rec["st"] = 1; rec.erase("out"); M.stuckGo = 0;
      return Move{0, e.yaw};
    }
    ivec3 fp; if (!piece(lv, w, rec, "frame", "item_frame", f.work, f.workF, fp, true)) { rec["st"] = 0; return std::nullopt; }
    if (!atPiece(fp, mv)) return mv;
    const int n = rec.value("maps", 0) + 1; rec["maps"] = n;
    if (m && n % 3 == 0 && m->scale < MAP_MAX_SCALE && m->explored() > 0.08f) {
      PaperMap* z = maps.zoomed(*m);   // twice the land; the old sheet stays, the frame takes the wider one
      rec["map"] = z->id; rec["mc"] = 0;
      int cursor = 0;
      mapPaintLoaded(lv, *z, cursor, 400);
      rec["mc"] = cursor;
      m = z;
    } else if (m) {
      int cursor = rec.value("mc", 0);
      mapPaintLoaded(lv, *m, cursor, 400);
      rec["mc"] = cursor;
    }
    ItemStack stack("filled_map", 1);
    stack.tag = json{{"map", m ? m->id : ""}, {"name", "Map of " + v.name + " (" + std::to_string(n) + ")"}};
    w.setBlockEntity(fp.x, fp.y, fp.z, {{"type", "frame"}, {"item", stack.toJson()}, {"rot", 0}});
    lv.fx->sound("map_paper", vec3(fp) + vec3(0.5f), 10); e.swingT = 0.3f;
    npcSay(lv, e, n == 1 ? "There. Our corner of the world." : "A little more of the world on it.", 0.35f);
    rec["st"] = 0;
    return std::nullopt;   // (the rest of the day at the table)
  }

  // ---- the librarian: writing in the book on the lectern, shelving it when it's full
  if (job == "librarian") {
    ivec3 lp; if (!piece(lv, w, rec, "lectern", "lectern", f.work, f.workF, lp)) return std::nullopt;
    if (!atPiece(lp, mv)) return mv;
    json* le = w.blockEntity(lp.x, lp.y, lp.z);
    if (!le || !le->is_object() || !le->contains("stack")) {   // a fresh book and quill on it
      ItemStack b("writable_book", 1); b.tag = json{{"pages", json::array()}};
      w.setBlockEntity(lp.x, lp.y, lp.z, {{"type", "lectern"}, {"book", true}, {"stack", b.toJson()}, {"page", 0}});
      le = w.blockEntity(lp.x, lp.y, lp.z);
    }
    T += dt; M.pose |= NP_READ;
    if (T < 9) return Move{0, e.yaw};
    T = 0;
    ItemStack b = ItemStack::fromJson((*le)["stack"]);
    json& pg = b.tag["pages"]; if (!pg.is_array()) pg = json::array();
    static const std::vector<std::string> LINES = {
      "Day {d}. The bread was good and the wind was kind to the mill.", "Day {d}. {a} and {b} argued again by the well. Over goats, I am told.",
      "Day {d}. A traveller came through with strange buttons for coin.", "Day {d}. The golem walked the wall all night. Nothing came.",
      "Day {d}. The children's lessons went well. They can all count to twenty now, most of them.", "Day {d}. Rain in the morning, sun after. The crops drank.",
      "Day {d}. The smith's hammer rang till dusk. New tools on the rack.", "Day {d}. {a} brought {b} flowers. The whole village noticed.",
      "Day {d}. The fishing was poor. The fisherman blames the moon.", "Day {d}. Someone has been seen at the edge of the woods. We keep the gates."};
    std::string line = pick(LINES);
    auto rep = [&](const std::string& k, const std::string& val) { for (size_t q; (q = line.find(k)) != std::string::npos;) line.replace(q, k.size(), val); };
    rep("{d}", std::to_string(npcDay(lv)));
    rep("{a}", npcPersonName(lv, v, (int)(rnd01() * v.folk.size())));
    rep("{b}", npcPersonName(lv, v, (int)(rnd01() * v.folk.size())));
    pg.push_back(line);
    lv.fx->sound("map_paper", vec3(lp) + vec3(0.5f, 1, 0.5f), 8);
    if ((int)pg.size() >= 6) {   // full: signed, onto the shelf
      const int n = rec.value("books", 0) + 1; rec["books"] = n;
      ItemStack done("written_book", 1); done.tag = json{{"title", "Chronicle of " + v.name + " " + std::to_string(n)}, {"author", npcPersonName(lv, v, idx)}, {"pages", pg}};
      ivec3 sp; bool shelved = false;
      if (piece(lv, w, rec, "case", "oak_bookcase", f.work, f.workF, sp)) {
        json* be = containerEntity(w, sp.x, sp.y, sp.z);
        if (be && (*be)["items"].is_array()) for (auto& s : (*be)["items"]) if (s.is_null()) { s = done.toJson(); shelved = true; w.blockEntityChanged(sp.x, sp.y, sp.z); break; }
        if (!shelved && be && (*be)["items"].is_array() && !(*be)["items"].empty()) { (*be)["items"][n % (*be)["items"].size()] = done.toJson(); w.blockEntityChanged(sp.x, sp.y, sp.z); }   // (a full case: an old one makes room)
      }
      npcSay(lv, e, "Another one done. On the shelf with you.", 0.35f);
      ItemStack nb("writable_book", 1); nb.tag = json{{"pages", json::array()}};
      (*le)["stack"] = nb.toJson(); (*le)["page"] = 0;
    } else { (*le)["stack"] = b.toJson(); (*le)["page"] = ((int)pg.size() - 1) / 2; }
    w.blockEntityChanged(lp.x, lp.y, lp.z);
    return Move{0, e.yaw};
  }

  // ---- the butcher: the pens kept right
  if (job == "butcher" || job == "shepherd") {
    const bool sheep = job == "shepherd";
    const VBld* pen = sheep ? nearestOf(v, "pen", workAt, "sheep", false) : nearestOf(v, "pen", workAt, nullptr, false);
    if (!pen) return std::nullopt;
    if (T > 0) { T -= dt; return std::nullopt; }
    // the grown ones in the pen
    std::map<std::string, std::vector<Creature*>> kinds;
    for (auto& up : lv.entities()) if (auto* c = dynamic_cast<Creature*>(up.get())) {
      if (c->removed || !c->alive() || c->baby || npcOf(*c) || c->realm != e.realm) continue;
      if (c->pos.x < std::min(pen->x0, pen->x1) - 0.5f || c->pos.x > std::max(pen->x0, pen->x1) + 1.5f || c->pos.z < std::min(pen->z0, pen->z1) - 0.5f || c->pos.z > std::max(pen->z0, pen->z1) + 1.5f) continue;
      kinds[c->type].push_back(c);
    }
    if (sheep) {   // shear one with its wool
      Creature* s = nullptr;
      for (Creature* c : kinds["sheep"]) if (!c->sheared) { s = c; break; }
      if (!s) { T = 8; return std::nullopt; }
      e.heldItem = "shears";
      if (!by(e, s->pos, 1.6f)) return go(s->pos);
      face(s->pos); s->sheared = true; e.swingT = 0.3f;
      stockAdd(S, "wool", 1 + (int)(rnd01() * 3));
      lv.fx->sound("shear", s->pos, 12);
      lv.fx->particles("burst", s->pos + vec3(0, 0.8f, 0), 6, json{{"block", "white_wool"}, {"speed", 0.3}});
      T = 4;
      return Move{0, e.yaw};
    }
    if (ph > 0.26f) {   // the day's end: two of a short pen to breeding
      for (auto& [k, L] : kinds) if (L.size() >= 2 && L.size() < 5) { L[0]->loveT = 30; L[1]->loveT = 30; lv.fx->particles("heart", L[0]->pos + vec3(0, 1, 0), 3); }
      T = 60; return std::nullopt;
    }
    static const std::map<std::string, std::pair<const char*, const char*>> MEAT = {{"cow", {"beef", "leather"}}, {"pig", {"porkchop", ""}}, {"sheep", {"mutton", "white_wool"}}, {"chicken", {"chicken", "feather"}}, {"rabbit", {"rabbit", "rabbit_hide"}}};
    for (auto& [k, L] : kinds) {
      auto it = MEAT.find(k);
      if (it == MEAT.end() || L.size() < 4) continue;   // (enough left of each kind)
      Creature* c = L.back();
      if (!by(e, c->pos, 1.7f)) return go(c->pos);
      face(c->pos); e.swingT = 0.3f;
      c->removed = true;
      stockAdd(S, it->second.first, 1 + (int)(rnd01() * 2));
      if (*it->second.second) stockAdd(S, it->second.second, 1);
      lv.fx->particles("burst", c->pos + vec3(0, 0.5f, 0), 5, json{{"block", "red_wool"}, {"speed", 0.25}});
      T = 40;
      return Move{0, e.yaw};
    }
    T = 20;
    return std::nullopt;
  }

  // ---- the cleric: the brewing stand going
  if (job == "cleric") {
    if (T > 0) { T -= dt; return std::nullopt; }
    ivec3 bp; if (!piece(lv, w, rec, "stand", "brewing_stand", f.work, f.workF, bp)) return std::nullopt;
    if (!atPiece(bp, mv)) return mv;
    json& be = brewEntity(w, bp.x, bp.y, bp.z);
    json& it = be["items"];
    bool changed = false;
    for (int k = 0; k < 3; k++) {   // finished ones off, water bottles on
      ItemStack s = ItemStack::fromJson(it[k]);
      if (!s.empty() && s.id != "potion_water" && s.id != "potion_awkward") { stockAdd(S, s.id, 1); it[k] = nullptr; changed = true; }
      if (it[k].is_null()) { it[k] = ItemStack("potion_water", 1).toJson(); changed = true; }
    }
    if (it[3].is_null()) { it[3] = ItemStack(rnd01() < 0.5f ? "nether_wart" : "glistering_melon_slice", 1).toJson(); changed = true; }
    if (it[4].is_null() && reg().item("cinder_powder")) { it[4] = ItemStack("cinder_powder", 4).toJson(); changed = true; }
    if (changed) w.blockEntityChanged(bp.x, bp.y, bp.z);
    e.swingT = 0.3f; lv.fx->sound("bottle_fill", vec3(bp) + vec3(0.5f), 8);
    T = 25;
    return Move{0, e.yaw};
  }

  // ---- the smiths and the leatherworker: an armour stand dressed in their work; the rack
  if (job == "armorer" || job == "weaponsmith" || job == "toolsmith" || job == "leatherworker") {
    if (T > 0) { T -= dt; return std::nullopt; }
    ivec3 sp; if (!piece(lv, w, rec, "stand", "armor_stand", f.work, f.workF, sp)) return std::nullopt;
    if (!atPiece(sp, mv)) return mv;
    static const char* MAT[] = {"iron", "chainmail", "gold", "diamond", "copper"};
    const int n = rec.value("dressed", 0) + 1; rec["dressed"] = n;
    json items = json::array();
    if (job == "leatherworker") {
      static const u32 DYES[] = {0xA06540, 0x3C44AA, 0xB02E26, 0x5E7C16, 0x835432, 0xF9801D, 0x169C9C, 0x8932B8};
      const u32 dye = DYES[n % 8];
      for (const char* p : {"leather_helmet", "leather_chestplate", "leather_leggings", "leather_boots"}) { ItemStack s(p, 1); s.tag = json{{"color", (int)dye}}; items.push_back(s.toJson()); }
      items.push_back(nullptr);
    } else {
      std::string m = MAT[n % 5]; if (!reg().item(m + "_helmet")) m = "iron";
      for (const char* p : {"_helmet", "_chestplate", "_leggings", "_boots"}) items.push_back(job == "armorer" && reg().item(m + p) ? ItemStack(m + p, 1).toJson() : json());
      static const char* WEAP[] = {"iron_sword", "iron_axe", "stone_sword", "diamond_sword", "gold_sword"};
      static const char* TOOL[] = {"iron_pickaxe", "iron_shovel", "iron_hoe", "iron_axe", "stone_pickaxe"};
      items.push_back(job == "weaponsmith" ? ItemStack(WEAP[n % 5], 1).toJson() : job == "toolsmith" ? ItemStack(TOOL[n % 5], 1).toJson() : json());
      // the forged work on the wall rack too
      if (job != "armorer") {
        ivec3 rp;
        if (piece(lv, w, rec, "rack", "wall_weapon_rack", f.work, f.workF, rp, true)) {
          json ra = json::array();
          for (int k = 0; k < 3; k++) ra.push_back(ItemStack(job == "weaponsmith" ? WEAP[(n + k + 1) % 5] : TOOL[(n + k + 1) % 5], 1).toJson());
          w.setBlockEntity(rp.x, rp.y, rp.z, {{"type", "rack"}, {"rack", ra}});
        }
      }
    }
    w.setBlockEntity(sp.x, sp.y, sp.z, {{"type", "stand"}, {"items", items}, {"pose", n % 5}});
    lv.fx->sound("equip", vec3(sp) + vec3(0.5f, 1, 0.5f), 10); e.swingT = 0.3f;
    T = 90 + rnd01() * 60;   // (a new set every so often)
    return Move{0, e.yaw};
  }

  // ---- the mason: a fine block by the stonecutter, changed now and then
  if (job == "mason") {
    if (T > 0) { T -= dt; return std::nullopt; }
    static const char* FINE[] = {"chiseled_stone_bricks", "polished_andesite", "polished_granite", "polished_diorite", "mossy_stone_bricks", "chiseled_sandstone", "quartz_pillar", "smooth_stone"};
    const int n = rec.value("blocks", 0) + 1; rec["blocks"] = n;
    ivec3 at;
    if (rec.contains("fine") && rec["fine"].is_array()) at = ivec3(rec["fine"][0].get<int>(), rec["fine"][1].get<int>(), rec["fine"][2].get<int>());
    else if (!spotBy(w, f.work, f.workF, 2, at, 1)) return std::nullopt;
    if (!atPiece(at, mv)) return mv;
    const BlockId id = B(FINE[n % 8]);
    if (!id) return std::nullopt;
    const BlockId was = w.getBlock(at.x, at.y, at.z);
    if (was != AIR && !rec.contains("fine")) return std::nullopt;
    w.setBlock(at.x, at.y, at.z, id, 0);
    rec["fine"] = {at.x, at.y, at.z};
    lv.fx->material("stone", "place", vec3(at) + vec3(0.5f)); e.swingT = 0.3f;
    T = 120 + rnd01() * 80;
    return Move{0, e.yaw};
  }

  // ---- the fletcher: the target, a frame of arrows, a few shots
  if (job == "fletcher") {
    ivec3 tp;
    if (!rec.contains("target")) {   // set out before the workplace, five or six ahead in a line, on open ground
      const int fdir = f.workF & 3;
      bool ok = false;
      for (int d = 6; d >= 4 && !ok; d--) { const ivec3 c = f.work + ivec3(DIRX[fdir] * d, 0, DIRZ[fdir] * d); for (int dy = -1; dy <= 1 && !ok; dy++) if (freeCell(w, c.x, c.y + dy, c.z)) { tp = c + ivec3(0, dy, 0); ok = true; } }
      if (!ok) return std::nullopt;
      w.setBlock(tp.x, tp.y, tp.z, B("target"), 0);
      rec["target"] = {tp.x, tp.y, tp.z};
    }
    tp = ivec3(rec["target"][0].get<int>(), rec["target"][1].get<int>(), rec["target"][2].get<int>());
    if (w.getBlock(tp.x, tp.y, tp.z) != B("target")) { rec.erase("target"); return std::nullopt; }
    ivec3 fp;
    if (piece(lv, w, rec, "frame", "item_frame", f.work, f.workF, fp, true) && !w.blockEntity(fp.x, fp.y, fp.z)) {
      static const char* ARR[] = {"arrow", "spectral_arrow", "tipped_arrow"};
      w.setBlockEntity(fp.x, fp.y, fp.z, {{"type", "frame"}, {"item", ItemStack(reg().item(ARR[rec.value("shots", 0) % 3]) ? ARR[rec.value("shots", 0) % 3] : "arrow", 1).toJson()}, {"rot", 0}});
    }
    if (!by(e, workAt, 2.2f)) return go(workAt);
    const vec3 tc = vec3(tp) + vec3(0.5f);
    face(tc);
    e.heldItem = "bow";
    T += dt;
    if (T < 2.5f) return Move{0, e.yaw};
    T = 0;
    const vec3 from = e.pos + vec3(0, 1.5f, 0), d = tc - from;
    const float dist = glm::length(d);
    vec3 vel = glm::normalize(d) * 1.6f; vel.y += dist * 0.012f;   // (a little lift for the drop)
    spawnArrow(lv, e.realm, from + glm::normalize(d) * 0.6f, vel, &e, 0, false);
    lv.fx->sound("bow", from, 12); e.swingT = 0.3f;
    rec["shots"] = rec.value("shots", 0) + 1;
    if (rec.value("shots", 0) % 12 == 0) if (w.blockEntity(fp.x, fp.y, fp.z)) w.setBlockEntity(fp.x, fp.y, fp.z, nullptr);   // (the frame's arrow changed next time)
    return Move{0, e.yaw};
  }
  return std::nullopt;
}

} // namespace dq
