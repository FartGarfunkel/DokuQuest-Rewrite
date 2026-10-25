// render/terrain_fx.cpp — the terrain's RenderLayer: block selection outline (the block's real
// select boxes), breaking-crack overlay, animated tiles, underwater/lava fog, keeping the mesher's
// copy of the tiles current, and test hooks (DQ_RENDER_CAM / DQ_RENDER_TIME / DQ_RENDER_STATS).
#include "client/session.h"
#include "client/app.h"
#include "render/tile_anim.h"
#include "world/shapes.h"
#include "world/light.h"
#include <cstdlib>

namespace dq {

void registerShowcaseWorld();   // world/showcase.cpp

namespace {
struct FxVert { vec3 p; vec2 uv; };

class TerrainFx : public RenderLayer {
public:
  explicit TerrainFx(GameSession& s) {
    sh.load("terrain_fx.glsl", "terrain_fx.glsl");
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FxVert), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(FxVert), (void*)12);
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    syncTiles(s);
    if (const char* c = std::getenv("DQ_RENDER_CAM")) {
      float v[5] = {0, 0, 0, 0, 0};
      if (std::sscanf(c, "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]) >= 3) { camSet = true; dbgPos = vec3(v[0], v[1], v[2]); dbgYaw = v[3]; dbgPitch = v[4]; }
    }
    if (const char* t = std::getenv("DQ_RENDER_TIME")) dbgTime = std::atof(t);
    stats = std::getenv("DQ_RENDER_STATS") != nullptr;
    if (const char* e = std::getenv("DQ_RENDER_EDIT")) edits = e;   // "x,y,z,key;..." applied at frame 90 (light/remesh tests)
  }
  ~TerrainFx() override { if (vao) glDeleteVertexArrays(1, &vao); if (vbo) glDeleteBuffers(1, &vbo); }
  const char* name() const override { return "terrain-fx"; }
  int order() const override { return -100; }

  void syncTiles(GameSession& s) {
    App& a = s.app;
    if (tilesData == a.tiles.rgba.data() && tilesCount == a.tiles.count) return;
    tilesData = a.tiles.rgba.data(); tilesCount = a.tiles.count;
    s.worldRenderer.setTileTextures(a.tileTex);
    s.worldRenderer.setTiles(a.tiles);
    anim.reset();
  }

  void update(GameSession& s, double dt) override {
    App& a = s.app;
    syncTiles(s);
    anim.update(a.tiles, a.tileTex, a.time);
    FrameCtx& f = s.frame;
    // the original env: a light floor lifted by the brightness option (more in the Depths and the End), wind
    f.minLight = 0.025f + a.settings.brightness * 0.07f;
    if (f.dim == DIM_NETHER) f.minLight = std::max(f.minLight, 0.07f);
    if (f.dim == DIM_END) f.minLight = std::max(f.minLight, 0.12f);
    f.wind = a.settings.wavingPlants ? 0.45f + 0.5f * 0.9f : 0.f;
    s.worldRenderer.plantDetail = a.settings.plantDetail == "off" || a.settings.plantDetail == "flat" ? 0 : a.settings.plantDetail == "all" ? 2 : 1;
    // under water or lava: the world closes in (original viewEnv)
    World& w = s.world();
    glm::dvec3 e = f.cam.pos;
    int ex = ifloor(e.x), ey = ifloor(e.y), ez = ifloor(e.z);
    BlockId eb = w.getBlock(ex, ey, ez);
    const Registry& R = reg();
    f.eyeFluid = R.tLiquid[eb] == 1 || isWaterlogged(eb, w.getMeta(ex, ey, ez)) ? 1 : R.tLiquid[eb] == 2 ? 2 : 0;
    if (f.eyeFluid) { f.fogColor = f.eyeFluid == 1 ? vec3(0.09f, 0.23f, 0.48f) : vec3(1.f, 0.35f, 0.04f); f.fogStart = 0; f.fogEnd = 22; }
    // test hooks
    if (camSet && s.player) { s.player->pos = dbgPos; s.player->vel = vec3(0); s.player->yaw = dbgYaw; s.player->pitch = dbgPitch; s.player->flying = true; }
    if (dbgTime >= 0) s.level->info.time = dbgTime;
    if (!edits.empty() && ++editFrame == 90) {
      for (auto& part : splitStr(edits, ';')) {
        int x, y, z; char key[96] = {0};
        if (std::sscanf(part.c_str(), "%d,%d,%d,%95s", &x, &y, &z, key) == 4) w.setBlock(x, y, z, reg().blockId(key), 0, SetFlags::Urgent);
      }
    }
    if (stats) {
      s.worldRenderer.timing = true;
      double now = nowSeconds(), fdt = lastT > 0 ? now - lastT : 0; lastT = now;
      acc += fdt; frames++; worst = std::max(worst, fdt); gpuAcc += s.worldRenderer.gpuMs;
      if (frames >= 120) {
        auto* le = w.system<LightEngine>();
        DQ_LOG("render: %.2f ms/frame (worst %.1f, terrain GL %.2f ms)  sections %d/%d  arena %.1f/%.1f MB  mesh %.2f ms/section  jobs %d  chunks %zu  light %.2f ms (%d cells)",
               acc / frames * 1000, worst * 1000, gpuAcc / frames, s.worldRenderer.drawnSections, s.worldRenderer.totalSections, s.worldRenderer.usedBytes / 1048576.0,
               s.worldRenderer.arenaBytes / 1048576.0, s.worldRenderer.meshMsAvg, s.worldRenderer.pendingJobs, w.chunks().size(), le ? le->lastMs : 0, le ? le->lastCells : 0);
        acc = 0; frames = 0; worst = 0; gpuAcc = 0;
      }
    }
  }

  // boxes of the targeted block (select boxes), world space
  bool targetBoxes(GameSession& s, std::vector<BoxF>& boxes) {
    if (!s.target.hit) return false;
    ivec3 b = s.target.block;
    World& w = s.world();
    if (!blockShapeBoxes(w, b.x, b.y, b.z, ShapePurpose::Select, boxes)) { boxes.clear(); boxes.push_back({vec3(b), vec3(b) + vec3(1)}); }
    if (boxes.empty()) boxes.push_back({vec3(b), vec3(b) + vec3(1)});
    return true;
  }

  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    // the crack on the block being broken
    if (!s.breaking || !s.target.hit || s.target.block != s.breakBlock || !s.player || s.player->creative()) return;
    const BlockDef& b = reg().block(s.target.id);
    float t = std::max(0.05f, (b.hardness < 0 ? 1e9f : b.hardness) * 1.5f);
    int stage = clampv((int)(s.breakProgress / t * 10), 0, 9);
    int layer = reg().tile(strFormat("crack_%d", stage));
    if (layer < 0) return;
    std::vector<BoxF> boxes; targetBoxes(s, boxes);
    std::vector<FxVert> v;
    vec3 o = vec3(glm::dvec3(0) - f.cam.origin);
    for (auto& bx : boxes) {
      vec3 lo = bx.lo - vec3(0.002f), hi = bx.hi + vec3(0.002f), base = vec3(s.target.block);
      for (int fi = 0; fi < 6; fi++) {
        static const int C[6][4][3] = {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
                                       {{1, 0, 1}, {0, 0, 1}, {0, 0, 0}, {1, 0, 0}}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}};
        FxVert q[4];
        for (int k = 0; k < 4; k++) {
          vec3 p(C[fi][k][0] ? hi.x : lo.x, C[fi][k][1] ? hi.y : lo.y, C[fi][k][2] ? hi.z : lo.z);
          vec3 l = p - base; vec2 uv;
          switch (fi) { case 0: uv = {1 - l.z, 1 - l.y}; break; case 1: uv = {l.z, 1 - l.y}; break; case 2: uv = {l.x, l.z}; break; case 3: uv = {1 - l.x, l.z}; break; case 4: uv = {l.x, 1 - l.y}; break; default: uv = {1 - l.x, 1 - l.y}; }
          q[k] = {p + o, uv};
        }
        v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
      }
    }
    sh.use(); f.apply(sh); s.app.tileTex.bind(sh);
    sh.set("uViewProj", f.cam.viewProj); sh.set("uMode", 1); sh.set("uLayer", (float)layer);
    glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1, -1); glDepthMask(GL_FALSE);
    drawVerts(v, GL_TRIANGLES);
    glDepthMask(GL_TRUE); glDisable(GL_POLYGON_OFFSET_FILL); glDisable(GL_BLEND);
  }

  void drawTranslucent(GameSession& s, const FrameCtx& f) override {
    // the outline of the block under the crosshair (its real select boxes: slabs, stairs, fences...)
    if (s.hudHidden || s.app.topScreen()) return;
    std::vector<BoxF> boxes;
    if (!targetBoxes(s, boxes)) return;
    std::vector<FxVert> v;
    vec3 o = vec3(glm::dvec3(0) - f.cam.origin);
    static const int E[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& b : boxes) {
      vec3 a = b.lo - vec3(0.002f) + o, c = b.hi + vec3(0.002f) + o;
      vec3 C[8] = {{a.x, a.y, a.z}, {c.x, a.y, a.z}, {c.x, a.y, c.z}, {a.x, a.y, c.z}, {a.x, c.y, a.z}, {c.x, c.y, a.z}, {c.x, c.y, c.z}, {a.x, c.y, c.z}};
      for (auto& e : E) { v.push_back({C[e[0]], vec2(0)}); v.push_back({C[e[1]], vec2(0)}); }
    }
    sh.use(); f.apply(sh);
    sh.set("uViewProj", f.cam.viewProj); sh.set("uMode", 0); sh.set("uColor", vec4(0, 0, 0, 0.55f));
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    drawVerts(v, GL_LINES);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  }

private:
  Shader sh;
  GLuint vao = 0, vbo = 0;
  TileAnimator anim;
  const void* tilesData = nullptr; int tilesCount = -1;
  bool camSet = false; vec3 dbgPos{0}; float dbgYaw = 0, dbgPitch = 0; double dbgTime = -1;
  bool stats = false; double acc = 0, worst = 0, lastT = 0, gpuAcc = 0; int frames = 0;
  std::string edits; int editFrame = 0;
  void drawVerts(const std::vector<FxVert>& v, GLenum mode) {
    if (v.empty()) return;
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(FxVert)), v.data(), GL_STREAM_DRAW);
    glDrawArrays(mode, 0, (GLsizei)v.size());
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
};
} // namespace

// Client-side registration for the terrain (call once at start-up, before a world opens).
void registerTerrainLayers() {
  registerRenderLayer([](GameSession& s) { return std::make_shared<TerrainFx>(s); });
  registerShowcaseWorld();
}

} // namespace dq
