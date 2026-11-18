// tools/test/mesher_carve_test.cpp — headless mesher checks for chiseled (carved) blocks: a chiseled block keeps every face
// it shows with nothing round it when a chest, barrel, cabinet, bed, lantern... stands on it or beside it. A face may only
// go when the neighbour really covers it (a plain full cube). No GL context: tiles and station models are loaded on the
// CPU, then one section is meshed. Run: ./build/test_mesher [--userdir DIR] (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "assets/tileset.h"
#include "assets/local_models.h"
#include "render/mesher.h"
#include "render/chest_layer.h"
#include "render/mesh_ctx.h"
#include "world/carve_forms.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace dq;
static int fails = 0, passes = 0;
#define CHECK(c, ...) do { if (c) { passes++; } else { printf("  FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static const int SX = 6, SY = 6, SZ = 6;   // the chiseled block's cell in the section

// the area (in 1/16 px squared) of the chiseled block's own faces, per outward normal (FACES order)
struct Areas { double a[6] = {}; };
static Areas stoneAreas(const MeshOutput& out) {
  Areas r;
  auto pos = [](u32 v) { return (float)v / 256.f - 16.f; };
  for (const std::vector<ChunkVertex>* list : {&out.opaque, &out.cutout, &out.trans})
    for (size_t q = 0; q + 3 < list->size(); q += 4) {
      vec3 p[4];
      for (int k = 0; k < 4; k++) { const ChunkVertex& v = (*list)[q + k]; p[k] = vec3(pos(v.a & 16383), pos((v.a >> 14) & 16383), pos(v.b & 16383)); }
      const int n = ((*list)[q].a >> 28) & 7;
      if (n > 5) continue;
      const vec3 c = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
      const vec3 in = c - vec3((float)FACES[n].n[0], (float)FACES[n].n[1], (float)FACES[n].n[2]) * 0.01f;   // just behind the face: whose it is
      if (in.x < SX || in.x > SX + 1 || in.y < SY || in.y > SY + 1 || in.z < SZ || in.z > SZ + 1) continue;
      r.a[n] += glm::length(glm::cross(p[1] - p[0], p[3] - p[0])) * 256.0;
    }
  return r;
}

static std::unique_ptr<MeshInput> emptyInput() {
  auto in = std::make_unique<MeshInput>();
  for (int i = 0; i < MeshInput::N * MeshInput::N * MeshInput::NY; i++) {
    in->ids[i] = AIR; in->meta[i] = 0; in->carve[i] = 0; in->coat[i] = 0; in->light[i] = 0xF0; in->rgb[i] = LightRgb();
    for (int c = 0; c < 3; c++) in->rgb[i].s[c] = 255;
  }
  for (auto& c : in->climate) c = 128;
  in->dim = 1; in->cx = 0; in->sy = 0; in->cz = 0; in->empty = false;
  return in;
}
static void put(MeshInput& in, int x, int y, int z, BlockId id, u8 meta = 0, u8 carve = 0) {
  const int i = MeshInput::idx(x, y, z);
  in.ids[i] = id; in.meta[i] = meta; in.carve[i] = carve; in.light[i] = 0;
}

