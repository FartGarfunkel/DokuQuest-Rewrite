// render/mesh_ctx.h — internal to the mesher (mesher.cpp, mesh_shapes.cpp, mesh_plants.cpp).
#pragma once
#include "render/mesher.h"
#include "world/shapes.h"

namespace dq {

struct MeshTiles { int size = 32, count = 0; std::vector<u8> rgba; u64 gen = 0;
  const u8* px(int tile, int i, int j) const { return rgba.data() + (((size_t)tile * size + j) * size + i) * 4; } };
std::shared_ptr<const MeshTiles> meshTilesSnapshot();

// Original face table: normal, 4 corners (CCW from outside), shading
struct FaceDef { int n[3]; int c[4][3]; float shade; };
extern const FaceDef FACES[6];
extern const int FACE_UV[4][2];
extern const float AO_CURVE[4];

// the rain puddle (a flat carpet of water): drawn flat, glossy and see-through like water, never as relief
inline bool isPuddle(const Registry& R, BlockId id) { static const BlockId pid = R.blockId("puddle"); return id && id == pid; }

// One vertex before packing. `lit` is markCubyz(pack): bit 31 set means the 5-bit RGB word is real.
// Left at 0, put() builds a grayscale word from sky and blk so plants keep the new brightness.
struct MV { vec3 p; vec2 uv; float sky, blk; float shade; u8 sway; u32 lit = 0; };

struct MeshCtx {
  const MeshInput& in; MeshOutput& out; const MesherOptions& opt; const Registry& R; const MeshTiles* tiles;
  int wx0, wy0, wz0;          // world position of the section's corner
  u32 tint = 0xFFFFFF;        // current tint colour (0xBBGGRR; 0xFFFFFF none): only for merging faces of one climate now
  u8 tintKind = 0;            // what each vertex carries: Tint::None/Grass/Foliage/Water, Tint::Dust + power (mesher.h)
  u8 extraMat = 0;            // OR-ed into every vertex (TintAll for water and dust)
  MeshCtx(const MeshInput& in, MeshOutput& out, const MesherOptions& opt, const Registry& R, const MeshTiles* t);

  int I(int x, int y, int z) const { return MeshInput::idx(x, y, z); }
  bool inside(int y) const { return y >= MeshInput::Y0 && y <= 16; }
  BlockId at(int x, int y, int z) const { return inside(y) ? in.ids[I(x, y, z)] : AIR; }
  u8 metaAt(int x, int y, int z) const { return inside(y) ? in.meta[I(x, y, z)] : 0; }
  u8 carveAt(int x, int y, int z) const { return inside(y) ? in.carve[I(x, y, z)] : 0; }
  u8 coatAt(int x, int y, int z) const { return inside(y) ? in.coat[I(x, y, z)] : 0; }
  u8 skyAt(int x, int y, int z) const { return inside(y) ? in.light[I(x, y, z)] >> 4 : 15; }
  u8 blAt(int x, int y, int z) const { return inside(y) ? in.light[I(x, y, z)] & 15 : 0; }
  bool opaque(int x, int y, int z) const { return R.tOpaque[at(x, y, z)]; }
  // A chisel-carved neighbour does NOT fully cover the faces it shares: its meta high bits mark
  // carved corner octants, so any face shared with it must draw on both sides. Treat carved blocks
  // as non-opaque for culling (they get their own octant-box pass in the Cube case).
  bool opaqueSolid(int x, int y, int z) const {
    BlockId id = at(x, y, z);
    if (!R.tOpaque[id]) return false;
    if (carveAt(x, y, z)) return false;   // carved (dedicated carve byte): never cull against it
    return true;
  }
  bool water(int x, int y, int z) const {
    BlockId b = at(x, y, z);
    if (R.tLiquid[b] == 1) return true;
    if (!(metaAt(x, y, z) & WLOG)) return false;
    return R.tWlog[b] || (R.tOpaque[b] && carveAt(x, y, z));
  }

  // climate tint at a vertex corner (x,z in section-local block units), kind 0 grass 1 foliage 2 water
  u32 cornerTint(float x, float z, int kind);
  u32 tintCache[3 * 19 * 19]; u8 tintSeen[3 * 19 * 19];

