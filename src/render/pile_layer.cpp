// render/pile_layer.cpp — things set down in the world (game/entities.js blockEntityDraws): item piles
// (sneak + use with an item: blocks stack up, ingots lie in criss-crossed layers, tools lie flat, bottles stand,
// the rest in a little heap) and placed backpacks. The blocks themselves are invisible (render NONE); their
// block entities carry the stacks. Item frames show their item the same way.
#include "sim/piles.h"
#include "sim/worktable.h"
#include "sim/anvil_work.h"
#include "sim/ench_work.h"
#include "sim/crafting.h"
#include "core/tool_parts.h"
#include "sim/weapon_rack.h"
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "render/map_render.h"
#include "client/app.h"
#include "assets/voxel_paint.h"
#include "world/shapes.h"
#include "core/item_glint.h"
#include "render/hands_kit.h"
#include "assets/local_models.h"
#include "sim/hands.h"
#include "sim/brewing.h"
#include "client/app.h"
#include <regex>
#include <map>

namespace dq {

// where the 3 x 3 is drawn on the crafting table's top, measured from the picture in use (a resource pack's may put a
// small grid in the middle of a cloth: the squares didn't line up with it): the four evenly spaced lines each way that
// stand out most from the pixels beside them. Nothing clear: the whole top in thirds.
static vec4 findTableGrid(const TileSet& ts, int t) {
  const int S = ts.size;
  if (t < 0 || t >= ts.count || S < 8) return vec4(0, 0, 1, 1);
  const u8* px = ts.tile(t);
  auto lum = [&](int x, int y) { const u8* c = px + ((size_t)y * S + x) * 4; return c[3] < 128 ? 0.f : 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; };
  auto lines = [&](bool across) {   // across: vertical lines (x varies); else horizontal
    std::vector<float> score(S, 0);
    for (int a = 1; a < S - 1; a++) {
      float sum = 0; int n = 0;
      for (int b = S / 8; b < S - S / 8; b++) {
        const float c = across ? lum(a, b) : lum(b, a), l = across ? lum(a - 1, b) : lum(b, a - 1), r = across ? lum(a + 1, b) : lum(b, a + 1);
        sum += std::fabs(c - 0.5f * (l + r)); n++;
      }
      score[a] = n ? sum / n : 0;
    }
    // the four lines against the squares' insides between them: a drawn grid's lines stand out from what they bound
    float best = 0, bestLine = 0, bestIn = 0; int b0 = -1, bs = 0;
    for (int s = 3; s <= S / 3; s++) for (int x0 = 1; x0 + 3 * s <= S - 2; x0++) {
      const float line = (score[x0] + score[x0 + s] + score[x0 + 2 * s] + score[x0 + 3 * s]) / 4;
      const float in = (score[x0 + s / 2] + score[x0 + s + s / 2] + score[x0 + 2 * s + s / 2]) / 3;
      if (line - in > best) { best = line - in; bestLine = line; bestIn = in; b0 = x0; bs = s; }
    }
    if (b0 < 0 || bestLine < bestIn * 2.f + 4.f) return vec2(-1);   // no clear grid
    return vec2((b0 + 0.5f) / S, (b0 + 3 * bs + 0.5f) / S);
  };
  const vec2 gx = lines(true), gz = lines(false);
  if (gx.x < 0 || gz.x < 0 || gx.y - gx.x < 0.3f || gz.y - gz.x < 0.3f) return vec4(0, 0, 1, 1);
  return vec4(gx.x, gz.x, gx.y, gz.y);
}

namespace {

// a stack's model: a mixed tool, a cast head, a filled mold... by its makeup (core/tool_parts.h), else its item's
}  // namespace
std::string stackModelKey(const ItemStack& s) { const std::string k = partToolModelKey(s); return k.empty() ? s.id : k; }   // (render/chest_layer.cpp too)
namespace {

// furniture.js bookMesh: a small book standing on a bookcase shelf, 0.22 x 0.38 x 0.4 blocks; cover colour per index
// (0-3 the shop's colours, 4 purple for an enchanted book), gold bands across the spine (front, +z), page edges
// top, bottom and back. Origin at the middle of its foot.
const VoxelModel& shelfBook(int i) {
  static auto& cache = *new std::map<int, std::unique_ptr<VoxelModel>>();
  auto it = cache.find(i);
  if (it != cache.end()) return *it->second;
  static const u8 COL[5][3] = {{0x8a, 0x2a, 0x22}, {0x24, 0x45, 0x6e}, {0x2e, 0x5a, 0x2a}, {0x6a, 0x44, 0x22}, {0x5a, 0x2a, 0x6a}};
  const int W = 7, H = 12, D = 13;
  VoxelGrid g(ivec3(W, H, D));
  auto c = [&](int k, float sh) { return voxShade(voxRGBA(COL[k][0], COL[k][1], COL[k][2]), sh); };
  u32 pg = voxRGBA(232, 224, 200), pg2 = voxRGBA(208, 200, 176), gold = voxRGBA(216, 176, 64);
  for (int y = 0; y < H; y++) for (int z = 0; z < D; z++) for (int x = 0; x < W; x++) {
    bool cover = x == 0 || x == W - 1 || z == D - 1;   // the two covers and the spine
    u32 col;
    if (z == D - 1) {   // the spine: its colour, two gold bands, a few lighter dots
      col = c(i % 5, (y == 0 || y == H - 1) ? 0.6f : 1.f);
      if (y == 3 || y == H - 4) col = gold;
      else if (y > 4 && y < H - 5 && x > 1 && x < W - 2 && (x + y) % 3 == 0) col = c(i % 5, 1.25f);
    } else if (cover) col = c((i + 1) % 5, (x + y + z) & 1 ? 1.f : 0.92f);
    else col = (z + y) & 1 ? pg : pg2;
    g.set(x, y, z, col);
  }
  auto m = std::make_unique<VoxelModel>();
  VoxelMeshData md; buildVoxelMesh(g, md);
  m->gl.upload(md);
  m->frame.voxel = 1.f / 32; m->frame.origin = vec3(-W * 0.5f, 0, -D * 0.5f) / 32.f;
  m->size = g.size; m->kind = VoxelModel::Flat; m->built = true; m->key = "shelf_book";
  return *(cache[i] = std::move(m));
}

// a piled ingot: a bar 16 x 4 x 8 voxels (half a block long, an eighth high, a quarter wide), its top narrower like a
// cast bar, in the colour of the ingot's own picture (the item model is carved from a diagonal sprite: it doesn't stack)
const VoxelModel& ingotBar(const ItemStack& st) {
  static auto& cache = *new std::map<std::string, std::unique_ptr<VoxelModel>>();
  const TileSet& ts = app().tiles;
  const std::string key = st.id + "#" + ts.packName + std::to_string(ts.count);
  auto it = cache.find(key);
  if (it != cache.end()) return *it->second;
  u32 avg = voxRGBA(170, 172, 178);
  if (const ItemDef* d = st.def()) { auto ti = ts.index.find(d->icon); if (ti != ts.index.end() && ti->second >= 0) avg = ts.avgColor(ti->second); }
  const vec3 c(avg & 255, (avg >> 8) & 255, (avg >> 16) & 255);
  const float lum = std::max(1.f, 0.3f * c.r + 0.59f * c.g + 0.11f * c.b), k = std::clamp(150.f / lum, 0.9f, 1.6f);   // (the sprite's dark outline brightened out)
  const vec3 m = glm::min(c * k, vec3(255));
  const int L = 16, H = 4, W = 8;
  VoxelGrid g(ivec3(L, H, W));
  for (int y = 0; y < H; y++) {
    const int inset = y >= H - 1 ? 1 : 0;   // (the top a voxel in all round: a cast bar's taper)
    for (int z = inset; z < W - inset; z++) for (int x = inset; x < L - inset; x++) {
      const bool top = y == H - 1, edge = x == inset || x == L - 1 - inset || z == inset || z == W - 1 - inset;
      const float sh = top ? (edge ? 1.05f : 1.15f) : (y == 0 ? 0.7f : 0.88f);
      const float n = ((x * 7 + z * 13 + y * 5) % 5) * 0.02f;   // (a little hammered texture)
      g.set(x, y, z, voxRGBA((int)(m.r * (sh - n)), (int)(m.g * (sh - n)), (int)(m.b * (sh - n))));
    }
  }
  auto mdl = std::make_unique<VoxelModel>();
  VoxelMeshData md; buildVoxelMesh(g, md);
  mdl->gl.upload(md);
  mdl->frame.voxel = 1.f / 32; mdl->frame.origin = vec3(-L * 0.5f, 0, -W * 0.5f) / 32.f;
  mdl->size = g.size; mdl->kind = VoxelModel::Flat; mdl->built = true; mdl->key = "ingot_bar";
  return *(cache[key] = std::move(mdl));
}

class PileLayer : public RenderLayer {
  struct FramedMap { const PaperMap* map; mat4 M; vec2 light; };
  std::vector<FramedMap> framedMaps;   // drawn after the voxels, with the content shader
  struct StillWater { ivec3 at; int facing; vec2 light; };
  std::vector<StillWater> handsWater;   // the hands-on still brewing: water in its glass
  struct StillBook { ivec3 at; int facing, page; vec2 light; };
  std::vector<StillBook> handsBooks;   // the still's recipe books: their parts and writing
  struct TableLapis { ivec3 at; int facing; float fill; vec2 light; };
  std::vector<TableLapis> handsLapis;   // the enchanting table's lapis in its tray (filled to the stack)
public:
  const char* name() const override { return "item-piles"; }
  int order() const override { return 110; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    auto begin = [&] { if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; } };
    forBlockEntities(w, f, 48.f, [&](const Chunk&, json& e, int x, int y, int z) {
      const std::string type = e.value("type", "");
      if (type != "pile" && type != "backpack" && type != "frame" && type != "shelf" && type != "bookcase" && type != "rack" && type != "ingots" && type != "logs" && type != "kiln" && type != "worktable" && type != "anvilwork" && type != "enchwork" && type != "brew") return;
      vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
      if (glm::dot(rel - vec3(0.5f), rel - vec3(0.5f)) > 48.f * 48.f || !f.cam.frustum.sphere(rel + vec3(0.5f), 1.2f)) return;
      // the light where the things are: a crafting table's grid lies on top of a solid block (which holds no light inside:
      // the pieces drew black), so there it's the light just above it
      const BlockId here = w.getBlock(x, y, z);
      u8 l = here && reg().tOpaque[here] ? w.getLight(x, y + 1, z) : w.getLight(x, y, z);
      VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      // resting on a bottom half slab: sit half a block lower
      float yOff = 0;
      {
        BlockId bl = w.getBlock(x, y - 1, z);
        if (bl && reg().tShape[bl] == ShapeKind::Slab && !(w.getMeta(x, y - 1, z) & 7)) yOff = -0.5f;
      }
      mat4 base = glm::translate(mat4(1), rel + vec3(0, yOff, 0));
      if (type == "brew" && stationModel(w.getBlock(x, y, z)) && s.app.settings.handsStations) {
        // the hands-on still: the ingredient in its dome, the powder in its burner, a few
        // of each tossed where the block's own seed throws them (the bottles on its rack: render/magic_fx.cpp)
        begin();
        const ivec3 at(x, y, z);
        const TPose base = handsBlockPose(f, at, handsFacing(w, at));
        const VoxelLight TL = handsLight(w, at);
        const json& items = e.contains("items") && e["items"].is_array() ? e["items"] : json::array();
        auto pile = [&](int k, float cx, float cy, float cz) {
          const ItemStack st = k < (int)items.size() ? ItemStack::fromJson(items[k]) : ItemStack();
          if (st.empty()) return;
          u32 h = (u32)(x * 73856093) ^ (u32)(y * 19349663) ^ (u32)(z * 83492791) ^ (u32)k;
          auto rnd = [&] { h = h * 1664525u + 1013904223u; return (h >> 8 & 0xFFFF) / 65535.f; };
          ItemStack one = st; one.n = 1;
          for (int i = 0; i < std::min(st.n, 4); i++) {
            const float a = rnd() * 6.2832f, r = std::sqrt(rnd()), oy = rnd() * 0.6f, spin = rnd() * 360, tx = (rnd() - 0.5f) * 25, tz = (rnd() - 0.5f) * 25;
            TPose p = base; p.t((cx + std::cos(a) * r) / 16, (cy + oy) / 16, (cz + std::sin(a) * r) / 16).ry(spin).rx(90 + tx).rz(tz).s(0.16f);
            handsItem(one, p.m, TL);
          }
        };
        pile(3, 11.8f, 4.2f, 10.9f);
        pile(4, 11.5f, 0.8f, 10.f);
        if (e.value("t", 0.0) > 0) handsWater.push_back({at, handsFacing(w, at), vec2(TL.sky, TL.block)});
        // its recipe book in front: ten of our brews a spread, five a page, each
        // its ingredient's icon and what it makes from what; the page's parts and writing drawn after the voxels
        const int page = e.value("bookPage", 0);
        const auto& R = brewRecipes();
        for (int i = 0; i < 10; i++) {
          const int k = page * 10 + i;
          if (k >= (int)R.size()) break;
          const bool left = i < 5; const int r = i % 5;
          const float rh = 0.96f, z1 = 5.6f - r * rh, x0 = left ? 8.3f : 3.3f;
          TPose p = base;
          p.t(0.5f, 0.0875f, 0.21875f).rz(left ? 4.f : -4.f).t(-0.5f, -0.0875f, -0.21875f);   // (each half tilted on the spine)
          p.t((x0 + rh * 0.5f) / 16, 0.096875f, (z1 - rh * 0.5f) / 16).ry(180).rx(-90).s(rh * 0.85f / 16);
          handsItem(ItemStack(R[k].ing, 1), p.m, TL);
        }
        handsBooks.push_back({at, handsFacing(w, at), page, vec2(TL.sky, TL.block)});
        return;
      }
      if (type == "brew") {   // the brewing stand: bottles on its three feet, the ingredient in the bowl, the powder at its foot
        begin();
        // (the brewing scene's FEET {12, 8} {4, 4} {4, 12} px, the bottles' feet on the stone 2 px up)
        static const vec3 SPOT[5] = {{12.5f / 16, 2 / 16.f, 8 / 16.f}, {3.5f / 16, 2 / 16.f, 4 / 16.f}, {3.5f / 16, 2 / 16.f, 12 / 16.f}, {0.5f, 14.5f / 16.f, 0.5f}, {11 / 16.f, 1 / 16.f, 13 / 16.f}};
        const json& items = e.contains("items") && e["items"].is_array() ? e["items"] : json::array();
        for (int k = 0; k < 5 && k < (int)items.size(); k++) {
          const ItemStack st = ItemStack::fromJson(items[k]);
          if (st.empty()) continue;
          const VoxelModel& im = itemModel(stackModelKey(st));
          if (!im.ready()) continue;
          if (k < 3) continue;   // (the bottles on the feet are drawn by render/magic_fx.cpp with the stand's bubbles: once only)
          mat4 m = glm::translate(base, SPOT[k]);
          if (k < 3) { m = glm::scale(glm::translate(m, vec3(0, im.stand ? 0.14f : 0.1f, 0)), vec3(0.3f)); }   // a bottle standing
          else if (im.kind == VoxelModel::Block) m = glm::scale(glm::translate(m, vec3(0, 0.05f, 0)), vec3(0.12f));
          else { m = glm::rotate(m, k == 3 ? 0.4f : 1.1f, vec3(0, 1, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(k == 3 ? 0.2f : 0.18f)); }
          VR.draw(im.gl, m * im.matrix(), L);
        }
        return;
      }
      if (type == "enchwork" && stationModel(w.getBlock(x, y, z)) && s.app.settings.handsStations) {
        // the hands-on table: the item over its socket, bobbing and turning; the lapis in its
        // reservoir at the back, filling it as there's more
        begin();
        const ivec3 at(x, y, z);
        const std::vector<ItemStack> v = enchWorkItems(w, at);
        const TPose base = handsBlockPose(f, at, handsFacing(w, at));
        const VoxelLight TL = handsLight(w, at);
        const float age = (float)f.time * 20.f;
        if (!v[0].empty()) {
          TPose p = base; p.t(0.5f, (16.5f + std::sin(age * 0.1f) * 0.6f) / 16, 0.375f).ry(age * 2.f).s(0.55f);
          ItemStack one = v[0]; one.n = 1;
          handsItem(one, p.m, VoxelLight{TL.sky, std::max(TL.block, 0.7f)});
        }
        if (!v[1].empty()) handsLapis.push_back({at, handsFacing(w, at), std::clamp(v[1].n / 64.f, 0.08f, 1.f), vec2(TL.sky, TL.block)});   // (its tray, below)
        return;
      }
      if (type == "enchwork") {   // sim/ench_work.h: the item hovering over the enchanting table, turning; the lapis on its top
        begin();
        const std::vector<ItemStack> v = enchWorkItems(w, ivec3(x, y, z));
        const float t = (float)f.time;
        if (!v[0].empty()) if (const VoxelModel& im = itemModel(stackModelKey(v[0])); im.ready()) {
          mat4 m = glm::translate(base, vec3(0.5f, 0.75f + 0.42f + 0.03f * std::sin(t * 1.6f), 0.5f));   // (enchant scene ITEM_SIZE 0.34)
          m = glm::rotate(m, t * 0.8f, vec3(0, 1, 0));
          m = glm::scale(m, vec3(im.kind == VoxelModel::Block ? 0.24f : 0.34f));
          VR.draw(im.gl, m * im.matrix(), VoxelLight{L.sky, std::max(L.block, 0.7f)}, vec4(0), 1.f);   // (glinting, lit by the table)
        }
        if (!v[1].empty()) if (const VoxelModel& lm = itemModel(v[1].id); lm.ready()) for (int e = 0; e < std::min(v[1].n, 3); e++) {
          mat4 m = glm::translate(base, vec3(0.78f - e * 0.05f, 0.755f + e * 0.012f, 0.25f + e * 0.04f));
          m = glm::rotate(m, 0.6f + e, vec3(0, 1, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.22f));
          VR.draw(lm.gl, m * lm.matrix(), L);
        }
        return;
      }
      if (type == "anvilwork" && stationModel(w.getBlock(x, y, z)) && s.app.settings.handsStations) {
        // the hands-on anvil: the work flat on its face, the material over it sinking in blow by blow
        begin();
        const ivec3 at(x, y, z);
        const std::vector<ItemStack> v = anvilWorkItems(w, at);
        const TPose base = handsBlockPose(f, at, handsFacing(w, at));
        const VoxelLight TL = handsLight(w, at);
        const int req = std::max(1, e.value("req", 2)), hits = e.value("hits", 0);
        const float sink = std::min(1.f, (float)hits / req);
        const bool fusion = !v[0].empty() && !v[1].empty() && v[0].id == v[1].id;
        auto flat = [&](const ItemStack& st, float py, float sc) {
          if (st.empty()) return;
          ItemStack one = st; one.n = 1;
          TPose p = base; p.t(0.5f, py / 16, 0.5f).rx(90).s(sc);
          handsItem(one, p.m, TL);
        };
        flat(v[0], 16.1f, 0.84f);
        flat(v[1], 17.f + (16.1f - 17.f) * sink + 0.1f, fusion ? 0.84f : 0.42f);
        return;
      }
      if (type == "anvilwork") {   // sim/anvil_work.h: the work lying on the anvil's face, what goes into it beside it, askew
        begin();
        const std::vector<ItemStack> v = anvilWorkItems(w, ivec3(x, y, z));
        for (int k = 0; k < 2; k++) {
          if (v[k].empty()) continue;
          const VoxelModel& im = itemModel(stackModelKey(v[k]));
          if (!im.ready()) continue;
          // (the anvil scene's: MAIN_SIZE 0.4 turned 45 degrees along the anvil; ADDITION_SIZE 0.3, to the right)
          mat4 m = glm::translate(base, vec3(0.5f + (k ? 0.2f : -0.02f), 1.005f, 0.5f + (k ? 0.12f : 0.f)));
          m = glm::rotate(m, k ? 0.45f : PI / 4, vec3(0, 1, 0));
          if (im.kind == VoxelModel::Block) m = glm::scale(glm::translate(m, vec3(0, 0.07f, 0)), vec3(k ? 0.14f : 0.18f));
          else { m = glm::translate(m, vec3(0, (im.depth > 0 ? im.depth : 0.03f) * (k ? 0.3f : 0.4f) + 0.004f, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(k ? 0.3f : 0.4f)); }
          VR.draw(im.gl, m * im.matrix(), L, vec4(0), itemGlints(v[k]) ? 1.f : 0.f);
          if (k == 1 && v[1].n > 1) for (int e = 1; e < std::min(v[1].n, 4); e++) {   // a little heap of the material
            mat4 m2 = glm::translate(base, vec3(0.72f - e * 0.03f, 1.005f + e * 0.02f, 0.62f + e * 0.025f)); m2 = glm::rotate(m2, 0.45f + e * 0.7f, vec3(0, 1, 0));
            if (im.kind == VoxelModel::Block) m2 = glm::scale(glm::translate(m2, vec3(0, 0.07f, 0)), vec3(0.14f));
            else { m2 = glm::rotate(m2, -PI / 2, vec3(1, 0, 0)); m2 = glm::scale(m2, vec3(0.3f)); }
            VR.draw(im.gl, m2 * im.matrix(), L);
          }
        }
        return;
      }
      if (type == "worktable") {   // sim/worktable.h: the pieces on their squares of the table's top; what they make, over it
        {   // the drawn grid, once per picture (a resource pack changes it)
          static std::string seen;
          const std::string stamp = s.app.tiles.packName + "#" + std::to_string(s.app.tiles.count);
          if (stamp != seen) {
            seen = stamp;
            const BlockId tb = reg().blockId("crafting_table");
            const vec4 r = findTableGrid(s.app.tiles, tb ? (int)reg().block(tb).faces[2] : -1);
            setWorktableGrid(r);
            DQ_LOG("crafting table grid: %.3f %.3f .. %.3f %.3f", r.x, r.y, r.z, r.w);
          }
        }
        begin();
        const std::vector<ItemStack> g = worktableGrid(e);
        for (int k = 0; k < 9; k++) {
          if (g[k].empty()) continue;
          const VoxelModel& im = itemModel(stackModelKey(g[k]));
          if (!im.ready()) continue;
          const vec3 cc = worktableCellCenter(ivec3(x, y, z), k) - vec3(x, y, z);   // (on the drawn squares)
          const float cs = (worktableGridRect().z - worktableGridRect().x) / 3 * 3;   // the squares' size against thirds of the block
          mat4 m = glm::translate(base, cc);
          if (im.kind == VoxelModel::Block) m = glm::scale(glm::translate(m, vec3(0, 0.08f * cs, 0)), vec3(0.16f * cs));   // (a block a little under a square)
          else if (im.stand) m = glm::scale(glm::translate(m, vec3(0, 0.11f * cs, 0)), vec3(0.28f * cs));
          else { m = glm::translate(m, vec3(0, (im.depth > 0 ? im.depth : 0.03f) * 0.3f * cs + 0.004f, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.3f * cs)); }
          VR.draw(im.gl, m * im.matrix(), L, vec4(0), itemGlints(g[k]) ? 1.f : 0.f);
        }
        if (const CraftMatch r = worktableMatch(w, ivec3(x, y, z), s.player->yaw, w.immersive)) {   // (read from any side)
          const ItemStack out = r.result();
          const VoxelModel& om = itemModel(stackModelKey(out));
          if (om.ready()) {
            mat4 m = glm::translate(base, vec3(0.5f, 1.38f + 0.04f * std::sin((float)f.time * 2.f), 0.5f));
            m = glm::rotate(m, (float)f.time * 1.2f, vec3(0, 1, 0));
            m = glm::scale(m, vec3(om.kind == VoxelModel::Block ? 0.3f : 0.42f));
            VR.draw(om.gl, m * om.matrix(), L, vec4(0), itemGlints(out) ? 1.f : 0.f);
          }
        }
        return;
      }
      if (type == "kiln") {   // sim/pit_kiln.h: the pottery in its quarters, a bed of straw over it, the logs on that
        begin();
        if (e.contains("items") && e["items"].is_array()) for (int k = 0; k < 4 && k < (int)e["items"].size(); k++) {
          if (e["items"][k].is_null()) continue;
          const ItemStack st = ItemStack::fromJson(e["items"][k]);
          const VoxelModel& im = itemModel(stackModelKey(st));
          if (!im.ready()) continue;
          mat4 m = glm::translate(base, vec3((k % 2) * 0.5f + 0.25f, 0.2f, (k / 2) * 0.5f + 0.25f));
          m = glm::rotate(m, k * 0.9f, vec3(0, 1, 0)); m = glm::scale(m, vec3(0.42f));
          VR.draw(im.gl, m * im.matrix(), L);
        }
        const int straw = e.value("straw", 0), logs = e.value("logs", 0);
        if (straw > 0) if (const VoxelModel& sm = itemModel("thatch"); sm.ready()) {
          const float h = 0.04f + 0.08f * straw / 8.f;
          mat4 m = glm::translate(base, vec3(0.5f, 0.3f + h * 0.5f, 0.5f)); m = glm::scale(m, vec3(0.98f, h, 0.98f));
          VR.draw(sm.gl, m * sm.matrix(), L);
        }
        if (logs > 0) if (const VoxelModel& lm = itemModel("log"); lm.ready()) for (int i = 0; i < logs; i++) {
          const int course = i / 4, j = i % 4;
          const float a = 0.125f + j * 0.25f, y = 0.42f + 0.11f + course * 0.22f;
          mat4 m = glm::translate(base, course % 2 ? vec3(0.5f, y, a) : vec3(a, y, 0.5f));
          m = glm::rotate(m, course % 2 ? PI / 2 : 0.f, vec3(0, 1, 0));
          m = glm::rotate(m, PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.22f, 0.98f, 0.22f));
          VR.draw(lm.gl, m * lm.matrix(), L);
        }
        return;
      }
      if (type == "ingots" || type == "logs") {   // sim/piles.h: ingot piles (brickwork courses) and log piles (a crib)
        if (!e.contains("item")) return;
        const ItemStack st = ItemStack::fromJson(e["item"]);
        if (st.empty() || !st.def()) return;
        const VoxelModel& im = itemModel(stackModelKey(st));
        if (!im.ready()) return;
        begin();
        const bool logs = type == "logs";
        const int n = std::min(st.n, logs ? LOG_PILE_MAX : INGOT_PILE_MAX);
        const float gl = itemGlints(st) ? 1.f : 0.f;
        for (int i = 0; i < n; i++) {
          const PileSlot sl = logs ? logSlot(i) : ingotSlot(i);
          mat4 m = glm::translate(base, sl.pos);
          m = glm::rotate(m, sl.yaw, vec3(0, 1, 0));
          if (logs) {   // the log's rings at its ends: its up turned to lie along the slot
            m = glm::rotate(m, PI / 2, vec3(1, 0, 0));
            m = glm::scale(m, vec3(0.245f, 0.99f, 0.245f));
          } else {      // an ingot: a bar half a block long along the slot, a quarter wide, an eighth high
            const VoxelModel& bar = ingotBar(st);
            m = glm::rotate(m, PI / 2, vec3(0, 1, 0));   // (the bar's length along x: along the slot)
            VR.draw(bar.gl, m * bar.matrix(), L, vec4(0), gl);
            continue;
          }
          VR.draw(im.gl, m * im.matrix(), L, vec4(0), gl);
        }
        return;
      }
      if (type == "rack") {
        // batche.js blockEntityDraws: up to three weapons, tools or shields resting on a weapon rack (floor or wall)
        if (!e.contains("rack") || !e["rack"].is_array()) return;
        int rk = (int)reg().block(w.getBlock(x, y, z)).num("rack");
        if (rk <= 0) return;
        int f = w.getMeta(x, y, z) & 3;
        const json& R = e["rack"];
        for (size_t k = 0; k < R.size() && k < 3; k++) {
          ItemStack st = ItemStack::fromJson(R[k]);
          if (st.empty() || !st.def()) continue;
          const VoxelModel& im = itemModel(stackModelKey(st));
          if (!im.ready()) continue;
          begin();
          mat4 m = glm::translate(mat4(1), rel) * rackSlotMat(f, rk == 2, (int)k);
          VR.draw(im.gl, m * im.matrix(), L, vec4(0), itemGlints(st) ? 1.f : 0.f);
        }
        return;
      }
      if (type == "frame") {
        // an item frame: the item sits flat against the wall behind it, turned in eighths (glow frames light it fully)
        if (!e.contains("item") || !e["item"].is_object()) return;
        ItemStack st = ItemStack::fromJson(e["item"]);
        if (st.empty() || !st.def()) return;
        static const float TURN[4] = {0, -PI / 2, PI, PI / 2};
        int f = w.getMeta(x, y, z) & 3;
        bool glow = reg().block(w.getBlock(x, y, z)).num("frame") == 2;
        // a filled map: the sheet itself across the face, its picture and markers (turned in quarters, as maps are)
        if (isMapStack(st) && !stackMapId(st).empty()) {
          if (const PaperMap* pm = mapStore(*s.level).get(stackMapId(st))) {
            mat4 m = glm::translate(mat4(1), rel + vec3(0.5f));
            m = glm::rotate(m, TURN[f], vec3(0, 1, 0));
            m = glm::translate(m, vec3(0, 0, -0.5f + 0.07f));
            m = glm::rotate(m, -(float)(e.value("rot", 0) % 4) * PI / 2, vec3(0, 0, 1));
            framedMaps.push_back({pm, m, glow ? vec2(1, 1) : vec2(L.sky, L.block)});
            return;
          }
        }
        const VoxelModel& im = itemModel(stackModelKey(st));
        if (!im.ready()) return;
        begin();
        mat4 m = glm::translate(mat4(1), rel + vec3(0.5f));
        m = glm::rotate(m, TURN[f], vec3(0, 1, 0));
        m = glm::translate(m, vec3(0, 0, -0.5f + std::max(0.09f, im.depth * 0.55f + 0.07f)));   // thick things stand out from the frame
        m = glm::rotate(m, -(float)e.value("rot", 0) * PI / 4, vec3(0, 0, 1));
        m = glm::scale(m, vec3(im.kind == VoxelModel::Block ? 0.36f : 0.55f));
        VR.draw(im.gl, m * im.matrix(), glow ? VoxelLight{1, 1} : L, vec4(0), itemGlints(st) ? 1.f : 0.f);
        return;
      }
      if (type == "shelf" || type == "bookcase") {
        // furniture.js blockEntityDraws: what stands on a wall shelf (3 spots), a standing shelf (6) or a bookcase (6 books)
        if (!e.contains("items") || !e["items"].is_array()) return;
        const BlockDef& bd = reg().block(w.getBlock(x, y, z));
        if (!bd.has("shelf") && !bd.has("bookcase")) return;
        bool wall = bd.str("furniture") == "shelf", cased = type == "bookcase";
        static const float FACE[4] = {0, -PI / 2, PI, PI / 2};
        mat4 pm = glm::translate(mat4(1), rel + vec3(0.5f, 0, 0.5f));
        pm = glm::rotate(pm, FACE[w.getMeta(x, y, z) & 3], vec3(0, 1, 0));
        pm = glm::translate(pm, vec3(-0.5f, 0, -0.5f));
        u8 sl = w.getLight(x, y, z);
        VoxelLight SL{(sl >> 4) / 15.f, (sl & 15) / 15.f};
        const json& its = e["items"];
        for (size_t k = 0; k < its.size() && k < 6; k++) {
          if (!its[k].is_object()) continue;
          ItemStack st = ItemStack::fromJson(its[k]);
          if (st.empty() || !st.def()) continue;
          int col = (int)k % 3, row = (int)k / 3;
          float cx = (col + 0.5f) / 3 * 0.86f + 0.07f;
          if (cased) {
            int hh = ((unsigned char)st.id[0] + (int)k * 7) % 5;
            const VoxelModel& bm = shelfBook(st.id.find("enchant") != std::string::npos ? 4 : ((int)k + hh) % 4);
            if (!bm.ready()) continue;
            begin();
            mat4 m = glm::translate(pm, vec3(cx, row ? 8.5f / 16 : 1.f / 16, 0.5f));
            m = glm::rotate(m, (((int)k * 37) % 5 - 2) * 0.03f, vec3(0, 0, 1));
            m = glm::scale(m, vec3(1, 0.9f + hh * 0.04f, 1));
            VR.draw(bm.gl, m * bm.matrix(), SL, vec4(0), itemGlints(st) ? 1.f : 0.f);
            continue;
          }
          const VoxelModel& im = itemModel(stackModelKey(st));
          if (!im.ready()) continue;
          begin();
          float top = wall ? 8.f / 16 : row ? 8.f / 16 : 1.f / 16, depth = wall ? 4.f / 16 : 5.5f / 16;
          mat4 m = pm;
          if (im.kind == VoxelModel::Block) { m = glm::translate(m, vec3(cx, top + 0.11f, depth)); m = glm::rotate(m, 0.3f * (col - 1), vec3(0, 1, 0)); m = glm::scale(m, vec3(0.22f)); }
          else { m = glm::translate(m, vec3(cx, top + 0.17f, depth)); m = glm::scale(m, vec3(0.36f)); }
          VR.draw(im.gl, m * im.matrix(), SL, vec4(0), itemGlints(st) ? 1.f : 0.f);
        }
        return;
      }
      if (type == "backpack") {
        if (!e.contains("stack")) return;
        ItemStack st = ItemStack::fromJson(e["stack"]);
        if (st.empty() || !st.def()) return;
        const VoxelModel& im = itemModel(stackModelKey(st));
        if (!im.ready()) return;
        begin();
        float yaw = e.value("yaw", 0.f);
        mat4 m = glm::translate(base, vec3(0.5f, 0.36f, 0.5f));
        m = glm::rotate(m, yaw + PI, vec3(0, 1, 0));   // standing up, its pouch toward you
        m = glm::scale(m, vec3(0.64f));
        VR.draw(im.gl, m * im.matrix(), L, vec4(0), itemGlints(st) ? 1.f : 0.f);
        return;
      }
      if (!e.contains("items") || !e["items"].is_array()) return;
      const json& items = e["items"];
      int filled = 0; for (size_t k = 0; k < items.size() && k < 4; k++) if (items[k].is_object()) filled++;
      for (size_t k = 0; k < items.size() && k < 4; k++) {
        if (!items[k].is_object()) continue;
        ItemStack st = ItemStack::fromJson(items[k]);
        if (st.empty() || !st.def()) continue;
        const VoxelModel& im = itemModel(stackModelKey(st));
        if (!im.ready()) continue;
        begin();
        float r0 = e.contains("rot") && e["rot"].is_array() && k < e["rot"].size() && e["rot"][k].is_number() && e["rot"][k].get<float>() != 0 ? e["rot"][k].get<float>() : (float)k * 0.7f;
        float ox = (float)(k % 2) * 0.5f + 0.25f, oz = (float)(k / 2) * 0.5f + 0.25f;
        static const std::regex BAR("ingot|brick|nugget|bar$");
        bool bar = std::regex_search(st.id, BAR) && !im.stand, tool = im.kind != VoxelModel::Block && isHandheld(st.id) && !im.stand;   // (a mold or a pot stands, whatever its name)
        bool block = im.kind == VoxelModel::Block;
        int n = st.n;
        // a stack of bars on its own (the ingot piles): four across the whole block, each layer turned
        // across the one under it, the pile rising as the stack grows
        const bool solo = bar && filled == 1;
        int copies = block ? std::min(4, (n + 15) / 16) : solo ? std::min(32, (n + 1) / 2) : bar ? std::min(16, (n + 3) / 4) : tool ? 1 : std::min(5, (n + 11) / 12);
        for (int c = 0; c < copies; c++) {
          mat4 m = base;
          float j = (float)((k * 7 + c * 13) % 10) / 10.f - 0.5f;
          if (block) {
            m = glm::translate(m, vec3(ox, 0.125f + c * 0.25f, oz)); m = glm::rotate(m, r0 + c * 0.25f, vec3(0, 1, 0)); m = glm::scale(m, vec3(0.25f));
          } else if (solo) {
            int layer = c / 4; float across = ((c % 4) - 1.5f) * 0.22f;
            m = glm::translate(m, vec3(0.5f, 0.03f + layer * 0.055f, 0.5f)); m = glm::rotate(m, r0 + (layer % 2) * PI / 2, vec3(0, 1, 0));
            m = glm::translate(m, vec3(across, 0, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.36f));
          } else if (bar) {
            int layer = c / 2; float side = c % 2 ? 1.f : -1.f;
            m = glm::translate(m, vec3(ox, 0.03f + layer * 0.055f, oz)); m = glm::rotate(m, r0 + (layer % 2) * PI / 2, vec3(0, 1, 0));
            m = glm::translate(m, vec3(side * 0.065f, 0, 0)); m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.36f));
          } else if (tool) {
            m = glm::translate(m, vec3(ox, (im.depth > 0 ? im.depth : 0.03f) * 0.6f + 0.005f, oz)); m = glm::rotate(m, r0 + PI / 4, vec3(0, 1, 0));
            m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.6f));
          } else if (im.stand) {
            if (c >= 2) break;   // bottles, buckets and bowls stand up
            m = glm::translate(m, vec3(ox + (c ? 0.12f : 0.f), 0.225f, oz + (c ? -0.1f : 0.f))); m = glm::rotate(m, r0 + c * 1.3f, vec3(0, 1, 0)); m = glm::scale(m, vec3(0.45f));
          } else {
            float dz = (im.depth > 0 ? im.depth : 0.02f) * 0.45f;
            m = glm::translate(m, vec3(ox + j * 0.05f, dz + 0.005f + c * dz * 1.8f, oz - j * 0.05f)); m = glm::rotate(m, r0 + c * 0.5f + j, vec3(0, 1, 0));
            m = glm::rotate(m, -PI / 2, vec3(1, 0, 0)); m = glm::scale(m, vec3(0.45f));
          }
          VR.draw(im.gl, m * im.matrix(), L, vec4(0), itemGlints(st) ? 1.f : 0.f);
        }
      }
    });
    if (begun) VR.end();
    if (!handsWater.empty()) {
      ContentDraw& cd = contentDraw();
      if (const PackModel* water = handsPart("still_water")) if (cd.begin(f, f.cam.viewProj)) {
        for (auto& sw : handsWater) handsPartDraw(s.app, cd, *water, handsBlockPose(f, sw.at, sw.facing).m, sw.light);
        cd.end();
      }
      handsWater.clear();
    }
    if (!handsLapis.empty()) {
      ContentDraw& cd = contentDraw();
      if (const PackModel* lp = handsPart("enchanting_lapis")) if (cd.begin(f, f.cam.viewProj)) {
        for (auto& l : handsLapis) { TPose p = handsBlockPose(f, l.at, l.facing); p.t(0, 0.975f, 0).s(1, l.fill, 1).t(0, -0.975f, 0); handsPartDraw(s.app, cd, *lp, p.m, l.light); }
        cd.end();
      }
      handsLapis.clear();
    }
    if (!handsBooks.empty()) {
      ContentDraw& cd = contentDraw();
      if (cd.begin(f, f.cam.viewProj)) {
        const auto& R = brewRecipes();
        auto name = [](const std::string& id) { const ItemDef* d = reg().item(id); std::string n = d ? d->name : id; for (const char* pre : {"Potion of ", "Splash Potion of ", "Lingering Potion of "}) if (n.rfind(pre, 0) == 0) n = n.substr(std::string(pre).size()); return n; };
        const int pages = std::max(1, ((int)R.size() + 9) / 10);
        for (auto& b : handsBooks) {
          const TPose base = handsBlockPose(f, b.at, b.facing);
          auto part = [&](const char* n, float tilt) {
            if (const PackModel* m = handsPart(n)) { TPose p = base; if (tilt != 0) p.t(0.5f, 0.0875f, 0.21875f).rz(tilt).t(-0.5f, -0.0875f, -0.21875f); handsPartDraw(s.app, cd, *m, p.m, b.light); }
          };
          part("book_spine", 0); part("book_base_l", 4); part("book_page_l", 4); part("book_base_r", -4); part("book_page_r", -4);
          if (b.page > 0) part("book_arrow_prev", 4);
          if (b.page < pages - 1) part("book_arrow_next", -4);
          const vec4 ink(0.2f, 0.12f, 0.06f, 1.f);
          for (int half = 0; half < 2; half++) {
            const bool left = half == 0;
            TPose hp = base; hp.t(0.5f, 0.0875f, 0.21875f).rz(left ? 4.f : -4.f).t(-0.5f, -0.0875f, -0.21875f);
            if (left) handsText(cd, hp, 10.5f, 0.1f, 6.3f, "Recipes " + std::to_string(b.page + 1) + "/" + std::to_string(pages), 0.003f, 4.4f, ink, b.light);
            for (int r = 0; r < 5; r++) {
              const int k = b.page * 10 + half * 5 + r;
              if (k >= (int)R.size()) break;
              const float rh = 0.96f, z1 = 5.6f - r * rh, x0 = left ? 8.3f : 3.3f, x1 = left ? 12.7f : 7.7f;
              handsText(cd, hp, (x0 + 1.2f * rh + x1) * 0.5f, 0.1f, z1 - rh * 0.5f, name(R[k].to) + " (" + name(R[k].from) + ")", 0.0021f, x1 - x0 - 1.2f * rh, ink, b.light);
            }
          }
        }
        cd.end();
      }
      handsBooks.clear();
    }
    if (!framedMaps.empty()) {
      ContentDraw& cd = contentDraw();
      if (cd.begin(f, f.cam.viewProj)) {
        for (auto& fm : framedMaps) drawMapSheet(cd, *s.level, fm.map, s.player, fm.M, 0.86f, fm.light);
        cd.end();
      }
      framedMaps.clear();
    }
  }
};

} // namespace

void registerPileLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<PileLayer>(); }); }

} // namespace dq