int main(int argc, char** argv) {
  CoreOptions co; co.userDirOverride = "/tmp/dq-mesher-test";
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) co.userDirOverride = argv[++i];
  if (!initCore(co)) { printf("initCore failed\n"); return 2; }
  TileSet ts; loadBuiltinTiles(ts);
  loadStationModels(ts);   // the chest, the anvil... drawn from their models (they don't fill their cell)
  setMeshTiles(ts);
  const Registry& R = reg();
  MesherOptions opt; opt.greedy = false;
  const BlockId stone = R.blockId("stone");
  // the neighbours: things that stand on or beside a block without filling their cell, and a plain cube for contrast
  const char* others[] = {"chest", "ender_chest", "barrel", "oak_cabinet", "lantern", "white_bed", "anvil", "crafting_table", "furnace", "glass", "oak_slab", "stone"};
  // carve patterns: a top corner, a bottom corner, a bottom slab, a top slab, half along x, a quarter, a stairs form
  const u8 carves[] = {0x80, 0x01, 0xCC, 0x33, 0x0F, 0x88, 0x44};
  static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  MeshOutput out;
  int cases = 0;
  for (u8 cv : carves) {
    auto base = emptyInput(); put(*base, SX, SY, SZ, stone, 0, cv);
    buildSectionMesh(*base, out, opt);
    const Areas alone = stoneAreas(out);
    double total = 0; for (double a : alone.a) total += a;
    CHECK(total > 0, "carve %02x: the chiseled block alone draws faces", cv);
    for (const char* key : others) {
      const BlockId nb = R.blockId(key);
      if (!nb) { printf("  (no block %s)\n", key); continue; }
      // a plain cube that fills its cell may hide the face it touches; anything else must leave it
      const bool fills = R.tOpaque[nb] && R.tRender[nb] == (u8)Render::Cube && !R.tShape[nb] && !stationModelOpen(nb);
      for (int f = 0; f < 6; f++) {
        auto in = emptyInput(); put(*in, SX, SY, SZ, stone, 0, cv);
        put(*in, SX + D[f][0], SY + D[f][1], SZ + D[f][2], nb);
        buildSectionMesh(*in, out, opt);
        const Areas with = stoneAreas(out);
        cases++;
        for (int g = 0; g < 6; g++) {
          if (g == f && fills) {
            CHECK(with.a[g] <= alone.a[g] + 0.5, "carve %02x, %s at face %d: face %d grew", cv, key, f, g);
            bool touches = false;   // some octant on that side is still there: the stone beside covers it
            for (int m = 0; m < 8; m++) { const int h[3] = {(m >> 2) & 1, (m >> 1) & 1, m & 1}, ax = g / 2; if (h[ax] == (g % 2 == 0 ? 1 : 0) && !((cv >> m) & 1)) touches = true; }
            if (nb == stone && touches) CHECK(with.a[g] < alone.a[g] - 0.5, "carve %02x, stone at face %d: the face it covers still drawn", cv, f);
            continue;
          }
          CHECK(std::fabs(with.a[g] - alone.a[g]) < 0.5, "carve %02x, %s at face %d: face %d area %.0f, alone %.0f", cv, key, f, g, with.a[g], alone.a[g]);
        }
      }
    }
  }
  // a chest standing open is drawn by the chest layer (render/chest_layer.cpp), lid up: the block beside or under it,
  // plain or chiseled, keeps the face it shares with it
  const BlockId chest = R.blockId("chest");
  for (u8 cv : {(u8)0, (u8)0x80, (u8)0x01}) for (int f = 0; f < 6; f++) {
    auto base = emptyInput(); put(*base, SX, SY, SZ, stone, 0, cv);
    buildSectionMesh(*base, out, opt);
    const Areas alone = stoneAreas(out);
    auto in = emptyInput(); put(*in, SX, SY, SZ, stone, 0, cv);
    const int cx = SX + D[f][0], cy = SY + D[f][1], cz = SZ + D[f][2];
    put(*in, cx, cy, cz, chest);
    const vec3 o = in->origin();
    g_chestHidden[0][0] = (int)o.x + cx; g_chestHidden[0][1] = (int)o.y + cy; g_chestHidden[0][2] = (int)o.z + cz; g_chestHiddenN = 1;
    buildSectionMesh(*in, out, opt);
    g_chestHiddenN = 0;
    const Areas with = stoneAreas(out);
    cases++;
    CHECK(std::fabs(with.a[f] - alone.a[f]) < 0.5, "carve %02x, open chest at face %d: face area %.0f, alone %.0f", cv, f, with.a[f], alone.a[f]);
  }
  printf("mesher carve checks: %d cases, %d passed, %d failed\n", cases, passes, fails);
  return fails;
}
