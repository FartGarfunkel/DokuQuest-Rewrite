// render/mesh_shapes.cpp — slabs, stairs, fences... (emitShapeBlock/emitBox), static models
// (emitShape + the cloth of game/townlife.js), redstone dust, rails, lever handles, candle flames.
#include "render/mesh_ctx.h"
#include "world/light_color.h"
#include "world/carve_faces.h"

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

// Doors and trapdoors: both big faces carry one print, so the handle is on the same edge from either side.
// The swing draws that print on the shut panel and turns it; this undoes facing and the hinge so the resting
// mesh shows the same texels on the same wood (no snap when the swing ends, open or shut).
void unrotDoor(float& x, float& z, int facing) {
  if (facing == 1) { float nx = z, nz = 16 - x; x = nx; z = nz; }
  else if (facing == 2) { x = 16 - x; z = 16 - z; }
  else if (facing == 3) { float nx = 16 - z, nz = x; x = nx; z = nz; }
}
int unfaceDoor(int face, int facing) {
  // inverse of one rotBox step: +X→+Z, -X→-Z, +Z→-X, -Z→+X
  static const int inv[6] = {5, 4, 2, 3, 0, 1};
  for (int i = 0; i < (facing & 3); i++) face = inv[face];
  return face;
}
vec2 panelST(int shape, u8 meta, int face, float X, float Y, float Z) {
  float x = X, y = Y, z = Z;
  const int facing = meta & 3;
  unrotDoor(x, z, facing);
  face = unfaceDoor(face, facing);
  const bool open = meta & 4;
  if (shape == ShapeKind::Door) {
    if (open) {
      const bool right = meta & 8;
      // open-frame face back to the shut panel (left hinge is +90° about Y, right is -90°)
      static const int fromLeft[6] = {4, 5, 2, 3, 1, 0};    // +X→+Z, -X→-Z, +Z→-X, -Z→+X
      static const int fromRight[6] = {5, 4, 2, 3, 0, 1};   // +X→-Z, -X→+Z, +Z→+X, -Z→-X
      const float ox = x, oz = z;
      if (right) { x = oz; z = 29 - ox; }
      else { x = 16 - oz; z = ox + 13; }
      face = (right ? fromRight : fromLeft)[face];
    }
    if (face == 4 || face == 5) return {x, 16 - y};   // both sides of the door
    return shapeST(face, x, y, z);
  }
  if (open) {
    const bool high = meta & 8;
    // open-frame face → shut-panel face. Low hinge (-90° X): -Z came from +Y, +Z from -Y.
    static const int fromLow[6] = {0, 1, 4, 5, 3, 2};
    static const int fromHigh[6] = {0, 1, 5, 4, 2, 3};
    const float oy = y, oz = z;
    if (high) { y = oz + 13; z = 16 - oy; }
    else { y = 3 - oz; z = oy; }
    face = (high ? fromHigh : fromLow)[face];
  }
  if (face == 2 || face == 3) return {x, z};   // both sides of the trapdoor
  return shapeST(face, x, y, z);
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
      vec2 st = (shape == ShapeKind::Door || shape == ShapeKind::Trapdoor) ? panelST(shape, meta, f, X, Y, Z) : shapeST(f, X, Y, Z);
      // A lightning rod's picture is copper in columns 0-2. The thin box sits on columns 7-9, which are empty.
      if (shape == ShapeKind::Rod && b.tu == 0.f && !b.uvT) {
        float ext[3] = {b.b[3] - b.b[0], b.b[4] - b.b[1], b.b[5] - b.b[2]};
        int L = (ext[1] >= ext[0] && ext[1] >= ext[2]) ? 1 : (ext[0] >= ext[2] ? 0 : 2);
        int N = FACE_AXIS[f];
        float P[3] = {X, Y, Z};
        if (N == L) {
          int a = (L + 1) % 3, c3 = (L + 2) % 3;
          st = vec2(P[a] - 7.f, P[c3] - 7.f);
        } else {
          int S = 3 - N - L;
          st = vec2(P[S] - 7.f, 16.f - P[L]);
        }
      }
      if (b.tu || b.uvT) { if (b.uvT) std::swap(st.x, st.y); st += vec2(b.tu, b.tv); }   // (model boxes' tv is addShapeBox's)
      float sk, bl, ao = 1.f;
      LightRgb sample;
      if (smooth) {
        float pv[3] = {X / 16, (Y + sink) / 16, Z / 16};
        float pa = clampv(pv[A], 0.f, 1.f), pb = clampv(pv[Bx], 0.f, 1.f);
        sk = bl = ao = 0;
        float ss[3] = {}, sb[3] = {};
        for (int q = 0; q < 4; q++) {
          const int* cq = F.c[q];
          float wgt = (cq[A] ? pa : 1 - pa) * (cq[Bx] ? pb : 1 - pb);
          sk += fc.sky[q] * wgt; bl += fc.bl[q] * wgt; ao += fc.ao[q] * wgt;
          for (int i = 0; i < 3; i++) { ss[i] += fc.rgb[q].s[i] * wgt; sb[i] += fc.rgb[q].b[i] * wgt; }
        }
        bl = std::max(bl, own.l0);
        for (int i = 0; i < 3; i++) {
          sample.s[i] = (u8)clampv((int)std::lround(ss[i]), 0, 255);
          sample.b[i] = (u8)clampv((int)std::lround(sb[i]), 0, 255);
        }
      } else { sk = own.sky; bl = own.bl; sample = rgbAt(c, x, y, z); }
      { u8 em[3]; blockLightRgb(id, (u8)clampv((int)std::lround(own.l0), 0, 15), em); for (int i = 0; i < 3; i++) if (em[i] > sample.b[i]) sample.b[i] = em[i]; }
      float shade = FACES[f].shade * ao;
      v[k] = {vec3(x + X * P, y + (Y + sink) * P, z + Z * P), vec2(clampv(st.x, 0.f, 16.f) / 16, clampv(st.y, 0.f, 16.f) / 16), sk, bl, shade, 0, markCubyz(packCubyzLight(sample))};
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
  c.tint = (u32)col.r | (u32)col.g << 8 | (u32)col.b << 16; c.tintKind = (u8)(Tint::Dust + power); c.extraMat = Mat::TintAll;
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), power ? (power >> 1) + 1 : 0);
  float Y = y + 0.016f + sinkOffset(id, nb) / 16.f;
  int line = c.R.tile("redstone_dust_line"), dot = c.R.tile("redstone_dust_dot");
  if (line < 0) line = c.R.block(id).faces[0]; if (dot < 0) dot = line;
  // Near the player the dust is coloured voxels. The power paints the grey picture the same red.
  // Far dust stays on the chunk picture. A wire on a bottom slab keeps that drop.
  if (line >= 0) c.out.hasTorch = true;
  if (line >= 0 && c.opt.torchVoxels) {
    CandleSpot s;
    s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
    s.tile = (u16)line;
    s.top = (u16)std::max(dot, 0);
    u8 bits = 0;
    for (int h = 0; h < 4; h++) {
      if (arms[h]) bits = (u8)(bits | (1u << h));
      if (cn[h] == 2) bits = (u8)(bits | (1u << (h + 4)));
    }
    s.only = bits;
    s.faceRot = (u16)(power & 15);
    s.sink = (float)sinkOffset(id, nb);
    s.flame = -8;
    c.out.candles.push_back(s);
    c.tint = 0xFFFFFF; c.tintKind = 0; c.extraMat = 0;
    return;
  }
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
  c.tint = 0xFFFFFF; c.tintKind = 0; c.extraMat = 0;
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
  // Near the player the rail is coloured voxels. The picture, the turn and the slope stay as they are.
  // Far rails stay on the chunk picture. A rail on a bottom slab keeps that drop.
  if (tile >= 0) c.out.hasTorch = true;
  if (tile >= 0 && c.opt.torchVoxels) {
    CandleSpot s;
    s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
    s.tile = (u16)tile;
    s.only = (u8)(sh & 15);
    s.faceRot = (on && def.str("rail") == "powered") ? 1 : 0;
    s.sink = (float)sinkOffset(id, nb);
    s.flame = -9;
    c.out.candles.push_back(s);
    return;
  }
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
  SeatShift sh = seatShiftOf(c.at(x, y, z), meta, c.carveAt(x, y, z), MeshShapeNb(c, x, y, z));
  vec3 seat = sh.micro ? vec3(sh.x, sh.y, sh.z) / 16.f : vec3(0);
  emitOBB(c, d, vec3(x, y, z) + ctr / 16.f + seat, ax, ay, az, 1 / 16.f, len / 32, 1 / 16.f, t < 0 ? 0 : t, own.sky, own.bl);
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
    const float vbase = std::floor(a0 / 16.f) * 16.f;   // a long chain comes in block-long pieces: each takes the picture from its own block's start
    for (int k = 0; k < 2; k++) {
      float sq = k ? -1.f : 1.f, u0 = k ? 3.f : 0.f, u1 = u0 + 3;
      MV v[4];
      for (int i = 0; i < 4; i++) {
        float s = (i == 0 || i == 3) ? -1.f : 1.f, along = (i < 2) ? a0 : a1;
        float P[3]; P[ax] = along; P[p] = cp + s * hw; P[q] = cq + s * hw * sq;
        vec3 pos = o + vec3(P[0], P[1] + lift, P[2]) * unit;
        v[i] = {pos, vec2(s < 0 ? u0 : u1, 16 - (along - vbase)) / 16.f, sk, bl, 1.f, swayFn ? (*swayFn)(pos) : (u8)0};
      }
      c.quad2(d, v, chainTile, NORMAL_NONE, (u8)(mat | Mat::Cutout | Mat::NoRelief));
    }
    return;
  }
  int xt = box.xt >= 0 ? box.xt : side, xf = (rot & 1) ? 4 : 0, zf = (rot & 1) ? 0 : 4;
  for (int f = 0; f < 6; f++) {
    const FaceDef& F = FACES[f];
    const bool onX = f == xf || f == xf + 1, onZ = box.zt >= 0 && (f == zf || f == zf + 1);
    int tile = f == 2 || f == 3 ? end : onX ? xt : onZ ? box.zt : side;
    // a log's end shows its whole ring however small the face; a log's top runs its grain along the log
    const bool fitF = box.fit && f != 2 && f != 3 && ((onX && box.xt >= 0) || onZ);
    const bool turn = box.grain && (f == 2 || f == 3) && ((box.grain == 2) != !!(rot & 1));
    MV v[4];
    for (int k = 0; k < 4; k++) {
      const int* cc = F.c[k];
      float X = cc[0] ? box.b[3] : box.b[0], Y = cc[1] ? box.b[4] : box.b[1], Z = cc[2] ? box.b[5] : box.b[2];
      vec2 st = shapeST(f, X, Y, Z);
      if (box.tv && f != 2 && f != 3) st.y += box.tv;
      if (turn) std::swap(st.x, st.y);
      vec3 p = o + vec3(X, Y + lift, Z) * unit;
      v[k] = {p, st / 16.f, sk, bl, FACES[f].shade, swayFn ? (*swayFn)(p) : (u8)0};
    }
    if (fitF) {   // stretch the picture over the face
      vec2 mn(1e9f), mx(-1e9f);
      for (auto& q : v) { mn = glm::min(mn, q.uv); mx = glm::max(mx, q.uv); }
      for (auto& q : v) q.uv = (q.uv - mn) / glm::max(mx - mn, vec2(1e-4f));
    }
    c.quad(d, v, tile, f, fitF ? (u8)(mat | Mat::NoRelief) : mat);
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
  // vines and lily pads, and every ladder. The ladder's rungs are real geometry, so looking up or down
  // the wall still shows the whole mesh. Plant detail only decides the vines and pads.
  // Near the player a ladder or glow lichen is coloured voxels on that wall. A vine keeps the
  // chunk plate, so the climate still colours it and it can sway. Far ladders stay on the chunk picture.
  if (kind == ShapeKind::Ladder && !T.tint) {
    c.out.hasTorch = true;
    if (c.opt.torchVoxels) {
      CandleSpot s;
      s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
      s.tile = self.faces[0];
      s.only = (u8)(meta & 3);
      s.flame = -4;
      c.out.candles.push_back(s);
      return;
    }
  }
  if ((kind == ShapeKind::Ladder || (c.opt.detail && T.tint && kind == ShapeKind::Carpet)) && emitVoxelLeafPlate(c, d, id, x, y, z, meta)) return;
  // A door is the same kind of cutout as a ladder: the relief march walks into the window and the
  // wood drops out when you look along it. The plate is real wood, and the swing turns this same plate.
  if ((kind == ShapeKind::Door || kind == ShapeKind::Trapdoor) && emitVoxelDoor(c, d, id, x, y, z, meta)) return;
  MeshShapeNb nb(c, x, y, z);
  thread_local std::vector<ShapeBox> boxes;
  if (!shapeBoxes(id, meta, nb, ShapePurpose::Draw, boxes)) return;
  int side = self.faces[0], top = self.faces[2], bot = self.faces[3];
  if (T.door && (meta & 16) && T.upperTile >= 0) side = top = bot = T.upperTile;
  if (T.door) top = bot = side;
  SeatShift sh = seatShiftOf(id, meta, c.carveAt(x, y, z), nb);
  float sink = sh.micro ? sh.y : (float)sinkOffset(id, nb);
  float sox = sh.micro ? sh.x : 0, soz = sh.micro ? sh.z : 0;
  float L0 = R.metaLight(id, meta);
  Own own{(float)c.skyAt(x, y, z), std::max<float>(c.blAt(x, y, z), L0), L0};
  if (T.tint) { c.tint = c.cornerTint((float)x + 1, (float)z + 1, T.tint == 2 ? 1 : 0); c.tintKind = (u8)(1 + (T.tint == 2 ? 1 : 0)); }
  // Candles, buttons, plates and a lever's base are the same little crop of one picture. Near the
  // player that crop is coloured voxels (torch_layer.cpp). A lit candle still carries its flame,
  // and a lever still carries its tilted stick.
  if (kind == ShapeKind::Candle || kind == ShapeKind::Button || kind == ShapeKind::Plate || kind == ShapeKind::Lever) {
    c.out.hasTorch = true;
    if (c.opt.torchVoxels) {
      int ft = (kind == ShapeKind::Candle && T.candle && (meta & 4)) ? R.tile("candle_flame") : -1;
      for (auto& b0 : boxes) {
        ShapeBox b = b0;
        b.b[0] += sox; b.b[3] += sox; b.b[2] += soz; b.b[5] += soz;
        CandleSpot s;
        s.x0 = b.b[0]; s.y0 = b.b[1]; s.z0 = b.b[2]; s.x1 = b.b[3]; s.y1 = b.b[4]; s.z1 = b.b[5];
        s.sink = sink;
        s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
        int st = b.side >= 0 ? b.side : side;
        s.tile = s.top = s.bot = (u16)std::max(st, 0);
        s.nx = s.pz = s.nz = s.tile;
        s.only = b.only;
        s.flame = (i16)ft;
        c.out.candles.push_back(s);
      }
      if (kind == ShapeKind::Lever) {
        float a = 0.7f * ((meta & 8) ? 1.f : -1.f);
        vec3 dir(0.f, std::cos(a), std::sin(a));
        vec3 ctr = attachPoint(vec3(8.f, 1.5f, 8.f) + dir * 5.f, meta, false);
        vec3 ay = attachPoint(dir, meta, true);
        vec3 ax = attachPoint(vec3(1.f, 0.f, 0.f), meta, true);
        vec3 az = glm::cross(ax, ay);
        vec3 seat = sh.micro ? vec3(sh.x, sh.y, sh.z) / 16.f : vec3(0);
        vec3 cpos = vec3((float)x, (float)y, (float)z) + ctr / 16.f + seat;
        LeverSpot L;
        L.x = cpos.x; L.y = cpos.y; L.z = cpos.z;
        L.xx = ax.x; L.xy = ax.y; L.xz = ax.z;
        L.yx = ay.x; L.yy = ay.y; L.yz = ay.z;
        L.zx = az.x; L.zy = az.y; L.zz = az.z;
        L.cx = (u8)x; L.cy = (u8)y; L.cz = (u8)z;
        int ht = R.tile("lever_handle");
        L.tile = (u16)std::max(ht, 0);
        c.out.levers.push_back(L);
      }
      c.tint = 0xFFFFFF; c.tintKind = 0;
      return;
    }
  }
  // An end rod's picture is a strip. Near the player that strip is coloured voxels (the base too).
  // A lightning rod's copper is the left of its picture. Near the player that stick is coloured voxels.
  if (kind == ShapeKind::Rod && T.endRod) {
    c.out.hasTorch = true;
    if (c.opt.torchVoxels) {
      RodSpot s;
      s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
      int ax = meta & 3;
      s.axis = (u8)(ax == 1 || ax == 2 ? ax : 0);
      s.sink = sink;
      s.ox = sox / 16.f; s.oz = soz / 16.f;
      s.tile = (u16)std::max(side, 0);
      c.out.rods.push_back(s);
      c.tint = 0xFFFFFF; c.tintKind = 0;
      return;
    }
  }
  if (kind == ShapeKind::Rod && !T.endRod) {
    c.out.hasTorch = true;
    if (c.opt.torchVoxels) {
      RodSpot s;
      s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
      int ax = meta & 3;
      s.axis = (u8)((ax == 1 || ax == 2 ? ax : 0) | 0x80);
      s.sink = sink;
      s.ox = sox / 16.f; s.oz = soz / 16.f;
      s.tile = (u16)std::max(side, 0);
      c.out.rods.push_back(s);
      c.tint = 0xFFFFFF; c.tintKind = 0;
      return;
    }
  }
  // Scaffolding is poles and a plank. Near the player that is one coloured mesh (torch_layer.cpp
  // treats flame -2 as that mesh). Farther off it stays on the chunk mesh.
  if (kind == ShapeKind::Scaffold) {
    c.out.hasTorch = true;
    if (c.opt.torchVoxels) {
      CandleSpot s;
      s.x0 = 0; s.y0 = 0; s.z0 = 0; s.x1 = 16; s.y1 = 16; s.z1 = 16;
      s.sink = sink;
      s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
      s.tile = (u16)std::max(side, 0);
      s.top = (u16)std::max(top, 0);
      s.bot = (u16)std::max(bot, 0);
      s.flame = -2;
      c.out.candles.push_back(s);
      c.tint = 0xFFFFFF; c.tintKind = 0;
      return;
    }
  }
  // A cake's icing, a detector's face, a hopper's rim and a bed's pillow each keep the picture the
  // box already used. A tripwire, a chorus stem, a repeater, a piston and a cactus do too.
  // Clear glass and iron bars do too. Stained glass stays on the chunk picture: its colour is a see-through wash.
  // A cocoa pod keeps the ripe picture. A young pod is a smaller crop of that same picture.
  // A fence gate's posts and rails do too. Near the player those boxes
  // are coloured voxels; farther off they stay on the chunk mesh.
  if (kind == ShapeKind::Cake || kind == ShapeKind::Daylight || kind == ShapeKind::Hopper
      || kind == ShapeKind::Composter || kind == ShapeKind::Frame || kind == ShapeKind::Hook
      || kind == ShapeKind::Bed || kind == ShapeKind::Tripwire || kind == ShapeKind::Chorus
      || kind == ShapeKind::Repeater || kind == ShapeKind::Comparator
      || kind == ShapeKind::PistonBase || kind == ShapeKind::PistonHead || kind == ShapeKind::Cactus
      || kind == ShapeKind::Cocoa || kind == ShapeKind::Gate
      || (kind == ShapeKind::Pane && self.key.find("stained") == std::string::npos)) {
    c.out.hasTorch = true;
    if (c.opt.torchVoxels) {
      for (auto& b0 : boxes) {
        ShapeBox b = b0;
        b.b[0] += sox; b.b[3] += sox; b.b[2] += soz; b.b[5] += soz;
        int st = b.hasFaces && b.faces[0] >= 0 ? b.faces[0] : b.side >= 0 ? b.side : side;
        int nt = b.hasFaces && b.faces[1] >= 0 ? b.faces[1] : st;
        int tt = b.hasFaces && b.faces[2] >= 0 ? b.faces[2] : b.end >= 0 ? b.end : top;
        int bt = b.hasFaces && b.faces[3] >= 0 ? b.faces[3] : b.end >= 0 ? b.end : bot;
        int pt = b.hasFaces && b.faces[4] >= 0 ? b.faces[4] : st;
        int zt = b.hasFaces && b.faces[5] >= 0 ? b.faces[5] : st;
        CandleSpot s;
        s.x0 = b.b[0]; s.y0 = b.b[1]; s.z0 = b.b[2]; s.x1 = b.b[3]; s.y1 = b.b[4]; s.z1 = b.b[5];
        s.sink = sink;
        s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
        s.tile = (u16)std::max(st, 0);
        s.nx = (u16)std::max(nt, 0);
        s.top = (u16)std::max(tt, 0);
        s.bot = (u16)std::max(bt, 0);
        s.pz = (u16)std::max(pt, 0);
        s.nz = (u16)std::max(zt, 0);
        s.only = b.only;
        s.flame = b.glow ? (i16)(-10 - b.glow) : (i16)-1;   // a lit torch glows; -2 is reserved for scaffolding
        c.out.candles.push_back(s);
      }
      c.tint = 0xFFFFFF; c.tintKind = 0;
      return;
    }
  }
  u8 mat = Mat::Cutout | (R.tTrans[id] ? Mat::Translucent : 0);
  if (isPuddle(R, id)) mat = Mat::Liquid | Mat::Translucent | Mat::NoRelief;   // water: flat, glossy, sky-tinted highlight
  for (auto& b0 : boxes) {
    ShapeBox b = b0;
    b.b[0] += sox; b.b[3] += sox; b.b[2] += soz; b.b[5] += soz;
    int st = b.side >= 0 ? b.side : side;
    int tt = b.end >= 0 ? b.end : top, bt = b.end >= 0 ? b.end : bot;
    Own o = b.glow ? Own{own.sky, std::max<float>(own.bl, b.glow), (float)b.glow} : own;
    emitBox(c, d, id, meta, x, y, z, b, st, tt, bt, sink, kind, o, mat);
    if (T.candle && (meta & 4)) {
      int ft = R.tile("candle_flame");
      if (ft >= 0) emitFlame(c, d, ft, x + (b.b[0] + b.b[3]) / 32, y + (b.b[4] + sink) / 16, z + (b.b[2] + b.b[5]) / 32, 0.11f, 0.2f, own.sky, 15);
    }
  }
  c.tint = 0xFFFFFF; c.tintKind = 0;
  if (kind == ShapeKind::Lever) emitLeverHandle(c, d, x, y, z, meta, own);
}

