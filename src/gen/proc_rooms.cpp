// gen/proc_rooms.cpp — furnished rooms shared by crypts, mineshafts, fortresses and strongholds.
#include "gen/proc_rooms.h"
#include <algorithm>

namespace dq {

std::string procWood(const std::string& want) {
  const std::string w = want.empty() ? "oak" : want;
  return hasBlock(w + "_chair") ? w : "oak";
}

std::string pickProcKind(Mulberry32& rnd, const std::string& role, int salt) {
  (void)salt;
  if (role == "entry") return "entry";
  if (role == "boss") return rnd() < 0.5 ? "boss" : "nave";
  if (role == "spawner") {
    static const char* S[] = {"spawner", "cells", "prison", "crypt", "well"};
    return S[jsFloor(rnd() * 5)];
  }
  static const char* REST[] = {
    "hall", "loot", "library", "barracks", "dining", "chapel", "kitchen", "armoury",
    "workshop", "gallery", "shrine", "store", "sleeping", "refectory", "scriptorium",
    "vault", "office", "forge", "crypt"
  };
  return REST[jsFloor(rnd() * 19)];
}

namespace {

bool isAir(SP& P, int x, int y, int z) {
  const BlockId id = P.get(x, y, z);
  return id == AIR;
}
void putAir(SP& P, int x, int y, int z, std::string_view k, u8 meta = 0) {
  if (!P.in(x, z) || k.empty()) return;
  if (P.get(x, y, z) == AIR || P.get(x, y, z) == NOBLOCK) P.put(x, y, z, k, meta);
}
std::string furn(const ProcRoom& r, const char* piece) {
  return PLK(r.wood + "_" + piece, std::string("oak_") + piece);
}
int faceMeta(int f) { return (f + 2) & 3; }   // furniture facingModel

void wallLights(SP& P, const ProcRoom& r, Mulberry32& rnd) {
  const int y = r.y + std::min(2, r.H - 1);
  struct W { int x, z, m; };
  W w[] = {{r.x, r.z - r.rz, 3}, {r.x, r.z + r.rz, 4}, {r.x - r.rx, r.z, 1}, {r.x + r.rx, r.z, 2}};
  for (auto& t : w) if (rnd() < 0.35 + r.density * 0.5 && isAir(P, t.x, y, t.z))
    putAir(P, t.x, y, t.z, r.light, t.m);
  if (rnd() < r.density && isAir(P, r.x, r.y + r.H - 1, r.z))
    putAir(P, r.x, r.y + r.H - 1, r.z, r.hangLight, 1);
}

void clutterCorners(SP& P, const ProcRoom& r, Mulberry32& rnd) {
  for (auto [sx, sz] : {std::pair{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}) {
    const int x = r.x + sx * r.rx, z = r.z + sz * r.rz;
    if (r.webs || rnd() < 0.35) putAir(P, x, r.y + r.H - 1, z, "cobweb");
    if (r.webs && rnd() < 0.5) putAir(P, x, r.y, z, "cobweb");
    else if (rnd() < 0.4 * r.density) putAir(P, x, r.y, z, rnd() < 0.5 ? "bone_block" : "decorated_pot");
  }
}

void tableSet(SP& P, const ProcRoom& r, int x, int z, int facing) {
  putAir(P, x, r.y, z, furn(r, "table"));
  putAir(P, x + 1, r.y, z, furn(r, "chair"), faceMeta(facing));
  putAir(P, x - 1, r.y, z, furn(r, "chair"), faceMeta(facing + 2));
}

void benches(SP& P, const ProcRoom& r, int rows) {
  for (int i = 0; i < rows; i++) {
    const int z = r.z - r.rz + 2 + i * 2;
    if (z >= r.z + r.rz - 1) break;
    for (int x = r.x - r.rx + 2; x <= r.x + r.rx - 2; x += 2)
      putAir(P, x, r.y, z, furn(r, "bench"), faceMeta(0));
  }
}

} // namespace

void furnishProcRoom(SP& P, const ProcRoom& r, const ProcChestFn& chest) {
  Mulberry32 rnd(r.seed ? r.seed : 1);
  auto ch = [&](int x, int y, int z, const char* k, bool mimic) {
    if (chest) chest(x, y, z, k, mimic);
  };
  wallLights(P, r, rnd);
  clutterCorners(P, r, rnd);

  const std::string& k = r.kind;
  const int y = r.y;

  if (k == "entry") {
    putAir(P, r.x - std::min(2, r.rx - 1), y, r.z - std::min(2, r.rz - 1), "decorated_pot");
    putAir(P, r.x + 1, y, r.z, furn(r, "stool"));
    if (rnd() < r.density) putAir(P, r.x, y + std::min(2, r.H - 1), r.z, r.hangLight, 1);
  } else if (k == "hall") {
    const int px = std::min(2, std::max(1, r.rx - 1)), pz = std::min(2, std::max(1, r.rz - 1));
    for (auto [sx, sz] : {std::pair{-1, -1}, {1, -1}, {-1, 1}, {1, 1}})
      for (int yy = y; yy < y + r.H; yy++) P.put(r.x + sx * px, yy, r.z + sz * pz, r.pillar);
    if (r.rx >= 3 && r.rz >= 3) tableSet(P, r, r.x, r.z, 0);
    putAir(P, r.x, y, r.z - std::min(2, r.rz - 1), furn(r, "bench"), faceMeta(0));
    if (rnd() < 0.45) putAir(P, r.x + std::min(2, r.rx - 1), y, r.z, "brazier");
    if (rnd() < 0.5) ch(r.x, y, r.z + std::min(1, r.rz - 1), "common", rnd() < 0.15);
  } else if (k == "loot" || k == "vault") {
    ch(r.x - 1, y, r.z, k == "vault" ? "rare" : "common", rnd() < 0.15);
    ch(r.x + 1, y, r.z, rnd() < 0.4 || k == "vault" ? "rare" : "common", rnd() < 0.12);
    if (k == "vault") ch(r.x, y, r.z + 1, "rare", false);
    putAir(P, r.x, y, r.z, "decorated_pot");
    putAir(P, r.x - std::min(2, r.rx - 1), y, r.z - std::min(2, r.rz - 1), furn(r, "standing_shelf"));
    putAir(P, r.x + std::min(2, r.rx - 1), y, r.z - std::min(2, r.rz - 1), "barrel");
    if (k == "vault") {
      putAir(P, r.x, y, r.z - 1, "armor_stand");
      putAir(P, r.x, y + std::min(2, r.H - 1), r.z, "soul_lantern", 1);
    }
  } else if (k == "spawner") {
    P.put(r.x, y - 1, r.z, r.pillar);
    putAir(P, r.x - std::min(2, r.rx - 1), y, r.z, "iron_bars");
    putAir(P, r.x + std::min(2, r.rx - 1), y, r.z, "iron_bars");
    putAir(P, r.x, y, r.z - std::min(2, r.rz - 1), "bone_block");
    ch(r.x + std::min(2, r.rx - 1), y, r.z + std::min(2, r.rz - 1), "common", false);
  } else if (k == "library" || k == "scriptorium") {
    for (int zz = r.z - r.rz + 1; zz <= r.z + r.rz - 1; zz++)
      for (int yy = y; yy < y + std::min(3, r.H); yy++) {
        if (std::abs(zz - r.z) <= 1 && yy == y) continue;
        P.put(r.x - r.rx, yy, zz, PLK(furn(r, "bookcase"), "bookshelf"));
        P.put(r.x + r.rx, yy, zz, PLK(furn(r, "bookcase"), "bookshelf"));
      }
    putAir(P, r.x, y, r.z + 1, "lectern", 0);
    tableSet(P, r, r.x, r.z - 1, 2);
    putAir(P, r.x + 1, y + 1, r.z - 1, "candle");
    ch(r.x, y, r.z - std::min(2, r.rz - 1), "rare", false);
  } else if (k == "boss" || k == "nave") {
    for (auto [sx, sz] : {std::pair{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}) {
      const int px = std::min(3, r.rx - 1), pz = std::min(3, r.rz - 1);
      for (int yy = y; yy < y + r.H; yy++) P.put(r.x + sx * px, yy, r.z + sz * pz, r.pillar);
      putAir(P, r.x + sx * px - sx, y + std::min(3, r.H - 1), r.z + sz * pz, "soul_torch", sx < 0 ? 2 : 1);
    }
    for (int x = r.x - 2; x <= r.x + 2; x++) P.put(x, y, r.z + r.rz - 1, r.slab, 0);
    putAir(P, r.x, y + 1, r.z + r.rz - 1, furn(r, "chair"));   // throne
    putAir(P, r.x - 2, y, r.z, "brazier");
    putAir(P, r.x + 2, y, r.z, "brazier");
    if (hasBlock("red_banner")) putAir(P, r.x - r.rx, y + 2, r.z, "red_banner");
    if (k == "boss") {
      ch(r.x, y + 1, r.z + r.rz - 1, "boss", false);
      ch(r.x - 1, y + 1, r.z + r.rz - 1, "rare", false);
      ch(r.x + 1, y + 1, r.z + r.rz - 1, "rare", false);
    } else benches(P, r, 2);
  } else if (k == "barracks" || k == "sleeping") {
    const std::string bed = PLK("red_bed", "bed");
    putAir(P, r.x - std::min(2, r.rx - 1), y, r.z - 1, bed, 0);
    putAir(P, r.x + std::min(2, r.rx - 1), y, r.z - 1, bed, 0);
    putAir(P, r.x, y, r.z + 1, "barrel");
    putAir(P, r.x, y, r.z + 2, "armor_stand");
    putAir(P, r.x - 1, y, r.z + 1, furn(r, "chest").empty() ? "barrel" : furn(r, "cabinet"));
    ch(r.x + 1, y, r.z + 1, "common", false);
  } else if (k == "dining" || k == "refectory") {
    for (int z = r.z - std::min(2, r.rz - 2); z <= r.z + std::min(2, r.rz - 2); z++)
      putAir(P, r.x, y, z, furn(r, "table"));
    for (int z = r.z - std::min(2, r.rz - 2); z <= r.z + std::min(2, r.rz - 2); z += 1) {
      putAir(P, r.x - 1, y, z, furn(r, "bench"), faceMeta(1));
      putAir(P, r.x + 1, y, z, furn(r, "bench"), faceMeta(3));
    }
    putAir(P, r.x - std::min(2, r.rx - 1), y, r.z, "barrel");
    putAir(P, r.x + std::min(2, r.rx - 1), y, r.z, "barrel");
    putAir(P, r.x, y + std::min(2, r.H - 1), r.z, r.hangLight, 1);
  } else if (k == "chapel" || k == "shrine") {
    benches(P, r, 2);
    putAir(P, r.x, y, r.z + std::min(2, r.rz - 1), "lectern", 0);
    putAir(P, r.x - 1, y, r.z + std::min(2, r.rz - 1), "candle");
    putAir(P, r.x + 1, y, r.z + std::min(2, r.rz - 1), "candle");
    if (hasBlock("statue_stone")) putAir(P, r.x, y, r.z, "statue_stone");
    if (hasBlock("red_banner")) putAir(P, r.x - r.rx, y + 2, r.z, "red_banner");
    ch(r.x, y, r.z - std::min(2, r.rz - 1), "rare", false);
  } else if (k == "kitchen") {
    putAir(P, r.x - 1, y, r.z, PLK("smoker", "furnace"));
    putAir(P, r.x + 1, y, r.z, "cauldron");
    tableSet(P, r, r.x, r.z + 1, 0);
    putAir(P, r.x - std::min(2, r.rx - 1), y, r.z - 1, "barrel");
    putAir(P, r.x + std::min(2, r.rx - 1), y, r.z - 1, furn(r, "cabinet"));
    putAir(P, r.x, y, r.z - 1, furn(r, "stool"));
  } else if (k == "armoury") {
    putAir(P, r.x - 1, y, r.z, PLK("weapon_rack", "armor_stand"));
    putAir(P, r.x + 1, y, r.z, "armor_stand");
    putAir(P, r.x, y, r.z - 1, PLK("wall_weapon_rack", "item_frame"));
    putAir(P, r.x, y, r.z + 1, furn(r, "shelf"));
    ch(r.x, y, r.z, "rare", false);
  } else if (k == "cells" || k == "prison") {
    putAir(P, r.x - 1, y, r.z - 1, "iron_bars");
    putAir(P, r.x + 1, y, r.z - 1, "iron_bars");
    putAir(P, r.x - 1, y, r.z + 1, "iron_bars");
    putAir(P, r.x + 1, y, r.z + 1, "iron_bars");
    putAir(P, r.x, y, r.z, furn(r, "stool"));
    putAir(P, r.x, y + 1, r.z - std::min(2, r.rz - 1), "chain");
    putAir(P, r.x, y, r.z + std::min(2, r.rz - 1), "cobweb");
  } else if (k == "workshop" || k == "forge" || k == "office") {
    if (k == "forge") {
      putAir(P, r.x - 1, y, r.z, "anvil");
      putAir(P, r.x + 1, y, r.z, PLK("blast_furnace", "furnace"));
      putAir(P, r.x, y, r.z, "brazier");
      putAir(P, r.x, y, r.z + 1, PLK("bellows", "cauldron"));
    } else if (k == "office") {
      tableSet(P, r, r.x, r.z, 0);
      putAir(P, r.x, y, r.z + 1, "lectern", 0);
      putAir(P, r.x + std::min(2, r.rx - 1), y, r.z, "item_pile");
    } else {
      putAir(P, r.x, y, r.z, "crafting_table");
      putAir(P, r.x - 1, y, r.z, furn(r, "table"));
      putAir(P, r.x + 1, y, r.z, furn(r, "stool"));
      putAir(P, r.x, y, r.z + 1, "barrel");
    }
    ch(r.x, y, r.z - 1, "common", false);
  } else if (k == "crypt") {
    putAir(P, r.x - 1, y, r.z, PLK("grave", "bone_block"));
    putAir(P, r.x + 1, y, r.z, "bone_block");
    P.put(r.x, y, r.z, r.slab, 0);
    putAir(P, r.x, y + std::min(2, r.H - 1), r.z, "soul_lantern", 1);
    ch(r.x, y, r.z + 1, "rare", false);
  } else if (k == "well") {
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
      if (dx || dz) P.put(r.x + dx, y, r.z + dz, r.slab, 0);
      else P.put(r.x, y - 1, r.z, "water");
    }
    putAir(P, r.x, y + 1, r.z, r.hangLight, 1);
  } else if (k == "gallery") {
    putAir(P, r.x - std::min(2, r.rx - 1), y + 1, r.z, PLK("painting", "item_frame"));
    putAir(P, r.x + std::min(2, r.rx - 1), y + 1, r.z, PLK("item_frame", "item_frame"));
    putAir(P, r.x, y, r.z, furn(r, "bench"), faceMeta(0));
    putAir(P, r.x, y, r.z + 1, furn(r, "stool"));
    ch(r.x, y, r.z - 1, "common", false);
  } else if (k == "store") {
    for (int i = 0; i < 3; i++) {
      putAir(P, r.x - 1, y + (i > 0 ? 1 : 0), r.z - 1 + i, "barrel");
      putAir(P, r.x + 1, y, r.z - 1 + i, furn(r, "standing_shelf"));
    }
    ch(r.x, y, r.z, "common", false);
  } else {
    // unknown kind: a lived-in hall
    if (r.rx >= 3) tableSet(P, r, r.x, r.z, 0);
    if (rnd() < 0.4) ch(r.x, y, r.z + 1, "common", false);
  }
}

} // namespace dq
