// render/mesh_shapes.cpp — slabs, stairs, fences... (emitShapeBlock/emitBox), static models
// (emitShape + the cloth of game/townlife.js), redstone dust, rails, lever handles, candle flames.
#include "render/mesh_ctx.h"

namespace dq {

namespace {
// where pixel (X,Y,Z) of a box lands in its face's picture (original SHAPE_ST), in pixels
vec2 shapeST(int f, float X, float Y, float Z) {
  switch (f) {
    case 0: return {16 - Z, 16 - Y};
    case 1: return {Z, 16 - Y};
    case 2: return {X, Z};
    case 3: return {16 - X, Z};
    case 4: return {X, 16 - Y};
    default: return {16 - X, 16 - Y};
  }
}
const int FACE_AXIS[6] = {0, 0, 1, 1, 2, 2};

struct Own { float sky, bl, l0; };

void emitBox(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, u8 meta, int x, int y, int z, const ShapeBox& b,
             int sideT, int topT, int botT, float sink, int shape, Own own, u8 mat, u8 forceOnly = 0) {
  const Registry& R = c.R;
  MeshShapeNb nb(c, x, y, z);   // neighbour ids/meta for all 6 directions (carve-aware cull below)
  const float P = 1 / 16.f;
  for (int f = 0; f < 6; f++) {
    bool forced = (forceOnly >> f) & 1;
    if (!(b.only & (1 << f)) && !forced) continue;
    const FaceDef& F = FACES[f];
    bool onB = f == 0 ? b.b[3] >= 16 : f == 1 ? b.b[0] <= 0 : f == 2 ? b.b[4] + sink >= 16 : f == 3 ? b.b[1] + sink <= 0 : f == 4 ? b.b[5] >= 16 : b.b[2] <= 0;
    bool smooth = false;
    FaceCorners fc;
    if (onB) {
      int nx = x + F.n[0], ny = y + F.n[1], nz = z + F.n[2];
      BlockId nbid = c.at(nx, ny, nz);
      u8 nm = c.metaAt(nx, ny, nz);
      bool ncarved = nbid && c.carveAt(nx, ny, nz);   // neighbour carved (dedicated carve byte)
      if (!forced && R.tOpaque[nbid]) {
        if (ncarved) {
          int hx = F.n[0] ? (F.n[0] > 0 ? 1 : 0) : (b.b[0] >= 8 ? 1 : 0);
          int hy = F.n[1] ? (F.n[1] > 0 ? 1 : 0) : (b.b[1] >= 8 ? 1 : 0);
          int hz = F.n[2] ? (F.n[2] > 0 ? 1 : 0) : (b.b[5] >= 8 ? 1 : 0);
          if ((c.carveAt(nx, ny, nz) >> ((hx << 2) | (hy << 1) | hz)) & 1) continue;   // neighbour has material at our exact octant: cull
        } else continue;   // plain opaque neighbour: cull
      }
      // joined panes, walls and fences, and matching slabs, hide the faces they share
      if (nbid == id && f != 2 && f != 3 && (shape == ShapeKind::Pane || shape == ShapeKind::Wall || shape == ShapeKind::Fence ||
          (shape == ShapeKind::Slab && (c.metaAt(nx, ny, nz) & 0x7f) == (meta & 0x7f)))) continue;
      faceCorners(c, x, y, z, f, fc); smooth = true;
    }
    int tile = b.hasFaces && b.faces[f] >= 0 ? b.faces[f] : f == 2 ? topT : f == 3 ? botT : sideT;
    int ax = FACE_AXIS[f], A = ax == 0 ? 1 : 0, Bx = ax == 2 ? 1 : 2;
    MV v[4];
    for (int k = 0; k < 4; k++) {
      const int* cc = F.c[k];
      float X = cc[0] ? b.b[3] : b.b[0], Y = cc[1] ? b.b[4] : b.b[1], Z = cc[2] ? b.b[5] : b.b[2];
      vec2 st = shapeST(f, X, Y, Z);
      float sk, bl, ao = 1;
      if (smooth) {
        float pv[3] = {X / 16, (Y + sink) / 16, Z / 16};
        float pa = clampv(pv[A], 0.f, 1.f), pb = clampv(pv[Bx], 0.f, 1.f);
        sk = bl = ao = 0;
        for (int q = 0; q < 4; q++) {
          const int* cq = F.c[q];
          float wgt = (cq[A] ? pa : 1 - pa) * (cq[Bx] ? pb : 1 - pb);
          sk += fc.sky[q] * wgt; bl += fc.bl[q] * wgt; ao += fc.ao[q] * wgt;
        }
        bl = std::max(bl, own.l0);
      } else { sk = own.sky; bl = own.bl; }
      v[k] = {vec3(x + X * P, y + (Y + sink) * P, z + Z * P), vec2(clampv(st.x, 0.f, 16.f) / 16, clampv(st.y, 0.f, 16.f) / 16), sk, bl, std::min(1.f, F.shade * ao), 0};
    }
    c.quad(d, v, tile, f, mat);
  }
}

// one quad; pts: 4 corners; st: picture fractions
void quadST(MeshCtx& c, std::vector<ChunkVertex>& d, const vec3 p[4], const vec2 st[4], int tile, float sk, float bl, float sh, bool twoSided, u8 mat) {
  MV v[4];
  for (int k = 0; k < 4; k++) v[k] = {p[k], st[k], sk, bl, sh, 0};
  if (twoSided) c.quad2(d, v, tile, NORMAL_NONE, mat); else c.quad(d, v, tile, NORMAL_NONE, mat);
}
// a flat piece over part of the cell; map(a, b) -> picture fraction; hgt(a, b) -> height (or y)
template <class Map, class Hgt>
void flatPiece(MeshCtx& c, std::vector<ChunkVertex>& d, float x, float y, float z, float x0, float z0, float x1, float z1, Map map, int tile, float sk, float bl, Hgt hgt, u8 mat) {
  float C[4][2] = {{x0, z1}, {x1, z1}, {x1, z0}, {x0, z0}};
  vec3 p[4]; vec2 st[4];
  for (int k = 0; k < 4; k++) { p[k] = vec3(x + C[k][0], hgt(C[k][0], C[k][1], y), z + C[k][1]); st[k] = map(C[k][0], C[k][1]); }
  quadST(c, d, p, st, tile, sk, bl, 1.f, false, mat);
}

vec3 dustColor(int power) {
  float f = power / 15.f;
  float r = f * 0.6f + (f > 0 ? 0.4f : 0.3f), g = clampv(f * f * 0.7f - 0.5f, 0.f, 1.f), b = clampv(f * f * 0.6f - 0.7f, 0.f, 1.f);
  return vec3(std::round(r * 204), std::round(g * 204), std::round(b * 204));
}

void emitWire(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta) {
  MeshShapeNb nb(c, x, y, z);
  int cn[4]; wireConnects(nb, cn);
  bool arms[4] = {cn[0] > 0, cn[1] > 0, cn[2] > 0, cn[3] > 0};
  int n = arms[0] + arms[1] + arms[2] + arms[3];
  if (n == 0) arms[0] = arms[1] = arms[2] = arms[3] = true;
  else if (n == 1) for (int h = 0; h < 4; h++) if (cn[h]) arms[(h + 2) & 3] = true;
  int power = meta & 15;
  vec3 col = dustColor(power);
  c.tint = (u32)col.r | (u32)col.g << 8 | (u32)col.b << 16; c.extraMat = Mat::TintAll;
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), power ? (power >> 1) + 1 : 0);
  float Y = y + 0.016f + sinkOffset(id, nb) / 16.f;
  int line = c.R.tile("redstone_dust_line"), dot = c.R.tile("redstone_dust_dot");
  if (line < 0) line = c.R.block(id).faces[0]; if (dot < 0) dot = line;
  auto ns = [](float a, float b) { return vec2(a, b); };
  auto ew = [](float a, float b) { return vec2(b, a); };
  auto flat = [&](float, float, float yy) { return yy; };
  u8 mat = Mat::Cutout | Mat::NoRelief;
  bool straight = (arms[0] && arms[2] && !arms[1] && !arms[3]) || (arms[1] && arms[3] && !arms[0] && !arms[2]);
  if (!straight) flatPiece(c, d, x, Y, z, 0, 0, 1, 1, ns, dot, sk, bl, flat, mat);
  if (arms[0]) flatPiece(c, d, x, Y, z, 0, 0, 1, 0.5f, ns, line, sk, bl, flat, mat);
  if (arms[2]) flatPiece(c, d, x, Y, z, 0, 0.5f, 1, 1, ns, line, sk, bl, flat, mat);
  if (arms[1]) flatPiece(c, d, x, Y, z, 0.5f, 0, 1, 1, ew, line, sk, bl, flat, mat);
  if (arms[3]) flatPiece(c, d, x, Y, z, 0, 0, 0.5f, 1, ew, line, sk, bl, flat, mat);
  // up the side of the next block
  for (int h = 0; h < 4; h++) {
    if (cn[h] != 2) continue;
    const float e = 0.016f;
    vec3 p[4];
    if (h == 0) { p[0] = {x, y, z + e}; p[1] = {x + 1, y, z + e}; p[2] = {x + 1, y + 1, z + e}; p[3] = {x, y + 1, z + e}; }
    else if (h == 2) { p[0] = {x + 1, y, z + 1 - e}; p[1] = {x, y, z + 1 - e}; p[2] = {x, y + 1, z + 1 - e}; p[3] = {x + 1, y + 1, z + 1 - e}; }
    else if (h == 1) { p[0] = {x + 1 - e, y, z}; p[1] = {x + 1 - e, y, z + 1}; p[2] = {x + 1 - e, y + 1, z + 1}; p[3] = {x + 1 - e, y + 1, z}; }
    else { p[0] = {x + e, y, z + 1}; p[1] = {x + e, y, z}; p[2] = {x + e, y + 1, z}; p[3] = {x + e, y + 1, z + 1}; }
    vec2 st[4] = {{1, 0}, {1, 1}, {0, 1}, {0, 0}};
    quadST(c, d, p, st, line, sk, bl, 220 / 255.f, true, mat);
  }
  c.tint = 0xFFFFFF; c.extraMat = 0;
}

