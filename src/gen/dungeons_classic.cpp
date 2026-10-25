// gen/dungeons_classic.cpp — Minecraft Alpha/Beta monster rooms (WorldGenDungeons): a cobblestone room with a
// mossy floor carved where a cave passes, a monster cage in the middle (block entity {type:'spawner', mob}, the same
// shape structure cages use) and one or two chests against the walls with Beta's loot plus a few of the original's
// things. Each room lies wholly inside one chunk, so the opening test sees exactly the blocks it changes.
#include "gen/overworld.h"
#include "gen/structures_common.h"

namespace dq {

namespace {
json slot(const char* id, int n) { return hasItem(id) ? json{{"id", id}, {"n", n}} : json(); }
// WorldGenDungeons.pickCheckLootItem (Beta 1.7), and now and then something of the original's
json lootItem(JRandom& r) {
  switch (r.nextInt(14)) {
    case 0: return slot("saddle", 1);
    case 1: return slot("iron_ingot", r.nextInt(4) + 1);
    case 2: return slot("bread", 1);
    case 3: return slot("wheat", r.nextInt(4) + 1);
    case 4: return slot("gunpowder", r.nextInt(4) + 1);
    case 5: return slot("string", r.nextInt(4) + 1);
    case 6: return slot("bucket", 1);
    case 7: return r.nextInt(100) == 0 ? slot("golden_apple", 1) : json();
    case 8: return r.nextInt(2) == 0 ? slot("redstone", r.nextInt(4) + 1) : json();
    case 9: if (r.nextInt(10) == 0) { static const char* D[] = {"music_disc_grove", "music_disc_sun", "music_disc_ember", "music_disc_tide"}; return slot(D[r.nextInt(4)], 1); } return json();
    case 10: return slot("cocoa_beans", r.nextInt(3) + 1);
    case 11: { static const char* G[] = {"gold_ingot", "coal", "bone", "rotten_flesh", "pumpkin_seeds", "melon_seeds"}; int k = r.nextInt(6); return slot(G[k], k < 1 ? r.nextInt(3) + 1 : r.nextInt(6) + 1); }
    case 12: { static const char* Q[] = {"silver_ingot", "tin_ingot", "copper_ingot", "book", "arrow", "lapis_lazuli"}; int k = r.nextInt(6); return slot(Q[k], r.nextInt(k == 3 ? 2 : 5) + 1); }
    default: { static const char* R[] = {"diamond", "emerald", "ruby", "sapphire", "enchanted_book", "mithril_ingot"}; return r.nextInt(5) == 0 ? slot(R[r.nextInt(6)], 1) : json(); }
  }
}
} // namespace

void OverworldGen::placeDungeons(GenBuf& b, Chunk& ch, const OwColumn* cols) const {
  const BlockId COBBLE = bid("cobblestone"), MOSSY = bid("mossy_cobblestone"), CAGE = bid("monster_spawner"), CHEST = bid("chest");
  if (COBBLE == NOBLOCK || MOSSY == NOBLOCK || CAGE == NOBLOCK || CHEST == NOBLOCK) return;
  JRandom rnd((i64)hash32(seed_, ch.cx, ch.cz, 0xD06E) * 0x5DEECE66DLL + ch.cx);   // u32 x 35 bits: no overflow
  int minH = DIM_H; for (int i = 0; i < 256; i++) minH = std::min(minH, cols[i].H);
  auto solid = [&](int x, int y, int z) { return B.solid[b.blocks[colIdx(x, y, z)]] && !B.liquid[b.blocks[colIdx(x, y, z)]]; };
  auto air = [&](int x, int y, int z) { return b.blocks[colIdx(x, y, z)] == AIR; };
  int built = 0;
  for (int attempt = 0; attempt < 8 && built < 2; attempt++) {
    const int rx = rnd.nextInt(2) + 2, rz = rnd.nextInt(2) + 2, H = 3;
    const int x = rx + 1 + rnd.nextInt(16 - 2 * (rx + 1)), z = rz + 1 + rnd.nextInt(16 - 2 * (rz + 1));
    const int y = 6 + rnd.nextInt(std::max(1, std::min(minH - 10, 120) - 6));
    if (y + H + 1 >= minH - 3) continue;
    // the Beta test: solid floor and roof all over, 1-5 openings (two-high gaps) in the walls at the floor
    bool ok = true; int openings = 0;
    for (int xx = x - rx - 1; xx <= x + rx + 1 && ok; xx++) for (int zz = z - rz - 1; zz <= z + rz + 1 && ok; zz++) {
      if (!solid(xx, y - 1, zz) || !solid(xx, y + H + 1, zz)) ok = false;
      const bool wall = xx == x - rx - 1 || xx == x + rx + 1 || zz == z - rz - 1 || zz == z + rz + 1;
      if (wall && air(xx, y, zz) && air(xx, y + 1, zz)) openings++;
    }
    if (!ok || openings < 1 || openings > 5) continue;
    for (int xx = x - rx - 1; xx <= x + rx + 1; xx++) for (int zz = z - rz - 1; zz <= z + rz + 1; zz++)
      for (int yy = y + H; yy >= y - 1; yy--) {
        const int i = colIdx(xx, yy, zz);
        const bool inside = xx != x - rx - 1 && xx != x + rx + 1 && zz != z - rz - 1 && zz != z + rz + 1 && yy != y - 1 && yy != y + H + 1;
        if (inside) { b.blocks[i] = AIR; b.meta[i] = 0; ch.blockEntities.erase(i); }
        else if (yy >= 1 && !B.solid[b.blocks[colIdx(xx, yy - 1, zz)]]) { if (b.blocks[i] != B.bedrock) { b.blocks[i] = AIR; b.meta[i] = 0; } }
        else if (B.solid[b.blocks[i]] && b.blocks[i] != B.bedrock) { b.blocks[i] = yy == y - 1 && rnd.nextInt(4) != 0 ? MOSSY : COBBLE; b.meta[i] = 0; }
      }
    // the roof (Beta leaves whatever rock was there; cobble it so the room reads as built)
    for (int xx = x - rx - 1; xx <= x + rx + 1; xx++) for (int zz = z - rz - 1; zz <= z + rz + 1; zz++) {
      const int i = colIdx(xx, y + H + 1, zz);
      if (B.solid[b.blocks[i]] && !B.liquid[b.blocks[i]]) b.blocks[i] = COBBLE;
    }
    // chests: up to two, each against exactly one wall
    for (int k = 0; k < 2; k++) for (int tries = 0; tries < 3; tries++) {
      const int qx = x + rnd.nextInt(rx * 2 + 1) - rx, qz = z + rnd.nextInt(rz * 2 + 1) - rz;
      if (!air(qx, y, qz)) continue;
      int walls = 0, d0 = 0;
      for (int d = 0; d < 4; d++) if (solid(qx + DIRX[d], y, qz + DIRZ[d])) { walls++; d0 = d; }
      if (walls != 1) continue;
      const int i = colIdx(qx, y, qz);
      b.blocks[i] = CHEST; b.meta[i] = (u8)d0;   // its front faces away from the wall (SP::chest's meta rule)
      json items = json::array(); for (int s = 0; s < 27; s++) items.push_back(nullptr);
      for (int n = 0; n < 8; n++) { json it = lootItem(rnd); const int s = rnd.nextInt(27); if (!it.is_null()) items[s] = it; }
      ch.blockEntities[i] = json{{"type", "chest"}, {"items", items}};
      break;
    }
    // the cage: Beta's mix (skeleton 1/4, zombie 1/2, spider 1/4); the original's zombie is the ghoul
    static const char* MOBS[4] = {"skeleton", "ghoul", "ghoul", "spider"};
    const int i = colIdx(x, y, z);
    b.blocks[i] = CAGE; b.meta[i] = 0;
    ch.blockEntities[i] = json{{"type", "spawner"}, {"mob", MOBS[rnd.nextInt(4)]}, {"dungeon", true}};
    built++;
  }
}

} // namespace dq
