// world/shapes.h — blocks that aren't full cubes (port of the original world/shapes.js plus the
// SHAPES model table of world/mesher.js and game/*.js).
//
// One function describes every non-cube block as a list of boxes in 1/16-block "pixels", and the
// same boxes drive drawing (render/mesher), collision (sim/physics) and the selection outline, so
// what you see is what you bump into. Shapes that join their neighbours (fences, walls, panes,
// stairs' corners, chorus stems, redstone dust, tripwire) look at them through a ShapeNb.
//
// Meta bits are exactly the original's (see the table at the top of shapes.cpp).
// No GL here: this is dqcore, the dedicated server uses it too.
#pragma once
#include "world/world.h"

namespace dq {

// What each block does in a redstone circuit (the original RS table, core/blocks.js)
enum class RsRole : u8 { None = 0, Wire, Torch, TorchOff, Block, Lever, Button, Plate, Repeater, Comparator, Observer, Lamp, Piston,
  Dispenser, Dropper, Hopper, Note, Tnt, Door, Trapdoor, Gate, Daylight, Target, PRail, DRail, Bell, Bulb, Hook };

// Pre-parsed per-block facts the mesher and the shapes need in hot loops (built from BlockDef.props).
struct BlockTraits {
  RsRole rs = RsRole::None;
  bool cond = false;            // a full solid block that redstone can power (T_COND)
  u8 rail = 0;                  // 0 not a rail, 1 normal (10 shapes), 2 powered/detector/activator (6 shapes)
  u8 tint = 0;                  // climate tint: 0 none, 1 grass, 2 foliage
  u8 cloth = 0;                 // 1 flag, 2 banner, 3 bunting, 4 washing line, 5 shop sign
  u8 campfire = 0;              // 1 campfire, 2 soul campfire
  bool pickle = false, chorus = false, chorusFlower = false, tripwire = false, door = false, candle = false, cauldron = false, pot = false;
  bool endRod = false, facingModel = false, lantern = false, bamboo = false, pile = false, backpack = false, hive = false;
  bool axis = false, hasFront = false, f6 = false, sideAll = false, selfCull = false, leaves = false;
  bool crop = false, dbl = false, altTile = false, hash = false, fire = false, flower = false, hangs = false, stackPlant = false;
  bool emitsParticles = false;  // fire, torches, portals, candles... (for the particles engine)
  int model = -1, modelHang = -1, modelOut = -1;   // index into modelShapes() (def.shape, its "_hang" and campfire "_out" variants)
  int upperTile = -1;           // doors (upper half), double plants (upper), alt tile
  int bedTop = -1;              // the bed's top tile (for the folded blanket edge)
  int clothTex = -1;
  int f6t[6] = {-1, -1, -1, -1, -1, -1};   // frontV, front, back, backOn, top, side
  std::vector<u16> stageTiles;  // crops
};
void initBlockTraits();                 // after the registry is final; World's constructor calls it (cheap if done)
const BlockTraits& traits(BlockId id);

struct ShapeBox {
  float b[6] = {0, 0, 0, 16, 16, 16};   // x0 y0 z0 x1 y1 z1 in pixels
  int side = -1, end = -1, xt = -1;     // tile layers (-1 = the block's own side / top+bottom tiles)
  int faces[6] = {-1, -1, -1, -1, -1, -1};   // per-face tiles (+x -x +y -y +z -z) when hasFaces
  bool hasFaces = false;
  bool nocol = false;                   // drawn but nothing to bump into
  u8 only = 63;                         // faces drawn (bit per face)
  u8 kind = 0;                          // cloth sway: 0 none, 1 len, 2 depth, 3 pen, 4 rope, 5 hang
  u8 glow = 0;                          // light the part gives itself (lit torches on repeaters)
  float tv = 0, lift = 0;               // model: shift down the picture; cloth: lift (pixels)
  ShapeBox() = default;
  ShapeBox(float x0, float y0, float z0, float x1, float y1, float z1) { b[0] = x0; b[1] = y0; b[2] = z0; b[3] = x1; b[4] = y1; b[5] = z1; }
};

// Neighbour access relative to the block being shaped.
struct ShapeNb {
  virtual ~ShapeNb() = default;
  virtual BlockId id(int dx, int dy, int dz) const = 0;
  virtual u8 meta(int dx, int dy, int dz) const = 0;
};
struct WorldShapeNb : ShapeNb {
  const World& w; int x, y, z;
  WorldShapeNb(const World& w, int x, int y, int z) : w(w), x(x), y(y), z(z) {}
  BlockId id(int dx, int dy, int dz) const override { return w.getBlock(x + dx, y + dy, z + dz); }
  u8 meta(int dx, int dy, int dz) const override { return w.getMeta(x + dx, y + dy, z + dz); }
};

namespace ShapePurpose { enum : int { Draw = 0, Collide = 1, Select = 2 }; }

// The boxes of block `id` with `meta` (pixels). Returns false for a plain full cube ("null" in the
// original); true with `out` possibly empty for "nothing" (an open gate, dust's collision).
bool shapeBoxes(BlockId id, u8 meta, const ShapeNb& nb, int purpose, std::vector<ShapeBox>& out);
int sinkOffset(BlockId id, const ShapeNb& nb);          // pixels: -8 when it sits on a bottom half slab
float torchSetBack(u8 meta, const ShapeNb& nb);          // 0.5 when a wall torch leans on a vertical slab's open face
bool slabGap(BlockId id, u8 meta, int side);

// World-space boxes of the block at (x,y,z), sink and torch set-back applied. false = full cube.
// This is what collision (purpose Collide), picking and the outline (Select) should use.
struct BoxF { vec3 lo, hi; };
bool blockShapeBoxes(const World& w, int x, int y, int z, int purpose, std::vector<BoxF>& out);

// ---- helpers shared with the mesher and gameplay ----
extern const int DIRX[4], DIRZ[4];                     // N E S W
extern const int D6X[6], D6Y[6], D6Z[6], OPP6[6], H2D6[4], D62H[6], D6_FACE[6], FACE_D6[6];
constexpr u8 WLOG = 0x80;
inline bool isWaterlogged(BlockId id, u8 meta);
void rotBox(ShapeBox& b, int f);                       // north-facing box turned to face f (0 N, 1 E, 2 S, 3 W)
void flipBox(ShapeBox& b);                             // upside down
ShapeBox attachBox(const ShapeBox& b, u8 meta);        // levers/buttons: floor/ceiling/wall
vec3 attachPoint(vec3 p, u8 meta, bool vec);
ShapeBox box6(const ShapeBox& b, int dir);             // modelled facing up, turned to a six-way facing
enum StairShape { STAIR_STRAIGHT = 0, STAIR_OUTER_L, STAIR_OUTER_R, STAIR_INNER_L, STAIR_INNER_R };
int stairShape(u8 meta, const ShapeNb& nb);
void wireConnects(const ShapeNb& nb, int out[4]);      // per side N/E/S/W: 0 none, 1 runs that way, 2 climbs the next block
int railShape(BlockId id, u8 meta);                    // 0..9 (see RAIL_EXITS)
extern const int RAIL_EXITS[10][2][3];
bool fullSolid(BlockId id);
bool joinsFence(BlockId id), joinsWall(BlockId id), joinsPane(BlockId id);
bool hangingLantern(BlockId id, u8 meta);

// static block models (data/shapes.json): name -> boxes
const std::vector<ShapeBox>* modelShape(int index);
int modelShapeIndex(const std::string& name);
void clothBoxes(BlockId id, u8 meta, std::vector<ShapeBox>& out);   // flags, banners, bunting, washing lines, shop signs (game/townlife.js)

inline bool isWaterlogged(BlockId id, u8 meta) { return reg().tWlog[id] && (meta & WLOG); }

} // namespace dq