void emitRail(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta) {
  const BlockDef& def = c.R.block(id);
  const BlockTraits& T = traits(id);
  int sh = railShape(id, meta); bool on = T.rail != 1 && (meta & 8);
  bool curve = sh >= 6;
  int tile = c.R.tile(curve ? "rail_corner" : (def.key + (on ? "_on" : "")));
  if (tile < 0) tile = def.faces[0];
  MeshShapeNb nb(c, x, y, z);
  float lift = 1 / 16.f + sinkOffset(id, nb) / 16.f;
  auto map = [&](float a, float b) {
    if (curve) { switch (sh - 6) { case 0: return vec2(a, b); case 1: return vec2(1 - a, b); case 2: return vec2(1 - a, 1 - b); default: return vec2(a, 1 - b); } }
    return (sh == 1 || sh == 2 || sh == 3) ? vec2(b, 1 - a) : vec2(a, b);
  };
  auto hgt = [&](float a, float b, float) { return y + lift + (sh == 2 ? a : sh == 3 ? 1 - a : sh == 4 ? 1 - b : sh == 5 ? b : 0); };
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), on && def.str("rail") == "powered" ? 4 : 0);
  u8 mat = Mat::Cutout | Mat::NoRelief;
  flatPiece(c, d, x, y, z, 0, 0, 1, 1, map, tile, sk, bl, hgt, mat);
  if (sh >= 2 && sh <= 5) flatPiece(c, d, x, y, z, 0, 0, 1, 1, map, tile, sk, bl, [&](float a, float b, float yy) { return hgt(a, b, yy) - 0.02f; }, mat);
}