// Near the player a chiseled block's remaining corners are coloured voxels, the same octants and
// the same faces the chunk boxes use. Grass, leaves and see-through blocks stay on the chunk picture.
// Far chisels stay there too. Snow on a column keeps that top face.
bool emitNearCarve(MeshCtx& c, BlockId id, int x, int y, int z, u8 carveMask) {
  const Registry& R = c.R;
  const BlockTraits& T = traits(id);
  if (T.tint || T.leaves || R.tTrans[id]) return false;
  c.out.hasTorch = true;
  if (!c.opt.torchVoxels) return false;
  const BlockDef& self = R.block(id);
  int side = self.faces[0], top = self.faces[2], bot = self.faces[3];
  MeshShapeNb nb(c, x, y, z);
  thread_local std::vector<ShapeBox> boxes;
  if (!carveBoxes(boxes, carveMask, nb)) return false;
  if (boxes.empty()) return true;
  const u8 coat = c.coatAt(x, y, z);
  const u16 sideT = (u16)std::max(side, 0);
  const u16 topT = (u16)std::max(top >= 0 ? top : side, 0);
  const u16 botT = (u16)std::max(bot >= 0 ? bot : side, 0);
  for (const ShapeBox& b : boxes) {
    int hx = b.b[0] >= 8.f ? 1 : 0, hy = b.b[1] >= 8.f ? 1 : 0, hz = b.b[2] >= 8.f ? 1 : 0;
    u8 only = b.only;
    if ((coat & 0x0F) && (coat & colBit(hx, hz)) && columnTopY(carveMask, hx, hz) == hy) {
      if (!(hy == 1 && c.opaqueSolid(x, y + 1, z))) only = (u8)(only & ~4u);
    }
    if (!only) continue;
    CandleSpot s;
    s.x0 = b.b[0]; s.y0 = b.b[1]; s.z0 = b.b[2]; s.x1 = b.b[3]; s.y1 = b.b[4]; s.z1 = b.b[5];
    s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
    s.tile = sideT; s.nx = sideT; s.pz = sideT; s.nz = sideT;
    s.top = topT; s.bot = botT;
    s.only = only;
    c.out.candles.push_back(s);
  }
  return true;
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

// Thin snow on each column that still has a micro-block. It sits on that column's top and grows upward.
void emitSnowCaps(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 carve) {
  u8 coat = c.coatAt(x, y, z);
  if (!(coat & 0x0F) || !carve) return;
  int tile = c.R.tile("snow");
  if (tile < 0) tile = 0;
  float h = (coat & 0x10) ? 4.f : 2.f;
  Own own{(float)c.skyAt(x, y, z), (float)c.blAt(x, y, z), 0};
  u8 mat = Mat::Cutout;
  u8 faces = (u8)(1 | 2 | 4 | 16 | 32);   // every side but the bottom, which would sit inside the stone
  for (int hx = 0; hx < 2; hx++) for (int hz = 0; hz < 2; hz++) {
    if (!(coat & colBit(hx, hz))) continue;
    int top = columnTopY(carve, hx, hz);
    if (top < 0) continue;
    if (top == 1 && c.opaqueSolid(x, y + 1, z)) continue;
    float x0 = hx ? 8.f : 0.f, z0 = hz ? 8.f : 0.f, y0 = top ? 16.f : 8.f;
    ShapeBox b(x0, y0, z0, x0 + 8.f, y0 + h, z0 + 8.f);
    b.only = faces;
    emitBox(c, d, id, 0, x, y, z, b, tile, tile, tile, 0, 0, own, mat, faces);
  }
}

void emitModel(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta) {
  const Registry& R = c.R;
  const BlockTraits& T = traits(id);
  u8 mat = Mat::Cutout;
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), R.tLight[id]);
  MeshShapeNb nb(c, x, y, z);
  SeatShift sh = seatShiftOf(id, meta, c.carveAt(x, y, z), nb);
  float lift = sh.micro ? sh.y : (float)sinkOffset(id, nb);
  int rf = T.facingModel ? meta & 3 : 0;
  vec3 o(x + (sh.micro ? sh.x / 16.f : 0.f), y, z + (sh.micro ? sh.z / 16.f : 0.f));
  bool stretchTop = sh.micro && sh.hang && sh.hy == 1;
  if (!stretchTop && R.block(id).str("shape") == "chain" && fullSolid(c.at(x, y + 1, z)) && carveFaceDepth(c.carveAt(x, y + 1, z), 0, -1, 0) == 8) stretchTop = true;
  if (T.cloth) {
    // flags, banners, bunting: each vertex learns how far it is from what holds it, and the shader flaps it
    thread_local std::vector<ShapeBox> cb; clothBoxes(id, meta, cb);
    // a flag reaches over to its pole: a fence post or wall post stands back from the cell's edge
    float reach = 0;
    if (T.cloth == 1) {
      static const int PX[4] = {0, -1, 0, 1}, PZ[4] = {1, 0, -1, 0};
      int ps = R.tShape[c.at(x + PX[rf], y, z + PZ[rf])];
      reach = ps == ShapeKind::Fence ? 6.f : ps == ShapeKind::Wall ? 4.f : 0.f;
    }
    for (auto& box0 : cb) {
      ShapeBox box = box0;
      if (reach > 0 && box.kind == 1) { box.b[2] += reach; box.b[5] += reach; }
      rotBox(box, rf);
      u8 k = box.kind;
      std::function<u8(vec3)> fn = [&](vec3 p) -> u8 {
        float vx = p.x - x, vy = p.y - y, vz = p.z - z;
        if (k == 0) return 0;
        if (k == 1) { float dd = (rf == 0 ? 1 - vz : rf == 1 ? vx : rf == 2 ? vz : 1 - vx) + reach / 16; return (u8)(((rf & 1) ? 48 : 64) + std::lround(clampv(dd, 0.f, 1.f) * 15)); }
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
  // Pots, chairs, anvils and the other still props. A flag flaps and a hanging lantern swings,
  // so those stay on the chunk mesh. A chain block's two link strips are coloured voxels nearby.
  // A grindstone's rim, a saw's shift and a campfire's end ring and grain are baked with the prop.
  // A lit campfire's flame still draws above the logs. A cauldron's iron is coloured voxels, and the water in it still draws.
  auto endsWith = [](const std::string& s, const char* suf) {
    size_t n = std::char_traits<char>::length(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
  };
  const std::string shapeName = R.block(id).str("shape");
  bool nearProp = endsWith(shapeName, "_chair") || endsWith(shapeName, "_stool") || endsWith(shapeName, "_bench")
      || endsWith(shapeName, "_table") || endsWith(shapeName, "_shelf") || endsWith(shapeName, "_bookcase")
      || endsWith(shapeName, "_cabinet") || shapeName.rfind("dragon_egg_", 0) == 0
      || shapeName == "flower_pot" || shapeName == "decorated_pot" || shapeName == "bell" || shapeName == "lectern"
      || shapeName == "anvil" || shapeName == "chipped_anvil" || shapeName == "damaged_anvil"
      || shapeName == "brewing_stand" || shapeName == "glass_jar" || shapeName == "weapon_rack"
      || shapeName == "wall_weapon_rack" || shapeName == "brazier" || shapeName == "loose_stones"
      || shapeName == "sleeping_bag" || shapeName == "sieve" || shapeName == "siphon"
      || shapeName == "grapple_anchor" || shapeName == "end_portal_frame" || shapeName == "end_portal_frame_eye"
      || shapeName == "lantern" || shapeName == "grindstone" || shapeName == "stonecutter"
      || shapeName == "grave" || shapeName == "sculk_sensor" || shapeName == "sculk_shrieker"
      || shapeName == "spike_trap" || shapeName == "campfire" || shapeName == "soul_campfire"
      || shapeName == "rope" || shapeName == "cauldron";
  bool plain = nearProp;
  static int chainTile = -2;
  if (chainTile == -2) chainTile = R.tile("chain");
  // The chain block only. A lantern's own links stay in the box loop below, where they can still sway.
  if (shapeName == "chain") {
    c.out.hasTorch = true;
    if (c.opt.torchVoxels && chainTile >= 0) {
      CandleSpot s;
      s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
      s.tile = (u16)chainTile;
      s.sink = lift;
      s.x0 = sh.micro ? sh.x / 16.f : 0.f;
      s.z0 = sh.micro ? sh.z / 16.f : 0.f;
      s.only = stretchTop ? 1 : 0;
      s.flame = -7;
      c.out.candles.push_back(s);
      return;
    }
  }
  if (plain) for (auto& b : *boxes) {
    if (b.tu != 0.f || b.uvT || b.only != 63) plain = false;
    if (chainTile >= 0 && (b.side == chainTile || b.end == chainTile)) plain = false;
  }
  if (plain) c.out.hasTorch = true;
  if (plain && c.opt.torchVoxels) {
    ModelSpot s;
    s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
    s.facing = (u8)(rf & 3);
    s.sink = lift;
    s.ox = sh.micro ? sh.x / 16.f : 0.f;
    s.oz = sh.micro ? sh.z / 16.f : 0.f;
    s.model = (u16)std::max(mi, 0);
    c.out.models.push_back(s);
  } else {
  // a hanging lantern swings gently: the further below the hook, the more it moves
  std::function<u8(vec3)> hangFn = [&](vec3 p) -> u8 { return (u8)(Sway::Hang + std::lround(clampv(y + 1 - p.y, 0.f, 1.f) * 11)); };
  const int extra = hang ? (meta >> 2) & 31 : 0;   // a chain up to a ceiling further up (half blocks beyond its own block)
  static int chainTile = -2; if (chainTile == -2) chainTile = R.tile("chain");
  for (auto& b0 : *boxes) {
    if (!b0.only) continue;   // a box that collides and picks but a render layer draws (a placed mob head: its stone box
                              // fought the head's own faces, the flicker round placed skulls)
    ShapeBox b = b0; if (rf) rotBox(b, rf);
    bool topLink = chainTile >= 0 && b.side == chainTile && b.b[4] >= 15.5f;
    if (stretchTop && topLink && !extra) b.b[4] += 8.f;   // the top link reaches an upper underside
    addShapeBox(c, d, b, o, 1 / 16.f, sk, bl, lift, rf, mat, hang ? &hangFn : nullptr);
    if (extra && chainTile >= 0 && b.side == chainTile)
      for (float a = 16; a < 16 + extra * 8; a += 16) {   // the rest of it, a block at a time
        ShapeBox s = b; s.b[1] = a; s.b[4] = std::min(16.f + extra * 8, a + 16);
        if (stretchTop && s.b[4] >= 16.f + extra * 8 - 0.01f) s.b[4] += 8.f;
        addShapeBox(c, d, s, o, 1 / 16.f, sk, bl, lift, rf, mat, nullptr);
      }
  }
  }
  if (T.campfire && !out) {
    int ft = R.tile(T.campfire == 1 ? "campfire_fire" : "soul_campfire_fire");
    // Near the player the flame is four coloured walls, and the frame still changes. Far away it stays a chunk picture.
    if (ft >= 0 && plain && c.opt.torchVoxels) {
      CandleSpot s;
      s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
      s.tile = (u16)ft;
      s.sink = lift;
      s.x0 = sh.micro ? sh.x / 16.f : 0.f;
      s.z0 = sh.micro ? sh.z / 16.f : 0.f;
      s.flame = -5;
      c.out.candles.push_back(s);
    } else if (ft >= 0 && !emitVoxelPlates(c, d, ft, o.x, y + lift / 16, o.z, 0.08f, 1, 15, 15, true))
      emitPlanes(c, d, ft, o.x, y + lift / 16, o.z, 0.08f, 1, 15, 15, false);
  }
  if (T.cauldron && (meta & 3)) {
    // water in a cauldron
    { c.tint = c.cornerTint((float)x + 1, (float)z + 1, 2); c.tintKind = (u8)(1 + (2)); } c.extraMat = Mat::TintAll;
    ShapeBox wb(2, 4, 2, 14, 4 + (meta & 3) * 3.6f, 14); wb.side = wb.end = R.tile("water");
    addShapeBox(c, d, wb, o, 1 / 16.f, sk, bl, 0, 0, Mat::NoRelief);
    c.tint = 0xFFFFFF; c.tintKind = 0; c.extraMat = 0;
  }
  if (T.pot) {
    int idx = MeshInput::idx(x, y, z);
    for (auto& [i, plant] : c.in.potPlants) if (i == idx) {
      int pt = R.block(plant).faces[0];
      float py = y + (lift + 4) / 16;
      // A flower near you is coloured voxels in the pot. Grass and ferns stay on the chunk picture so the climate still tints them.
      const BlockTraits& PT = traits(plant);
      if (plain && c.opt.torchVoxels && pt >= 0 && !PT.tint) {
        CandleSpot s;
        s.cx = (u8)x; s.cy = (u8)y; s.cz = (u8)z;
        s.tile = (u16)pt;
        s.sink = lift + 4.f;
        s.flame = -3;
        c.out.candles.push_back(s);
      } else if (!emitVoxelPlates(c, d, pt, o.x, py, o.z, 0.25f, 0.75f, sk, bl, false))
        emitPlanes(c, d, pt, o.x, py, o.z, 0.25f, 0.75f, sk, bl, true);
    }
  }
}

} // namespace dq
