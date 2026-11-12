// render/distant_build.cpp — the worker side of distant terrain, built the way Cubyz builds its far land (ideas from
// Cubyz's chunk.zig updateFromLowerResolution and renderer/mesh_storage.zig; no code taken, Cubyz is GPL):
//  * the land is VOXELS at every distance, only coarser further off: 2x2 blocks across next to the loaded chunks,
//    then 4, 8 ... (one block high each, so far hills keep their true heights);
//  * a coarse voxel is made from the finer ones under it: solid if ANY of them is (air is avoided, so thin walls,
//    bridges and spires survive), and of the block that shows the most — one with air round it beats one buried, so
//    grass stays on top of dirt and a wall keeps its bricks;
//  * real chunks are brought down to 2x2-block voxels once (summarizeChunk) and every coarser level is made from those;
//    where nobody has been, the generator's columns give the voxels;
//  * the voxels are drawn with the blocks' own textures (one texture per voxel face, like Cubyz's LOD chunks), merged
//    into big quads where the same block runs on.
#include "render/distant.h"
#include "render/mesher.h"
#include "assets/tileset.h"
#include <cstring>

namespace dq::lod {

namespace {
using Col = std::array<BlockId, DIM_H>;   // a column of voxels, band-local y
vec3 unpack(u32 c) { return vec3(c & 255, (c >> 8) & 255, (c >> 16) & 255); }
inline u8 b8(float v) { return (u8)clampv((int)std::lround(v), 0, 255); }

// what a voxel can be made of: plants, torches and the like vanish far off (Cubyz's lodReplacement)
inline bool lodSolid(const Registry& R, BlockId id) {
  return id && (R.tOpaque[id] || R.tLiquid[id] || R.tLeaves[id] || (R.tSolid[id] && R.tRender[id] != (u8)Render::Cross));
}

// Cubyz's pick for a coarse voxel: solid if any of the finer columns is solid at that height, and the block among them
// with the most air round it (above, below, and in the voxel's other columns)
void mergeCols(const Col* const* sub, int n, const Registry& R, Col& out) {
  for (int y = 0; y < DIM_H; y++) {
    int best = -1, bestScore = -1;
    for (int s = 0; s < n; s++) {
      const BlockId id = (*sub[s])[y];
      if (!lodSolid(R, id)) continue;
      int sc = 1;
      if (y + 1 >= DIM_H || !lodSolid(R, (*sub[s])[y + 1])) sc += 5;
      if (y > 0 && !lodSolid(R, (*sub[s])[y - 1])) sc += 5;
      for (int o = 0; o < n; o++) if (o != s && !lodSolid(R, (*sub[o])[y])) sc += 5;
      if (sc > bestScore) { bestScore = sc; best = s; }
    }
    out[y] = best < 0 ? AIR : (*sub[best])[y];
  }
}

// the grass / foliage / water tint of a block in a climate (as the chunk mesher tints it); white if none
vec3 tintOf(const ColorTable& ct, BlockId id, u8 temp, u8 humid, u32 sampleTint) {
  const u8 k = id < ct.tint.size() ? ct.tint[id] : 0;
  if (k == 3) return unpack(climateWater(temp, humid)) / 255.f;   // the chunks' own water colour for the climate
  if (k == 0) return vec3(1);
  if (sampleTint != 0xFFFFFFFF) return unpack(sampleTint) / 255.f * (k == 2 ? 0.9f : 1.f);
  return unpack(climateTint(k == 2 ? "foliage" : "grass", temp, humid)) / 255.f * (k == 2 ? 0.9f : 1.f);
}
} // namespace

std::shared_ptr<ColorTable> makeColorTable(const TileSet& tiles) {
  auto ct = std::make_shared<ColorTable>();
  auto& r = reg();
  size_t n = r.blocks.size();
  ct->top.assign(n, 0x808080); ct->side.assign(n, 0x808080); ct->tint.assign(n, 0); ct->isLog.assign(n, 0);
  for (auto& b : r.blocks) {
    u32 t = tiles.avgColor(b.faces[2]), s = tiles.avgColor(b.faces[0]);
    if (t) ct->top[b.id] = t & 0xFFFFFF;
    if (s) ct->side[b.id] = s & 0xFFFFFF; else ct->side[b.id] = ct->top[b.id];
    std::string tk = b.str("tint");
    ct->tint[b.id] = r.tLiquid[b.id] == 1 ? 3 : tk == "grass" ? 1 : tk == "foliage" ? 2 : 0;
    ct->isLog[b.id] = b.has("isLog");
  }
  ct->water = r.blockId("water"); ct->lava = r.blockId("lava"); ct->dirt = r.blockId("dirt"); ct->ice = r.blockId("ice");
  return ct;
}

// ---- a real chunk brought down to 2x2-block voxels, one block high (Cubyz: a level made from the one under it) ----
// Works for chunks you've seen and for chunks the generator makes for the far land alone (DistantTerrain::genFar).
std::shared_ptr<ChunkSummary> summarizeChunk(const Chunk& c, const ColorTable& ct) {
  (void)ct;
  auto S = std::make_shared<ChunkSummary>();
  const Registry& R = reg();
  const int top = std::min(DIM_H - 1, c.maxY + 1);
  static const BlockId DEEP[DIM_COUNT] = {R.blockId("end_stone"), R.blockId("netherrack"), R.blockId("stone"), R.blockId("skystone") ? R.blockId("skystone") : R.blockId("stone")};
  const BlockId deep = c.dim >= 0 && c.dim < DIM_COUNT ? DEEP[c.dim] : AIR;
  // where daylight gets in: the chunk's sky light, or (a chunk just generated, not lit) the air the open sky reaches
  static thread_local std::vector<u8> sky;
  const bool useLight = c.lightReady;
  if (!useLight) {
    const int H = top + 2, NN = 16 * 16 * H;
    sky.assign(NN, 0);
    static thread_local std::vector<int> q; q.clear();
    auto idx = [&](int x, int y, int z) { return (y * 16 + z) * 16 + x; };
    auto air = [&](int x, int y, int z) { return y > top || !lodSolid(R, c.get(x, y, z)); };
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) { sky[idx(x, H - 1, z)] = 1; q.push_back(idx(x, H - 1, z)); }
    for (size_t h = 0; h < q.size(); h++) {
      const int i = q[h], x = i & 15, z = (i >> 4) & 15, y = i >> 8;
      const int nx[6] = {x + 1, x - 1, x, x, x, x}, nz[6] = {z, z, z + 1, z - 1, z, z}, ny[6] = {y, y, y, y, y - 1, y + 1};
      for (int d = 0; d < 6; d++) {
        if (nx[d] < 0 || nx[d] > 15 || nz[d] < 0 || nz[d] > 15 || ny[d] < 0 || ny[d] >= H) continue;
        const int j = idx(nx[d], ny[d], nz[d]);
        if (sky[j] || !air(nx[d], ny[d], nz[d])) continue;
        sky[j] = 1; q.push_back(j);
      }
    }
  }
  static thread_local std::vector<Col> cell; cell.resize(64);
  Col cols[4];
  static thread_local std::vector<std::array<u8, DIM_H>> lit; lit.resize(64);
  for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {
    const int k = j * 8 + i;
    lit[k].fill(0);
    for (int s = 0; s < 4; s++) {
      const int x = i * 2 + (s & 1), z = j * 2 + (s >> 1);
      for (int y = 0; y < DIM_H; y++) {
        const BlockId id = y <= top ? c.get(x, y, z) : AIR;
        cols[s][y] = id;
        if (!lodSolid(R, id) && y <= top && (useLight ? (c.getLight(x, y, z) >> 4) > 0 : sky[((y * 16 + z) * 16 + x)] != 0)) lit[k][y] = 1;
      }
    }
    const Col* p[4] = {&cols[0], &cols[1], &cols[2], &cols[3]};
    Col& m = cell[k];
    mergeCols(p, 4, R, m);
    // a hollow shut away underground (no daylight, under six blocks tall) is filled in: nobody sees it from afar
    for (int y = 1; y < DIM_H - 1;) {
      if (lodSolid(R, m[y]) || !lodSolid(R, m[y - 1])) { y++; continue; }
      int y1 = y; while (y1 < DIM_H && !lodSolid(R, m[y1])) y1++;
      if (y1 >= DIM_H) break;   // open to the sky above
      bool open = y1 - y >= 6;
      for (int q = y; q < y1 && !open; q++) open = lit[k][q];
      if (!open) for (int q = y; q < y1; q++) m[q] = m[y1];
      y = y1;
    }
  }
  // buried voxels (rock on every side, well under the ground's top) become plain rock, so a column is a few runs, not
  // dozens (ores, slate, bedrock): nobody sees them, and a cliff in the next chunk shows rock there anyway
  auto solidAt = [&](int i, int j, int y) { return i < 0 || i > 7 || j < 0 || j > 7 || lodSolid(R, cell[j * 8 + i][y]); };
  if (deep) for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {
    Col& m = cell[j * 8 + i];
    int yTop = -1; for (int y = DIM_H - 1; y >= 0; y--) if (lodSolid(R, m[y])) { yTop = y; break; }
    for (int y = 1; y < yTop - 4; y++) {
      if (!lodSolid(R, m[y]) || R.tLiquid[m[y]] || !lodSolid(R, m[y - 1]) || !lodSolid(R, m[y + 1])) continue;
      if (solidAt(i - 1, j, y) && solidAt(i + 1, j, y) && solidAt(i, j - 1, y) && solidAt(i, j + 1, y)) m[y] = deep;
    }
  }
  for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {
    const int k = j * 8 + i;
    const Col& m = cell[k];
    S->start[k] = (u16)S->runs.size();
    for (int y = 0; y < DIM_H; y++) {
      if (!lodSolid(R, m[y])) continue;
      if (S->runs.size() > S->start[k] && S->runs.back().id == m[y] && S->runs.back().y1 + 1 == y) { S->runs.back().y1 = (u8)y; continue; }
      S->runs.push_back({(u8)y, (u8)y, m[y]});
    }
    const int ci = ((j * 2) * 16 + i * 2) * 2;
    S->temp[k] = c.climate[ci]; S->humid[k] = c.climate[ci + 1];
  }
  S->start[64] = (u16)S->runs.size();
  S->runs.shrink_to_fit();
  return S;
}