// lever handles: a stick tilted one way when off, the other way when on
void emitOBB(MeshCtx& c, std::vector<ChunkVertex>& d, vec3 ctr, vec3 ax, vec3 ay, vec3 az, float hx, float hy, float hz, int tile, float sk, float bl) {
  auto P = [&](float sx, float sy, float sz) { return ctr + ax * hx * sx + ay * hy * sy + az * hz * sz; };
  vec2 st[4] = {{0.4f, 1}, {0.6f, 1}, {0.6f, 0}, {0.4f, 0}};
  vec2 cap[4] = {{0.4f, 0}, {0.6f, 0}, {0.6f, 0.12f}, {0.4f, 0.12f}};
  u8 mat = Mat::Cutout | Mat::NoRelief;
  { vec3 p[4] = {P(1, -1, 1), P(1, -1, -1), P(1, 1, -1), P(1, 1, 1)}; quadST(c, d, p, st, tile, sk, bl, 210 / 255.f, true, mat); }
  { vec3 p[4] = {P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1)}; quadST(c, d, p, st, tile, sk, bl, 210 / 255.f, true, mat); }
  { vec3 p[4] = {P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1)}; quadST(c, d, p, st, tile, sk, bl, 180 / 255.f, true, mat); }
  { vec3 p[4] = {P(1, -1, -1), P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1)}; quadST(c, d, p, st, tile, sk, bl, 180 / 255.f, true, mat); }
  { vec3 p[4] = {P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), P(-1, 1, -1)}; quadST(c, d, p, cap, tile, sk, bl, 1.f, true, mat); }
}
void emitLeverHandle(MeshCtx& c, std::vector<ChunkVertex>& d, int x, int y, int z, u8 meta, Own own) {
  float a = 0.7f * ((meta & 8) ? 1 : -1);
  vec3 dir(0, std::cos(a), std::sin(a));
  vec3 base(8, 1.5f, 8); float len = 10;
  vec3 ctr = attachPoint(base + dir * len / 2.f, meta, false);
  vec3 ay = attachPoint(dir, meta, true), ax = attachPoint(vec3(1, 0, 0), meta, true);
  vec3 az = glm::cross(ax, ay);
  int t = c.R.tile("lever_handle");
  emitOBB(c, d, vec3(x, y, z) + ctr / 16.f, ax, ay, az, 1 / 16.f, len / 32, 1 / 16.f, t < 0 ? 0 : t, own.sky, own.bl);
}
// a small two-plane flame (candles), centred on (cx, cz) and standing on cy
void emitFlame(MeshCtx& c, std::vector<ChunkVertex>& d, int tile, float cx, float cy, float cz, float w, float h, float sk, float bl) {
  u8 mat = Mat::Cutout | Mat::NoRelief | Mat::Emissive;
  auto pl = [&](float ax, float az, float bx, float bz) {
    MV v[4] = {{vec3(cx + ax, cy, cz + az), {0, 1}, sk, bl, 1, 0}, {vec3(cx + bx, cy, cz + bz), {1, 1}, sk, bl, 1, 0},
               {vec3(cx + bx, cy + h, cz + bz), {1, 0}, sk, bl, 1, 0}, {vec3(cx + ax, cy + h, cz + az), {0, 0}, sk, bl, 1, 0}};
    c.quad2(d, v, tile, NORMAL_NONE, mat);
  };
  pl(-w / 2, 0, w / 2, 0); pl(0, -w / 2, 0, w / 2);
}