  void put(std::vector<ChunkVertex>& d, const MV& v, int tile, int normal, u8 mat) const;
  // quad a,b,c,d (CCW seen from the front). flip = split along b-d instead of a-c (the original's AO rule)
  void quad(std::vector<ChunkVertex>& d, const MV v[4], int tile, int normal, u8 mat, bool flip = false) const;
  void quad2(std::vector<ChunkVertex>& d, const MV v[4], int tile, int normal, u8 mat) const;   // both sides
};

// neighbour access for shapes.cpp
struct MeshShapeNb : ShapeNb {
  const MeshCtx& c; int x, y, z;
  MeshShapeNb(const MeshCtx& c, int x, int y, int z) : c(c), x(x), y(y), z(z) {}
  BlockId id(int dx, int dy, int dz) const override { return c.at(x + dx, y + dy, z + dz); }
  u8 meta(int dx, int dy, int dz) const override { return c.metaAt(x + dx, y + dy, z + dz); }
  u8 carve(int dx, int dy, int dz) const override { return c.carveAt(x + dx, y + dy, z + dz); }
};

// per-kind emitters
void emitShapeBlock(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta);
void emitCarvedBlock(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta);
void emitSnowCaps(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 carve);
void emitModel(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta);
void emitCross(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta);
bool emitVoxelLeafPlate(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta);   // vines, lily pads
// One corner of a door or trapdoor panel in the shut frame (block units, cell min at the origin), before
// the hinge and the facing turn. Four in a row are one quad. sh is 0..255, the same shade the plate bakes.
struct PanelVert { vec3 p; vec2 uv; float sh; };
// which: 0 a door stood against +Z, 1 a trapdoor lying on the floor, 2 a trapdoor against the ceiling.
bool doorPanelQuads(const MeshTiles* tiles, int tile, int which, std::vector<PanelVert>& out);
bool emitVoxelDoor(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta);
void emitPlanes(MeshCtx& c, std::vector<ChunkVertex>& d, int tile, float x, float y, float z, float inset, float scale, float sk, float bl, bool cross);

// 3D grass (Lay of the Land style): short blades over the top of a grass block, swaying, faded out with
// distance in chunk.glsl (sway codes 80..95 mark a tuft's tip). blades per block, 0 = off (settings grass3d).
void emitGrassTufts(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z);
int meshGrassTufts();
void setMeshGrassTufts(int bladesPerBlock);
void applyGrassSetting(const struct Settings& s);   // world_renderer.cpp: settings "grass3d" -> setMeshGrassTufts

// smooth light + AO at the four corners of a full face (same rules as cubes). sky/bl stay 0..15 (the nibble).
// rgb is the Cubyz sample after the darker-neighbour bias; lit is that sample packed, bit 31 set.
struct FaceCorners { float sky[4], bl[4], ao[4]; int aoi[4]; LightRgb rgb[4]; u32 lit[4]; };
void faceCorners(const MeshCtx& c, int x, int y, int z, int f, FaceCorners& fc);

// Apron sample. Outside the gathered neighbourhood: dark below the world, full sun elsewhere.
inline bool meshApron(int x, int y, int z) {
  return x >= -1 && x <= 16 && z >= -1 && z <= 16 && y >= MeshInput::Y0 && y <= 16;
}
inline LightRgb rgbAt(const MeshCtx& c, int x, int y, int z) {
  if (!meshApron(x, y, z)) return y < 0 ? LightRgb{} : fullSunRgb();
  return c.in.rgb[c.I(x, y, z)];
}
// Cubyz smooth bias: along the face normal, a neighbour up to 8 darker pulls this sample down by 2.5 times that gap.
inline void biasLightChannel(u8& a, u8 nxt) {
  if (a <= nxt) return;
  int diff = (int)a - (int)nxt; if (diff > 8) diff = 8;
  int drop = diff * 5 / 2;
  a = (u8)(a > drop ? a - drop : 0);
}
inline void biasRgb(LightRgb& L, const LightRgb& nxt) {
  for (int i = 0; i < 3; i++) { biasLightChannel(L.s[i], nxt.s[i]); biasLightChannel(L.b[i], nxt.b[i]); }
}
inline LightRgb biasedRgb(const MeshCtx& c, int x, int y, int z, int nx, int ny, int nz) {
  LightRgb L = rgbAt(c, x, y, z);
  if (meshApron(x + nx, y + ny, z + nz)) biasRgb(L, rgbAt(c, x + nx, y + ny, z + nz));
  return L;
}

} // namespace dq
