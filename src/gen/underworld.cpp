// gen/underworld.cpp — the Underworld (docs/WORLD_DEPTH.md): the bands under the Overworld in a deep world (layout 2).
// Underworld 1, just under the Overworld, carries Minecraft's land on down to its floor (y -64, in slate: our
// deepslate), its caves and their lava below y -55; under that, and through the bands below it, the deep caverns:
// great hollows and winding tunnels in rock that changes with depth (slate, then tuff, gabbro and basalt), each region
// one kind of cavern (amethyst grottos, lush hollows, fungal halls, dripstone, frozen, magma deeps, the sculk deep),
// the rare ores deeper down, old tombs (monster rooms), and at the very bottom a lava sea over the bedrock floor and
// the Ember Crust above the Nether.
// Band-local y -> Minecraft's y: my = ly - 1 - 256 * depth (Underworld 1's top row is Minecraft's -2; the Overworld's
// row 0 is -1).
#include "gen/overworld.h"
#include "gen/density.h"
#include "gen/structures_common.h"
#include "core/registry.h"

namespace dq {

namespace {
struct DeepBlocks {
  BlockId slate, tuff, gabbro, basalt, smoothBasalt, blackstone, calcite, amethyst, budding, cluster, moss, mossCarpet, glowLichen,
          caveVines, spore, azalea, roots, clay, water, lava, mycelium, redM, brownM, redMB, brownMB, stem, shroom, dripB, dripP,
          packed, blue, snow, magma, obsidian, crying, sculk, sculkVein, bedrock, crust, cobweb, stone, gravel;
  DeepBlocks() {
    auto g = [](const char* k, const char* alt = "stone") { BlockId b = reg().blockId(k); return b ? b : reg().blockId(alt); };
    slate = g("slate"); tuff = g("tuff", "slate"); gabbro = g("gabbro", "slate"); basalt = g("basalt", "slate"); smoothBasalt = g("smooth_basalt", "slate");
    blackstone = g("blackstone", "slate"); calcite = g("calcite"); amethyst = g("amethyst_block"); budding = g("budding_amethyst", "amethyst_block");
    cluster = reg().blockId("amethyst_cluster"); moss = g("moss_block", "dirt"); mossCarpet = reg().blockId("moss_carpet"); glowLichen = reg().blockId("glow_lichen");
    caveVines = reg().blockId("cave_vines"); spore = reg().blockId("spore_blossom"); azalea = reg().blockId("azalea"); roots = reg().blockId("hanging_roots");
    clay = g("clay"); water = g("water"); lava = g("lava"); mycelium = g("mycelium", "dirt"); redM = reg().blockId("red_mushroom"); brownM = reg().blockId("brown_mushroom");
    redMB = g("red_mushroom_block"); brownMB = g("brown_mushroom_block"); stem = g("mushroom_stem"); shroom = g("shroomlight", "glowstone");
    dripB = g("dripstone_block", "stone"); dripP = reg().blockId("pointed_dripstone"); packed = g("packed_ice"); blue = g("blue_ice", "packed_ice"); snow = g("snow_block");
    magma = g("magma_block"); obsidian = g("obsidian"); crying = g("crying_obsidian", "obsidian"); sculk = g("sculk", "slate"); sculkVein = reg().blockId("sculk_vein");
    bedrock = g("bedrock"); crust = g("ember_crust", "bedrock"); cobweb = reg().blockId("cobweb"); stone = g("stone"); gravel = g("gravel");
  }
};
const DeepBlocks& DB() { static const DeepBlocks d; return d; }

enum Theme { T_PLAIN, T_AMETHYST, T_LUSH, T_FUNGAL, T_DRIP, T_FROZEN, T_MAGMA, T_SCULK, T_COUNT };

inline double h01(i32 seed, int x, int y, int z, int salt) { return posHashF(seed ^ salt * 0x9E3779B1, x, y, z); }
} // namespace

// the kind of cavern a region of the deep is: by two slow noises and the depth (frozen and lush nearer the top, magma
// and the sculk deep at the bottom)
static Theme deepTheme(const SimplexNoise& n1, const SimplexNoise& n2, int wx, int wz, int my) {
  const double a = n1.noise2(wx / 260.0, wz / 260.0), b = n2.noise2(wx / 210.0 + 40, wz / 210.0 - 17);
  const double depth = std::clamp((-64.0 - my) / 900.0, 0.0, 1.0);   // 0 at y -64, 1 a thousand blocks down
  if (std::fabs(a) < 0.08) return T_PLAIN;
  if (a > 0.45) return depth > 0.55 ? T_MAGMA : T_AMETHYST;
  if (a < -0.45) return depth > 0.6 ? T_SCULK : T_FROZEN;
  if (b > 0.3) return depth < 0.5 ? T_LUSH : T_MAGMA;
  if (b < -0.3) return T_FUNGAL;
  if (a > 0.2) return T_DRIP;
  return depth > 0.75 ? T_SCULK : T_PLAIN;
}

void OverworldGen::generateDeep(Chunk& ch, int depth) const {
  GenBuf& b = GenBuf::local();
  const DeepBlocks& K = DB();
  const int bx = ch.cx * CS, bz = ch.cz * CS;
  const int deepest = worldLayout().underDepth;
  auto myOf = [&](int ly) { return ly - 1 - DIM_H * depth; };
  for (int i = 0; i < 256; i++) { ch.climate[i * 2] = 128; ch.climate[i * 2 + 1] = 128; }
  // the rock by depth: slate, then tuff, gabbro, basalt (a slow noise wobbles the boundaries: no flat lines)
  auto rockAt = [&](int wx, int my, int wz) {
    const double w = nDeep.noise3(wx / 60.0, my / 40.0, wz / 60.0) * 28 + (h01(seed_, wx, my, wz, 71) - 0.5) * 4;
    const double d = -my + w;
    return d < 330 ? K.slate : d < 520 ? (nDeepC.noise3(wx / 24.0, my / 18.0, wz / 24.0) > 0.15 ? K.tuff : K.slate) : d < 760 ? K.gabbro : K.basalt;
  };
  // 1. Minecraft's land down to its floor (Underworld 1, y -64..-2): its density, its caves, lava below y -55
  const int vanTop = depth == 1 ? DIM_H - 1 : -1, vanBot = depth == 1 ? std::max(0, -64 + 1 + DIM_H * depth) : DIM_H;   // band rows of y -64..-2
  if (depth == 1 && v14 && vg_) {
    const DensityGraph& g = *vg_;
    constexpr int NY = DIM_H / 8 + 1;
    static thread_local std::vector<double> lat; lat.assign(5 * 5 * NY, 1);
    const int j0 = vanBot / 8;
    for (int k = 0; k < 5; k++) for (int i = 0; i < 5; i++) for (int j = j0; j < NY; j++)
      lat[(k * 5 + i) * NY + j] = g.eval(vFinal, bx + i * 4, myOf(j * 8), bz + k * 4);
    for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
      const int i0 = x >> 2, k0 = z >> 2; const double fx = (x & 3) / 4.0, fz = (z & 3) / 4.0;
      for (int y = vanBot; y <= vanTop; y++) {
        const int jj = y >> 3; const double fy = (y & 7) / 8.0;
        auto L = [&](int di, int dj, int dk) { return lat[((k0 + dk) * 5 + (i0 + di)) * NY + std::min(NY - 1, jj + dj)]; };
        const double c00 = L(0, 0, 0) + (L(1, 0, 0) - L(0, 0, 0)) * fx, c10 = L(0, 1, 0) + (L(1, 1, 0) - L(0, 1, 0)) * fx;
        const double c01 = L(0, 0, 1) + (L(1, 0, 1) - L(0, 0, 1)) * fx, c11 = L(0, 1, 1) + (L(1, 1, 1) - L(0, 1, 1)) * fx;
        const double d0 = c00 + (c10 - c00) * fy, d1 = c01 + (c11 - c01) * fy;
        const double d = d0 + (d1 - d0) * fz;
        const int my = myOf(y);
        b.blocks[colIdx(x, y, z)] = d > 0 ? K.slate : my <= -55 ? K.lava : AIR;
      }
    }
  } else if (depth == 1) {
    for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) for (int y = vanBot; y <= vanTop; y++) b.blocks[colIdx(x, y, z)] = K.slate;
  }
  // 2. the deep caverns: a field on a 4 x 4 x 4 lattice (great hollows, and winding tunnels where two noises cross 0)
  const int cavTop = depth == 1 ? vanBot - 1 : DIM_H - 1;
  if (cavTop >= 0) {
    constexpr int NX = 5, NYC = DIM_H / 4 + 1;
    static thread_local std::vector<float> F; F.assign(NX * NX * NYC, 0);
    for (int k = 0; k < NX; k++) for (int i = 0; i < NX; i++) for (int j = 0; j < NYC; j++) {
      const double wx = bx + i * 4, wz = bz + k * 4, my = myOf(j * 4);
      const double hollow = nCave1.noise3(wx / 56.0, my / 30.0, wz / 56.0) * 0.75 + nCave2.noise3(wx / 19.0, my / 13.0, wz / 19.0) * 0.25;
      const double t1 = nCave3.noise3(wx / 44.0, my / 30.0, wz / 44.0), t2 = nFloor.noise3(wx / 44.0 + 300, my / 30.0, wz / 44.0 - 300);
      const double tunnel = 0.11 - std::sqrt(t1 * t1 + t2 * t2);
      // fewer and smaller hollows just under Minecraft's floor (it closes over gently), bigger lower down
      const double grow = std::clamp((-72.0 - my) / 60.0, 0.0, 1.0);
      F[(k * NX + i) * NYC + j] = (float)std::max((hollow - 0.32) * grow, tunnel * grow);
    }
    for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
      const int i0 = x >> 2, k0 = z >> 2; const float fx = (x & 3) / 4.f, fz = (z & 3) / 4.f;
      const int wx = bx + x, wz = bz + z;
      for (int y = 0; y <= cavTop; y++) {
        const int jj = y >> 2; const float fy = (y & 3) / 4.f;
        auto L = [&](int di, int dj, int dk) { return F[((k0 + dk) * NX + (i0 + di)) * NYC + std::min(NYC - 1, jj + dj)]; };
        const float c00 = L(0, 0, 0) + (L(1, 0, 0) - L(0, 0, 0)) * fx, c10 = L(0, 1, 0) + (L(1, 1, 0) - L(0, 1, 0)) * fx;
        const float c01 = L(0, 0, 1) + (L(1, 0, 1) - L(0, 0, 1)) * fx, c11 = L(0, 1, 1) + (L(1, 1, 1) - L(0, 1, 1)) * fx;
        const float d0 = c00 + (c10 - c00) * fy, d1 = c01 + (c11 - c01) * fy;
        const float open = d0 + (d1 - d0) * fz;
        const int my = myOf(y);
        b.blocks[colIdx(x, y, z)] = open > 0 ? (depth == deepest && y < 22 ? K.lava : AIR) : rockAt(wx, my, wz);   // (the lava sea at the bottom)
      }
    }
  }
  // the rock of Minecraft's part too, by depth (its slate) — left as slate: it is its deepslate
  // 3. the floor of the world: bedrock, jagged as Minecraft's, and the Ember Crust over the Nether
  if (depth == deepest) for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    for (int y = 0; y < 5; y++) if (y == 0 || posHashF(seed_, bx + x, y - DIM_H * depth, bz + z) < (5 - y) / 5.0) b.blocks[colIdx(x, y, z)] = K.bedrock;
    b.blocks[colIdx(x, 0, z)] = K.crust;
  }
  // 4. each region's cavern: its floors, walls and roofs dressed
  auto rock = [&](BlockId id) { return id == K.slate || id == K.tuff || id == K.gabbro || id == K.basalt; };
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    for (int y = 5; y < DIM_H - 1; y++) {
      const int i = colIdx(x, y, z);
      if (b.blocks[i] != AIR) continue;
      const int my = myOf(y);
      const Theme th = my < -66 ? deepTheme(nDeep, nDeepC, wx, wz, my) : T_PLAIN;
      const double r = h01(seed_, wx, my, wz, 13);
      const BlockId below = b.blocks[colIdx(x, y - 1, z)], above = b.blocks[colIdx(x, y + 1, z)];
      const bool floor = rock(below), roof = rock(above);
      auto setBelow = [&](BlockId id) { b.blocks[colIdx(x, y - 1, z)] = id; };
      auto setAbove = [&](BlockId id) { b.blocks[colIdx(x, y + 1, z)] = id; };
      auto put = [&](BlockId id) { if (id) b.blocks[i] = id; };
      switch (th) {
        case T_AMETHYST:
          if (floor) { setBelow(r < 0.06 ? K.budding : r < 0.5 ? K.amethyst : K.calcite); if (r < 0.12 && K.cluster) put(K.cluster); }
          if (roof) { setAbove(r > 0.7 ? K.amethyst : K.calcite); }
          break;
        case T_LUSH:
          if (floor) { setBelow(r < 0.12 ? K.clay : K.moss); if (r < 0.3) put(K.mossCarpet); else if (r < 0.36) put(K.azalea); else if (r > 0.97) {   // a pool: only into a basin (rock all round and under it), never a source loose on an open floor
              bool basin = true; for (int d = 0; d < 4 && basin; d++) { const int ax = x + DIRX[d], az = z + DIRZ[d]; basin = ax >= 0 && ax <= 15 && az >= 0 && az <= 15 && rock(b.blocks[colIdx(ax, y, az)]); }
              if (basin) b.blocks[colIdx(x, y, z)] = K.water; } }
          if (roof) { setAbove(K.moss); if (r > 0.993) put(K.spore); else if (r > 0.9 && K.caveVines) { for (int k = 0; k < 4 && y - k > 5 && b.blocks[colIdx(x, y - k, z)] == AIR; k++) b.blocks[colIdx(x, y - k, z)] = K.caveVines; } else if (r > 0.86) put(K.roots); }
          break;
        case T_FUNGAL:
          if (floor) { setBelow(K.mycelium); if (r < 0.08) put(K.brownM); else if (r < 0.14) put(K.redM);
            else if (r > 0.992 && x >= 2 && x <= 13 && z >= 2 && z <= 13) {   // a great mushroom: a stem and a cap (its cap all inside the chunk: none cut off at an edge)
              int top = y; while (top - y < 7 && top < DIM_H - 3 && b.blocks[colIdx(x, top + 1, z)] == AIR) top++;
              if (top - y >= 4) { const BlockId cap = r > 0.996 ? K.redMB : K.brownMB;
                for (int k = y; k < top; k++) b.blocks[colIdx(x, k, z)] = K.stem;
                for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) { const int ax = x + dx, az = z + dz; if (ax < 0 || ax > 15 || az < 0 || az > 15 || std::abs(dx) + std::abs(dz) > 3) continue; if (b.blocks[colIdx(ax, top, az)] == AIR) b.blocks[colIdx(ax, top, az)] = cap; }
                if (b.blocks[colIdx(x, top - 1, z)] == K.stem && r > 0.998) b.blocks[colIdx(x, top - 1, z)] = K.shroom; } } }
          if (roof && r > 0.95) setAbove(K.shroom);
          break;
        case T_DRIP:
          // clusters of spikes, each its own length, the longest nearest the middle of a cluster (the clusters by a slow
          // noise); now and then a stalagmite and a stalactite meet in a column
          if (floor) { setBelow(K.dripB);
            const double cl = nCave2.noise2(wx / 7.0, wz / 7.0 + my * 0.01);
            if (K.dripP && cl > 0.15 && r < 0.45) {
              int room = 0; while (room < 12 && y + room < DIM_H - 1 && b.blocks[colIdx(x, y + room, z)] == AIR) room++;
              const int n = std::min(room - 1, 1 + (int)((cl - 0.15) * 9 * (0.5 + h01(seed_, wx, my, wz, 31))));
              const bool meet = r < 0.02 && room <= 9;
              const int up = meet ? (room + 1) / 2 : n;
              for (int k = 0; k < up; k++) { b.blocks[colIdx(x, y + k, z)] = K.dripP; b.meta[colIdx(x, y + k, z)] = 0; }
              if (meet) for (int k = up; k < room; k++) { b.blocks[colIdx(x, y + k, z)] = K.dripP; b.meta[colIdx(x, y + k, z)] = 1; }
            } }
          if (roof && b.blocks[i] == AIR) { setAbove(K.dripB);
            const double cl = nCave2.noise2(wx / 7.0 + 40, wz / 7.0 - my * 0.01);
            if (K.dripP && cl > 0.1 && r > 0.45) {
              int room = 0; while (room < 12 && y - room > 5 && b.blocks[colIdx(x, y - room, z)] == AIR) room++;
              const int n = std::min(room - 1, 1 + (int)((cl - 0.1) * 10 * (0.5 + h01(seed_, wx, my, wz, 37))));
              for (int k = 0; k < n; k++) { b.blocks[colIdx(x, y - k, z)] = K.dripP; b.meta[colIdx(x, y - k, z)] = 1; }
            } }
          break;
        case T_FROZEN:
          if (floor) { setBelow(r < 0.2 ? K.blue : r < 0.6 ? K.packed : K.snow); }
          if (roof) { setAbove(r > 0.6 ? K.packed : K.blue); }
          break;
        case T_MAGMA:
          if (floor) { setBelow(r < 0.25 ? K.magma : r < 0.5 ? K.blackstone : r < 0.55 ? K.obsidian : K.smoothBasalt); if (r > 0.985) put(K.lava); }
          if (roof) { setAbove(r > 0.9 ? K.crying : K.blackstone); }
          break;
        case T_SCULK:
          if (floor) { setBelow(r < 0.7 ? K.sculk : K.slate); if (r < 0.2 && K.sculkVein) put(K.sculkVein); }
          if (roof && r > 0.5) setAbove(K.sculk);
          break;
        default:
          if (floor && r < 0.02) setBelow(K.gravel);
          if (roof && r > 0.985 && K.glowLichen) put(K.glowLichen);
          if (floor && r > 0.997 && K.cobweb) put(K.cobweb);
          break;
      }
    }
  }
  // 5. ores in the rock: Minecraft's deep ones (diamond, redstone, gold, lapis, iron), ours deeper (ruby, sapphire,
  // mithril, mese, adamantite), more the deeper
  {
    Mulberry32 rnd(hash32(seed_, ch.cx, ch.cz, 0xDEE0 + depth));
    struct O { const char* id; double tries; int size; double fromDepth; };
    static const O ORES[] = {{"diamond_ore", 2.5, 6, 0}, {"redstone_ore", 6, 7, 0}, {"gold_ore", 3, 8, 0}, {"lapis_ore", 1.5, 6, 0}, {"iron_ore", 6, 9, 0},
                             {"coal_ore", 3, 12, 0}, {"emerald_ore", 0.4, 3, 0}, {"silver_ore", 2, 7, 0}, {"ruby_ore", 1.2, 5, 0.2}, {"sapphire_ore", 1.2, 5, 0.2},
                             {"mithril_ore", 1.0, 5, 0.35}, {"mese_ore", 0.8, 5, 0.5}, {"adamantite_ore", 0.6, 4, 0.7}};
    for (const O& o : ORES) {
      const BlockId id = reg().blockId(o.id); if (!id) continue;
      for (int y0 = 0; y0 < DIM_H; y0 += 64) {
        const double dd = std::clamp((-(double)myOf(y0 + 32) - 64.0) / 900.0, 0.0, 1.0);
        if (dd < o.fromDepth) continue;
        double n = o.tries * (0.6 + dd); int cnt = (int)n; if (rnd() < n - cnt) cnt++;
        for (int k = 0; k < cnt; k++) {
          const int cx = 1 + (int)(rnd() * 14), cy = y0 + (int)(rnd() * 64), cz = 1 + (int)(rnd() * 14);
          for (int s = 0; s < o.size; s++) {
            const int x = std::clamp(cx + (int)(rnd() * 3) - 1, 0, 15), y = std::clamp(cy + (int)(rnd() * 3) - 1, 1, DIM_H - 1), z = std::clamp(cz + (int)(rnd() * 3) - 1, 0, 15);
            BlockId& at = b.blocks[colIdx(x, y, z)];
            if (rock(at)) at = id;
          }
        }
      }
    }
  }
  // 6. old tombs: the Alpha/Beta monster rooms, where a cavern passes (gen/dungeons_classic.cpp)
  {
    static thread_local OwColumn cols[256];
    for (auto& c : cols) { c.H = DIM_H - 6; c.biome = 0; }
    placeDungeons(b, ch, cols);
  }
  b.store(ch);
}

// ---- the Underworld's generators: one per band, all on the Overworld's own (its land, its noises) ----------------
class UnderworldGen : public DimGenerator {
public:
  UnderworldGen(const OverworldGen* ow, int depth) : ow(ow), depth(depth) {}
  void generate(Chunk& c) override { ow->generateDeep(c, depth); }
  void sample(int, int, ColumnSample& s) const override { s.height = -1; }   // (nothing for the far land: it's all under ground)
  int spawnHeight(int, int) const override { return -1; }
private:
  const OverworldGen* ow; int depth;
};
std::unique_ptr<DimGenerator> makeUnderworldGen(const OverworldGen* ow, int depth) { return std::make_unique<UnderworldGen>(ow, depth); }

} // namespace dq