// a model box (block models: lanterns, anvils, furniture...): every face drawn, flat light
void addShapeBox(MeshCtx& c, std::vector<ChunkVertex>& d, const ShapeBox& box, vec3 o, float unit, float sk, float bl, float lift, int rot, u8 mat,
                 const std::function<u8(vec3)>* swayFn = nullptr) {
  int side = box.side >= 0 ? box.side : 0, end = box.end >= 0 ? box.end : side;
  static int chainTile = -2;
  if (chainTile == -2) chainTile = c.R.tile("chain");
  if (chainTile >= 0 && box.side == chainTile) {
    // a chain is drawn like Minecraft's: two crossed planes along the box's long axis, showing the picture's two link
    // strips (columns 0-3 and 3-6). As a thin box it only showed the picture's empty middle and looked invisible.
    float ext[3] = {box.b[3] - box.b[0], box.b[4] - box.b[1], box.b[5] - box.b[2]};
    int ax = ext[1] >= ext[0] && ext[1] >= ext[2] ? 1 : ext[0] >= ext[2] ? 0 : 2, p = (ax + 1) % 3, q = (ax + 2) % 3;
    float cp = (box.b[p] + box.b[p + 3]) / 2, cq = (box.b[q] + box.b[q + 3]) / 2, a0 = box.b[ax], a1 = box.b[ax + 3], hw = 1.5f;
    for (int k = 0; k < 2; k++) {
      float sq = k ? -1.f : 1.f, u0 = k ? 3.f : 0.f, u1 = u0 + 3;
      MV v[4];
      for (int i = 0; i < 4; i++) {
        float s = (i == 0 || i == 3) ? -1.f : 1.f, along = (i < 2) ? a0 : a1;
        float P[3]; P[ax] = along; P[p] = cp + s * hw; P[q] = cq + s * hw * sq;
        vec3 pos = o + vec3(P[0], P[1] + lift, P[2]) * unit;
        v[i] = {pos, vec2(s < 0 ? u0 : u1, 16 - along) / 16.f, sk, bl, 1.f, swayFn ? (*swayFn)(pos) : (u8)0};
      }
      c.quad2(d, v, chainTile, NORMAL_NONE, (u8)(mat | Mat::Cutout | Mat::NoRelief));
    }
    return;
  }
  int xt = box.xt >= 0 ? box.xt : side, xf = (rot & 1) ? 4 : 0;
  for (int f = 0; f < 6; f++) {
    const FaceDef& F = FACES[f];
    int tile = f == 2 || f == 3 ? end : (f == xf || f == xf + 1) ? xt : side;
    MV v[4];
    for (int k = 0; k < 4; k++) {
      const int* cc = F.c[k];
      float X = cc[0] ? box.b[3] : box.b[0], Y = cc[1] ? box.b[4] : box.b[1], Z = cc[2] ? box.b[5] : box.b[2];
      vec2 st = shapeST(f, X, Y, Z);
      if (box.tv && f != 2 && f != 3) st.y += box.tv;
      vec3 p = o + vec3(X, Y + lift, Z) * unit;
      v[k] = {p, st / 16.f, sk, bl, std::min(1.f, F.shade + 0.06f), swayFn ? (*swayFn)(p) : (u8)0};
    }
    c.quad(d, v, tile, f, mat);
  }
}
} // namespace