// ---- a tile: its voxel columns, then its mesh ---------------------------------------------------------------
namespace {
struct Run { i16 y0, y1; BlockId id; };   // band-local, inclusive

struct MeshOut {
  std::vector<LodVertex>& v; float size; float minY = 1e9f, maxY = -1e9f;
  void vert(vec3 p, u16 tile, vec3 c, int sh, u16 flags) {
    LodVertex o;
    o.x = (u16)clampv((int)std::lround(p.x / size * 32768.f), 0, 65535);
    o.z = (u16)clampv((int)std::lround(p.z / size * 32768.f), 0, 65535);
    o.y = (i16)clampv((int)std::lround(p.y * 8), -32768, 32767);
    o.tile = tile;
    o.rgb = (u16)(((b8(c.x) * 31 + 127) / 255) << 11 | ((b8(c.y) * 63 + 127) / 255) << 5 | ((b8(c.z) * 31 + 127) / 255));
    o.pk = (u16)((flags & 15) | ((flags >> 8) & 7) << 4 | clampv(sh, 0, 255) << 7);
    minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    v.push_back(o);
  }
  // a quad facing `n` (corners in any order round the edge), shades per corner
  void quad(vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, u16 tile, vec3 col, int sa, int sb, int sc, int sd, u16 fl) {
    if (glm::dot(glm::cross(b - a, c - a), n) < 0) { std::swap(b, d); std::swap(sb, sd); }
    vert(a, tile, col, sa, fl); vert(b, tile, col, sb, fl); vert(c, tile, col, sc, fl); vert(d, tile, col, sd, fl);
  }
};
// the wall's foot in shadow, for depth
int footShade(int sh, float y1, float y0) { return std::max(80, (int)std::round(sh * std::max(0.6f, 1 - (y1 - y0) * 0.04f))); }
} // namespace