void emitPlanes(MeshCtx& c, std::vector<ChunkVertex>& d, int tile, float x, float y, float z, float inset, float scale, float sk, float bl, bool cross) {
  float h = scale, a0 = inset, a1 = 1 - inset;
  u8 mat = Mat::Cutout | Mat::NoRelief;
  auto pl = [&](float ax, float az, float bx, float bz) {
    MV v[4] = {{vec3(x + ax, y, z + az), {0, 1}, sk, bl, 1, 0}, {vec3(x + bx, y, z + bz), {1, 1}, sk, bl, 1, 0},
               {vec3(x + bx, y + h, z + bz), {1, 0}, sk, bl, 1, 0}, {vec3(x + ax, y + h, z + az), {0, 0}, sk, bl, 1, 0}};
    c.quad2(d, v, tile, NORMAL_NONE, mat);
  };
  if (cross) { pl(a0, a0, a1, a1); pl(a0, a1, a1, a0); }
  else { pl(0, a0, 1, a0); pl(0, a1, 1, a1); pl(a0, 0, a0, 1); pl(a1, 0, a1, 1); }
}

void emitShapeBlock(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta) {
  const Registry& R = c.R;
  const BlockDef& self = R.block(id);
  const BlockTraits& T = traits(id);
  int kind = R.tShape[id];
  if (kind == ShapeKind::Wire) { emitWire(c, d, id, x, y, z, meta); return; }
  if (kind == ShapeKind::Rail) { emitRail(c, d, id, x, y, z, meta); return; }
  // vines and lily pads become little voxel plates (3D plants)
  if (c.opt.detail && T.tint && (kind == ShapeKind::Ladder || kind == ShapeKind::Carpet) && emitVoxelLeafPlate(c, d, id, x, y, z, meta)) return;
  MeshShapeNb nb(c, x, y, z);
  thread_local std::vector<ShapeBox> boxes;
  if (!shapeBoxes(id, meta, nb, ShapePurpose::Draw, boxes)) return;
  int side = self.faces[0], top = self.faces[2], bot = self.faces[3];
  if (T.door && (meta & 16) && T.upperTile >= 0) side = top = bot = T.upperTile;
  if (T.door) top = bot = side;
  float sink = (float)sinkOffset(id, nb);
  float L0 = R.metaLight(id, meta);
  Own own{(float)c.skyAt(x, y, z), std::max<float>(c.blAt(x, y, z), L0), L0};
  if (T.tint) c.tint = c.cornerTint((float)x + 1, (float)z + 1, T.tint == 2 ? 1 : 0);
  u8 mat = Mat::Cutout | (R.tTrans[id] ? Mat::Translucent : 0);
  if (isPuddle(R, id)) mat = Mat::Liquid | Mat::Translucent | Mat::NoRelief;   // water: flat, glossy, sky-tinted highlight
  for (auto& b : boxes) {
    int st = b.side >= 0 ? b.side : side;
    int tt = b.end >= 0 ? b.end : top, bt = b.end >= 0 ? b.end : bot;
    Own o = b.glow ? Own{own.sky, std::max<float>(own.bl, b.glow), (float)b.glow} : own;
    emitBox(c, d, id, meta, x, y, z, b, st, tt, bt, sink, kind, o, mat);
  }
  c.tint = 0xFFFFFF;
  if (kind == ShapeKind::Lever) emitLeverHandle(c, d, x, y, z, meta, own);
  // lit candles: a little flame on every wick
  if (T.candle && (meta & 4)) {
    int ft = R.tile("candle_flame");
    if (ft >= 0) for (auto& b : boxes) emitFlame(c, d, ft, x + (b.b[0] + b.b[3]) / 32, y + (b.b[4] + sink) / 16, z + (b.b[2] + b.b[5]) / 32, 0.11f, 0.2f, own.sky, 15);
  }
}

// A full block whose high meta nibble carved some of its 8 corners (chisel): the eight octant
// boxes that remain, with the same face tiles as the plain cube path (emitCube).
void emitCarvedBlock(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 carveMask) {
  const Registry& R = c.R;
  const BlockDef& self = R.block(id);
  MeshShapeNb nb(c, x, y, z);
  thread_local std::vector<ShapeBox> boxes;
  if (!carveBoxes(boxes, carveMask, nb)) return;   // caller gate already checked the carve flag
  int side = self.faces[0], top = self.faces[2], bot = self.faces[3];
  Own own{(float)c.skyAt(x, y, z), std::max<float>((float)c.blAt(x, y, z), (float)R.tLight[id]), (float)R.metaLight(id, c.metaAt(x, y, z))};
  u8 mat = Mat::Cutout | (R.tTrans[id] ? Mat::Translucent : 0);
  for (auto& b : boxes)
    emitBox(c, d, id, c.metaAt(x, y, z), x, y, z, b, side, top, bot, 0, /*shape=*/0, own, mat, b.only);
}

void emitModel(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta) {
  const Registry& R = c.R;
  const BlockTraits& T = traits(id);
  u8 mat = Mat::Cutout;
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), R.tLight[id]);
  float lift = 0;
  if (R.tSink[id] && R.tShape[c.at(x, y - 1, z)] == ShapeKind::Slab && (c.metaAt(x, y - 1, z) & 7) == 0) lift -= 8;
  int rf = T.facingModel ? meta & 3 : 0;
  vec3 o(x, y, z);
  if (T.cloth) {
    // flags, banners, bunting: each vertex learns how far it is from what holds it, and the shader flaps it
    thread_local std::vector<ShapeBox> cb; clothBoxes(id, meta, cb);
    for (auto& box0 : cb) {
      ShapeBox box = box0; rotBox(box, rf);
      u8 k = box.kind;
      std::function<u8(vec3)> fn = [&](vec3 p) -> u8 {
        float vx = p.x - x, vy = p.y - y, vz = p.z - z;
        if (k == 0) return 0;
        if (k == 1) { float dd = rf == 0 ? 1 - vz : rf == 1 ? vx : rf == 2 ? vz : 1 - vx; return (u8)(((rf & 1) ? 48 : 64) + std::lround(clampv(dd, 0.f, 1.f) * 15)); }
        if (k == 2) { float dd = (meta & 4) ? 0.45f + (1 - vy) * 0.55f : (1 - vy) * 0.45f; return (u8)(((rf & 1) ? 64 : 48) + std::lround(clampv(dd, 0.f, 1.f) * 15)); }
        if (k == 3) { float dd = clampv((14 / 16.f - vy) / 0.55f, 0.f, 1.f) * 0.85f + 0.1f; return (u8)(((rf & 1) ? 64 : 48) + std::lround(dd * 15)); }
        if (k == 4) return Sway::Hang + 1;
        return (u8)(Sway::Hang + std::lround(clampv((1 - vy) * 1.1f, 0.f, 1.f) * 11));
      };
      addShapeBox(c, d, box, o, 1 / 16.f, sk, bl, box.lift, rf, mat | Mat::NoRelief, &fn);
    }
    return;
  }
  bool hang = hangingLantern(id, meta);
  bool out = T.campfire && (meta & 8);
  int mi = hang && T.modelHang >= 0 ? T.modelHang : out && T.modelOut >= 0 ? T.modelOut : T.model;
  const std::vector<ShapeBox>* boxes = modelShape(mi);
  if (!boxes) return;
  // a hanging lantern swings gently: the further below the hook, the more it moves
  std::function<u8(vec3)> hangFn = [&](vec3 p) -> u8 { return (u8)(Sway::Hang + std::lround(clampv(y + 1 - p.y, 0.f, 1.f) * 11)); };
  for (auto& b0 : *boxes) {
    ShapeBox b = b0; if (rf) rotBox(b, rf);
    addShapeBox(c, d, b, o, 1 / 16.f, sk, bl, lift, rf, mat, hang ? &hangFn : nullptr);
  }
  if (T.campfire && !out) {
    int ft = R.tile(T.campfire == 1 ? "campfire_fire" : "soul_campfire_fire");
    if (ft >= 0) emitPlanes(c, d, ft, (float)x, y + lift / 16, (float)z, 0.08f, 1, 15, 15, false);
  }
  if (T.cauldron && (meta & 3)) {
    // water in a cauldron
    c.tint = c.cornerTint((float)x + 1, (float)z + 1, 2); c.extraMat = Mat::TintAll;
    ShapeBox wb(2, 4, 2, 14, 4 + (meta & 3) * 3.6f, 14); wb.side = wb.end = R.tile("water");
    addShapeBox(c, d, wb, o, 1 / 16.f, sk, bl, 0, 0, Mat::NoRelief);
    c.tint = 0xFFFFFF; c.extraMat = 0;
  }
  if (T.pot) {
    int idx = MeshInput::idx(x, y, z);
    for (auto& [i, plant] : c.in.potPlants) if (i == idx) emitPlanes(c, d, R.block(plant).faces[0], (float)x, y + (lift + 4) / 16, (float)z, 0.25f, 0.75f, sk, bl, true);
  }
}

} // namespace dq