void buildTile(const TileInput& in, TileOutput& out) {
  double t0 = nowSeconds();
  const int N = CELLS, L = in.L, cs = cellSize(L), size = N * cs, W = N + 2, Q = L == 0 ? 1 : 4;
  const int ox = in.tx * size, oz = in.tz * size, base = dimBase(in.dim);
  const ColorTable& ct = *in.colors;
  const Registry& R = reg();
  DimGenerator* gen = in.gen ? in.gen->forDim(in.dim) : nullptr;
  static const BlockId DEEP[DIM_COUNT] = {R.blockId("end_stone"), R.blockId("netherrack"), R.blockId("stone"), R.blockId("skystone") ? R.blockId("skystone") : R.blockId("stone")};
  const BlockId deep = in.dim >= 0 && in.dim < DIM_COUNT && DEEP[in.dim] ? DEEP[in.dim] : ct.dirt;

  // trees where the generator plants them (near tiles): their crowns and trunks as voxels too
  struct TreeCol { i16 top = -1, leafBottom = 999; BlockId leaves = AIR, log = AIR; };
  std::vector<TreeCol> trees;
  if (L <= 1 && gen) {
    trees.assign((size_t)W * W, TreeCol());
    const int x0 = ox - cs, z0 = oz - cs, x1 = ox + size + cs, z1 = oz + size + cs;
    std::vector<TreeSample> list;
    for (int cz = floordiv(z0 - 8, 16); cz <= floordiv(z1 + 8, 16); cz++) for (int cx = floordiv(x0 - 8, 16); cx <= floordiv(x1 + 8, 16); cx++) {
      list.clear(); gen->trees(cx, cz, list);
      for (const TreeSample& t : list) for (const TreeColumn& c : t.cols) {
        const int wx = t.x + c.dx, wz = t.z + c.dz;
        if (wx < x0 || wz < z0 || wx >= x1 || wz >= z1) continue;
        TreeCol& tc = trees[(size_t)(floordiv(wz - oz, cs) + 1) * W + (floordiv(wx - ox, cs) + 1)];
        if (c.top > tc.top) tc.top = (i16)c.top;
        if (c.leafBottom < tc.leafBottom) tc.leafBottom = (i16)c.leafBottom;
        if (c.leaves && !tc.leaves) tc.leaves = c.leaves;
        if (c.log) tc.log = c.log;
      }
    }
  }

  // 1. each cell's column of voxels: from real chunks where they are known, otherwise from the generator
  std::vector<Run> runs; runs.reserve((size_t)W * W * 6);
  std::vector<int> start((size_t)W * W + 1, 0);
  std::vector<u8> temp((size_t)W * W, 128), humid((size_t)W * W, 128);
  std::vector<u32> stint((size_t)W * W, 0xFFFFFFFF);
  Col sub[4], col;
  auto fromSummary = [&](const ChunkSummary& sm, int wx, int wz, Col& c) {
    c.fill(AIR);
    const int q = ((wz & 15) >> 1) * 8 + ((wx & 15) >> 1);
    for (int r = sm.start[q]; r < sm.start[q + 1]; r++) for (int y = sm.runs[r].y0; y <= sm.runs[r].y1; y++) c[y] = sm.runs[r].id;
    return q;
  };
  auto fromGenerator = [&](int wx, int wz, Col& c, ColumnSample& s, int k) {
    c.fill(AIR);
    gen->sample(wx, wz, s);
    if (s.height >= DIM_H) s.height = DIM_H - 1;
    if (s.height >= 0) {
      const int bottom = s.floatBottom >= 0 && s.floatBottom <= s.height ? s.floatBottom : 0;
      const BlockId top = s.top ? s.top : ct.dirt, under = s.under ? s.under : top;
      for (int y = bottom; y <= s.height; y++) c[y] = y == s.height ? top : y >= s.height - 3 ? under : deep;
    }
    if (s.water >= 0 && s.fluid) for (int y = std::max(0, (int)s.height + 1); y <= std::min<int>(DIM_H - 1, s.water); y++) c[y] = s.fluid;
    if (s.roof >= 0 && s.roof > s.height) for (int y = s.roof; y < DIM_H; y++) c[y] = s.roofBlock ? s.roofBlock : deep;
    // far off, single trees can't be told apart: the forest is a canopy as tall as its trees (near tiles plant them)
    // (each cell's crown a little higher or lower than the next, so a forest is a lumpy canopy, not a flat roof)
    if (L >= 2 && s.tree && s.leaves && s.treeTop > s.height && !(s.water > s.height)) {
      u32 hsh = (u32)wx * 73856093u ^ (u32)wz * 19349663u; hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
      const int top = std::max<int>(s.height + 1, s.treeTop - (int)(hsh % 4));
      for (int y = s.height + 1; y <= std::min<int>(DIM_H - 1, top); y++) c[y] = s.leaves;
    }
    if (!trees.empty() && k >= 0 && trees[k].top > s.height) {
      const TreeCol& t = trees[k];
      const int lb = std::max<int>(s.height + 1, t.leafBottom < 999 ? t.leafBottom : t.top);
      for (int y = lb; y <= std::min<int>(DIM_H - 1, t.top); y++) if (!c[y]) c[y] = t.leaves ? t.leaves : s.leaves;
      if (t.log) for (int y = s.height + 1; y < lb && y < DIM_H; y++) if (!c[y]) c[y] = t.log;
    }
  };
  for (int j = 0; j < W; j++) for (int i = 0; i < W; i++) {
    const int k = j * W + i;
    start[k] = (int)runs.size();
    const int fx = (int)std::floor(ox + (i - 1 + 0.5) * cs), fz = (int)std::floor(oz + (j - 1 + 0.5) * cs);
    bool anyReal = false;
    for (int s = 0; s < Q && !in.sums.empty(); s++) if (in.sums[(size_t)k * Q + s]) anyReal = true;
    ColumnSample smp;
    if (!anyReal) {
      if (!gen) continue;
      fromGenerator(fx, fz, col, smp, k);
      temp[k] = smp.temp; humid[k] = smp.humid; stint[k] = smp.tint;
    } else {
      // Cubyz: a coarse voxel from the finer ones under it; the land nobody has seen from the generator
      for (int s = 0; s < Q; s++) {
        const int qx = Q == 1 ? fx : (int)std::floor(fx + ((s & 1) ? 0.25 : -0.25) * cs), qz = Q == 1 ? fz : (int)std::floor(fz + ((s & 2) ? 0.25 : -0.25) * cs);
        const ChunkSummary* sm = in.sums[(size_t)k * Q + s].get();
        if (sm) { int q = fromSummary(*sm, qx, qz, sub[s]); temp[k] = sm->temp[q]; humid[k] = sm->humid[q]; }
        else if (gen) fromGenerator(qx, qz, sub[s], smp, -1);
        else sub[s].fill(AIR);
      }
      if (Q == 1) col = sub[0];
      else { const Col* p[4] = {&sub[0], &sub[1], &sub[2], &sub[3]}; mergeCols(p, 4, R, col); }
    }
    for (int y = 0; y < DIM_H; y++) {
      if (!lodSolid(R, col[y])) continue;
      if ((int)runs.size() > start[k] && runs.back().id == col[y] && runs.back().y1 + 1 == y) { runs.back().y1 = (i16)y; continue; }
      runs.push_back({(i16)y, (i16)y, col[y]});
    }
  }
  start[(size_t)W * W] = (int)runs.size();

  // 2. the mesh: each voxel face that meets air, merged into quads where the same block runs on
  MeshOut M{out.verts, (float)size};
  out.verts.reserve((size_t)N * N * 12);
  auto faceOf = [&](BlockId id, int f) -> u16 { return R.block(id).faces[f]; };
  // flags: 1 water, 2 glows (lava), 4 tint every pixel (water), 8 drawn in its average colour (leaves and the like:
  // their see-through pixels mean nothing from afar); bits 8..10 the face (for the texture's direction)
  auto flagsOf = [&](BlockId id) -> u16 {
    return (u16)((R.tLiquid[id] == 1 ? 1 | 4 : 0) | (R.tLiquid[id] == 2 ? 2 : 0) | (R.tLeaves[id] || R.tRender[id] == (u8)Render::Cutout ? 8 : 0) | (id == ct.ice ? 1 | 4 : 0));
  };
  // a face's colour: the tint (white if none); a flat-coloured block's own colour, tinted
  auto colourOf = [&](BlockId id, int k, bool side) -> vec3 {
    // sea ice: near, it's see-through with the sea under it; far off it's drawn solid, so it takes the sea's blue
    if (id == ct.ice) return glm::min(unpack(climateWater(temp[k], humid[k])) * 1.15f, vec3(255.f));   // drawn as the near ice looks: sea under it
    vec3 t = tintOf(ct, id, temp[k], humid[k], stint[k]);
    if (side && ct.tint[id] == 1) t = vec3(1);   // grass sides: the picture marks which pixels take the tint
    if (flagsOf(id) & 8) return unpack(side ? ct.side[id] : ct.top[id]) * t;
    return t * 255.f;
  };
  // tops and undersides, merged into rectangles of the same height and block (a quad's texture repeats per voxel)
  // ambient occlusion on the tops, as Cubyz and the near chunks have it: a top next to higher ground is darker in the
  // corners that ground stands over, so hills, terraces and cliff feet read with depth. Those tops are their own quads
  // (shaded per corner); the open ones still merge into big rectangles.
  struct Face { i16 y; BlockId id; u8 ao[4]; };   // ao: corners (x0,z0) (x0,z1) (x1,z1) (x1,z0), 3 open .. 0 shut in
  auto solidCell = [&](int k, int y) {
    for (int r = start[k]; r < start[k + 1]; r++) if (y >= runs[r].y0 && y <= runs[r].y1) return !R.tLiquid[runs[r].id];
    return false;
  };
  std::vector<std::vector<Face>> tops((size_t)N * N), bots((size_t)N * N);
  for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
    const int k = (j + 1) * W + (i + 1);
    for (int r = start[k]; r < start[k + 1]; r++) {
      const bool above = r + 1 < start[k + 1] && runs[r + 1].y0 == runs[r].y1 + 1, below = r > start[k] && runs[r - 1].y1 == runs[r].y0 - 1;
      if (!above && runs[r].y1 + 1 < DIM_H) {
        Face f{(i16)(runs[r].y1 + 1), runs[r].id, {3, 3, 3, 3}};
        if (!R.tLiquid[f.id]) {
          auto o = [&](int dx, int dz) { return solidCell(k + dz * W + dx, f.y) ? 1 : 0; };
          const int xm = o(-1, 0), xp = o(1, 0), zm = o(0, -1), zp = o(0, 1);
          auto ao = [](int a, int b, int c) { return (u8)(a && b ? 0 : 3 - (a + b + c)); };
          f.ao[0] = ao(xm, zm, o(-1, -1)); f.ao[1] = ao(xm, zp, o(-1, 1)); f.ao[2] = ao(xp, zp, o(1, 1)); f.ao[3] = ao(xp, zm, o(1, -1));
        }
        tops[(size_t)j * N + i].push_back(f);
      }
      if (!below && runs[r].y0 > 0) bots[(size_t)j * N + i].push_back({runs[r].y0, runs[r].id, {3, 3, 3, 3}});
    }
  }
  auto open4 = [](const Face& f) { return f.ao[0] == 3 && f.ao[1] == 3 && f.ao[2] == 3 && f.ao[3] == 3; };
  std::vector<std::vector<u8>> used((size_t)N * N);
  for (int pass = 0; pass < 2; pass++) {
    auto& F = pass == 0 ? tops : bots;
    for (size_t q = 0; q < F.size(); q++) used[q].assign(F[q].size(), 0);
    // a face not yet taken at (i, j) matching f (same height and block, same tint)
    auto find = [&](int i, int j, const Face& f, int kRef) -> int {
      const auto& v = F[(size_t)j * N + i];
      const int k = (j + 1) * W + (i + 1);
      for (size_t q = 0; q < v.size(); q++) if (!used[(size_t)j * N + i][q] && v[q].y == f.y && v[q].id == f.id && open4(v[q]) &&
          (ct.tint[f.id] == 0 || (temp[k] == temp[kRef] && humid[k] == humid[kRef] && stint[k] == stint[kRef]))) return (int)q;
      return -1;
    };
    int hit[CELLS];
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
      for (size_t q = 0; q < F[(size_t)j * N + i].size(); q++) {
        if (used[(size_t)j * N + i][q]) continue;
        const Face f = F[(size_t)j * N + i][q];
        const int k = (j + 1) * W + (i + 1);
        used[(size_t)j * N + i][q] = 1;
        if (!open4(f)) {   // shaded in its corners: a quad of its own
          const float x0 = (float)i * cs, z0 = (float)j * cs, x1 = (float)(i + 1) * cs, z1 = (float)(j + 1) * cs, y = (float)(base + f.y);
          auto sh = [](u8 a) { return 125 + 43 * (int)a; };
          M.quad({x0, y, z0}, {x0, y, z1}, {x1, y, z1}, {x1, y, z0}, {0, 1, 0}, faceOf(f.id, 2), colourOf(f.id, k, false), sh(f.ao[0]), sh(f.ao[1]), sh(f.ao[2]), sh(f.ao[3]), (u16)(flagsOf(f.id) | (2 << 8)));
          continue;
        }
        int w = 1;
        while (i + w < N) { int g = find(i + w, j, f, k); if (g < 0) break; used[(size_t)j * N + i + w][g] = 1; w++; }
        int h = 1;
        for (; j + h < N; h++) {
          int x = 0;
          for (; x < w; x++) { hit[x] = find(i + x, j + h, f, k); if (hit[x] < 0) break; }
          if (x < w) break;
          for (x = 0; x < w; x++) used[(size_t)(j + h) * N + i + x][hit[x]] = 1;
        }
        const float x0 = (float)i * cs, z0 = (float)j * cs, x1 = (float)(i + w) * cs, z1 = (float)(j + h) * cs;
        const float y = (float)(base + f.y) - (pass == 0 && R.tLiquid[f.id] ? 0.125f : 0.f);
        const u16 fl = flagsOf(f.id);
        if (pass == 0) M.quad({x0, y, z0}, {x0, y, z1}, {x1, y, z1}, {x1, y, z0}, {0, 1, 0}, faceOf(f.id, 2), colourOf(f.id, k, false), 255, 255, 255, 255, (u16)(fl | (2 << 8)));
        else M.quad({x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, {0, -1, 0}, faceOf(f.id, 3), colourOf(f.id, k, true) * 0.85f, 145, 145, 145, 145, (u16)(fl | (3 << 8)));
      }
    }
  }

  // walls: each run of a cell, less what the neighbour's solid voxels cover, merged along each edge where they match
  const float skirt = 4 + cs * 0.75f;
  struct Sp { float y0, y1; };
  auto solidSpans = [&](int k, Sp* o, int cap, float lowerTop) -> int {
    int n = 0;
    for (int r = start[k]; r < start[k + 1]; r++) {
      const float y0 = (float)(base + runs[r].y0), y1 = (float)(base + runs[r].y1 + 1);
      if (n > 0 && o[n - 1].y1 == y0) { o[n - 1].y1 = y1; continue; }
      if (n < cap) o[n++] = {y0, y1};
    }
    for (int q = 0; q < n; q++) o[q].y1 -= lowerTop;
    return n;
  };
  auto subtract = [](float a0, float a1, const Sp* by, int nb, float (*o)[2]) -> int {
    float cur[16][2]; int n = 0; cur[n][0] = a0; cur[n][1] = a1; n++;
    for (int b = 0; b < nb; b++) {
      float nxt[16][2]; int m = 0;
      for (int p = 0; p < n; p++) {
        const float lo = cur[p][0], hi = cur[p][1];
        if (by[b].y1 <= lo || by[b].y0 >= hi) { if (m < 16) { nxt[m][0] = lo; nxt[m][1] = hi; m++; } continue; }
        if (by[b].y0 > lo && m < 16) { nxt[m][0] = lo; nxt[m][1] = by[b].y0; m++; }
        if (by[b].y1 < hi && m < 16) { nxt[m][0] = by[b].y1; nxt[m][1] = hi; m++; }
      }
      std::memcpy(cur, nxt, sizeof(float) * 2 * m); n = m;
    }
    int r = 0; for (int p = 0; p < n; p++) if (cur[p][1] - cur[p][0] > 0.01f) { o[r][0] = cur[p][0]; o[r][1] = cur[p][1]; r++; }
    return r;
  };
  struct Seg { float y1, y0; BlockId id; vec3 c; int start; };
  std::vector<Seg> open, cur;
  for (int d = 0; d < 4; d++) {
    // d: 0 -z, 1 +z, 2 -x, 3 +x (block faces 5, 4, 1, 0)
    const int dk = d == 0 ? -W : d == 1 ? W : d == 2 ? -1 : 1, face = d == 0 ? 5 : d == 1 ? 4 : d == 2 ? 1 : 0;
    const int sh = d < 2 ? 205 : 170;
    const vec3 n = d == 0 ? vec3(0, 0, -1) : d == 1 ? vec3(0, 0, 1) : d == 2 ? vec3(-1, 0, 0) : vec3(1, 0, 0);
    for (int line = 0; line < N; line++) {
      open.clear();
      auto emit = [&](const Seg& sg, int a, int b) {
        const float t0 = (float)a * cs, t1 = (float)b * cs;
        vec3 p0, p1;
        if (d < 2) { const float e = (float)(d == 0 ? line : line + 1) * cs; p0 = {t0, 0, e}; p1 = {t1, 0, e}; }
        else { const float e = (float)(d == 2 ? line : line + 1) * cs; p0 = {e, 0, t0}; p1 = {e, 0, t1}; }
        const int foot = footShade(sh, sg.y1, sg.y0);
        M.quad({p0.x, sg.y1, p0.z}, {p1.x, sg.y1, p1.z}, {p1.x, sg.y0, p1.z}, {p0.x, sg.y0, p0.z}, n, faceOf(sg.id, face), sg.c, sh, sh, foot, foot, (u16)(flagsOf(sg.id) | (face << 8)));
      };
      for (int t = 0; t <= N; t++) {
        cur.clear();
        if (t < N) {
          const int i = d < 2 ? t : line, j = d < 2 ? line : t;
          const int k = (j + 1) * W + (i + 1), nk = k + dk;
          const bool edge = d == 0 ? j == 0 : d == 1 ? j == N - 1 : d == 2 ? i == 0 : i == N - 1;
          // at the tile's edge the neighbour's voxels count a skirt lower, so a wall hangs down over any crack to the next
          // tile (which may be finer or coarser: Cubyz draws the faces toward a coarser neighbour for the same reason)
          Sp theirs[16]; const int nt = solidSpans(nk, theirs, 16, edge ? skirt : 0.f);
          for (int r = start[k]; r < start[k + 1]; r++) {
            const BlockId id = runs[r].id;
            if (R.tLiquid[id] && nt > 0 && !edge) continue;   // water and lava: their tops, and walls only at an open edge
            float pc[16][2]; const int np = subtract((float)(base + runs[r].y0), (float)(base + runs[r].y1 + 1), theirs, nt, pc);
            const vec3 c = colourOf(id, k, true);
            for (int p = 0; p < np; p++) cur.push_back({pc[p][1], pc[p][0], id, c, t});
          }
        }
        // carry on the runs that match a piece here, emit the rest
        for (size_t r = 0; r < open.size();) {
          bool kept = false;
          for (Seg& c : cur) if (c.start >= 0 && c.y1 == open[r].y1 && c.y0 == open[r].y0 && c.id == open[r].id && c.c == open[r].c) { c.start = -1; kept = true; break; }
          if (kept) { r++; continue; }
          emit(open[r], open[r].start, t);
          open[r] = open.back(); open.pop_back();
        }
        for (Seg& c : cur) if (c.start >= 0) open.push_back(c);
      }
    }
  }
  out.minY = M.minY < M.maxY ? M.minY : 0; out.maxY = M.maxY;
  out.ms = (nowSeconds() - t0) * 1000;
}

} // namespace dq::lod
