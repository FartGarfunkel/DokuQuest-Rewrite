// render/moving_layer.cpp — moving structures (sim/moving.h) drawn while they move: each moving body is meshed once when it
// sets off, by the ordinary block mesher (a section's worth of its blocks at a time: stairs, chests, facings exactly as they
// stand), and drawn every frame through the terrain's own shader with the body's turn or slide as a model matrix. Its
// blocks are out of the world meanwhile, so nothing is drawn where they were. Holding glue, the glued faces near you show
// as a faint amber outline.
#include "render/world_renderer.h"
#include "render/mesher.h"
#include "client/session.h"
#include "client/app.h"
#include "sim/moving.h"
#include "sim/glue.h"
#include "sim/player.h"
#include "core/registry.h"
#include <map>

namespace dq {

namespace {
struct FxVert { vec3 p; vec2 uv; };

class MovingLayer : public RenderLayer {
public:
  explicit MovingLayer(GameSession&) {
    sh.load("terrain_fx.glsl", "terrain_fx.glsl");
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FxVert), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(FxVert), (void*)12);
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
  ~MovingLayer() override { if (vao) glDeleteVertexArrays(1, &vao); if (vbo) glDeleteBuffers(1, &vbo); }
  const char* name() const override { return "moving"; }
  int order() const override { return 114; }

  void update(GameSession& s, double dt) override {
    if (!s.level || !s.player) return;
    WorldRenderer& wr = s.worldRenderer;
    const std::vector<Body>& list = movingBodiesList(*s.level);
    // gone (finished, or another realm): let their meshes go
    for (auto it = drawn.begin(); it != drawn.end();) {
      bool live = false;
      for (const Body& b : list) if (b.id == it->first && b.realm == s.player->realm) live = true;
      if (!live) { for (auto& [g, id] : it->second) wr.looseFree(id); it = drawn.erase(it); }
      else ++it;
    }
    for (const Body& b : list) if (b.realm == s.player->realm && !drawn.count(b.id)) drawn[b.id] = mesh(s, b);
    glueT -= dt;
    if (glueT <= 0) { glueT = 0.25; findGlue(s); }
  }

  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (drawn.empty() || !s.level) return;
    for (int pass = 0; pass < 2; pass++) forEach(s, f, [&](u32 id, const mat4& m) { s.worldRenderer.drawLoose(f, pass, id, m); });
  }
  void drawTranslucent(GameSession& s, const FrameCtx& f) override {
    if (!drawn.empty() && s.level) {
      glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      forEach(s, f, [&](u32 id, const mat4& m) { s.worldRenderer.drawLoose(f, 2, id, m); });
      glDisable(GL_BLEND);
    }
    drawGlue(s, f);
  }

private:
  using Meshes = std::vector<std::pair<ivec3, u32>>;   // (16-block group of the body, loose mesh)
  std::map<u32, Meshes> drawn;
  Shader sh; GLuint vao = 0, vbo = 0;
  double glueT = 0;
  std::vector<std::pair<ivec3, int>> glueFaces;   // glued faces near you (cell, face) while you hold glue

  static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

  Meshes mesh(GameSession& s, const Body& b) {
    Meshes out;
    World* wp = s.level->worldIfOpen(b.realm);
    if (!wp) return out;
    const ivec3 M = b.m.machine;
    // the light it moves in: the brightest open cell beside the machine (the machine's own cell is solid, and dark)
    u8 light = 0; LightRgb rgb{}; int best = -1;
    for (int f = 0; f < 7; f++) {
      const ivec3 q = f < 6 ? M + faceDir(f) : M;
      if (f < 6 && reg().tSolid[wp->getBlock(q.x, q.y, q.z)]) continue;
      const u8 l = wp->getLight(q.x, q.y, q.z);
      const int v = std::max(l >> 4, l & 15);
      if (v <= best) continue;
      best = v; light = l;
      if (Chunk* c = wp->chunk(dimOfY(q.y), q.x >> 4, q.z >> 4)) rgb = c->getLightRgb(q.x & 15, q.y & 255, q.z & 15); else rgb = fullSunRgb();
    }
    std::map<std::tuple<int, int, int>, bool> groups;
    for (const BodyBlock& k : b.blocks) groups[{fdiv(k.rel.x, 16), fdiv(k.rel.y, 16), fdiv(k.rel.z, 16)}] = true;
    auto in = std::make_unique<MeshInput>();
    for (auto& [gk, _] : groups) {
      const ivec3 G(std::get<0>(gk), std::get<1>(gk), std::get<2>(gk));
      std::memset(in->ids, 0, sizeof in->ids); std::memset(in->meta, 0, sizeof in->meta); std::memset(in->carve, 0, sizeof in->carve);
      std::memset(in->coat, 0, sizeof in->coat); std::memset(in->light, light, sizeof in->light); std::memset(in->climate, 128, sizeof in->climate);
      for (auto& r : in->rgb) r = rgb;
      in->potPlants.clear();
      const ivec3 corner = M + G * 16;
      in->dim = dimOfY(corner.y); in->cx = fdiv(corner.x, 16); in->cz = fdiv(corner.z, 16); in->sy = ((corner.y & 255) >> 4);
      in->empty = true;
      for (const BodyBlock& k : b.blocks) {
        const ivec3 l = k.rel - G * 16;
        if (l.x < -1 || l.x > 16 || l.z < -1 || l.z > 16 || l.y < MeshInput::Y0 || l.y > 16) continue;
        const int i = MeshInput::idx(l.x, l.y, l.z);
        in->ids[i] = k.id; in->meta[i] = k.meta; in->carve[i] = k.carve;
        if (l.x >= 0 && l.x < 16 && l.y >= 0 && l.y < 16 && l.z >= 0 && l.z < 16) in->empty = false;
      }
      if (in->empty) continue;
      if (const u32 id = s.worldRenderer.looseBuild(*in)) out.push_back({G, id});
    }
    return out;
  }

  template <class F> void forEach(GameSession& s, const FrameCtx& f, F&& fn) {
    for (const Body& b : movingBodiesList(*s.level)) {
      auto it = drawn.find(b.id);
      if (it == drawn.end()) continue;
      const vec3 rel = f.cam.rel(glm::dvec3(b.m.machine));
      if (glm::dot(rel, rel) > 256.f * 256.f) continue;
      const mat4 base = glm::translate(mat4(1), rel) * b.transform();
      for (auto& [G, id] : it->second) fn(id, base * glm::translate(mat4(1), vec3(G * 16)));
    }
  }

  void findGlue(GameSession& s) {
    glueFaces.clear();
    if (!s.player || s.player->held().id != "glue") return;
    World& w = s.world();
    const ivec3 c = ivec3(glm::floor(s.player->pos));
    for (int x = -8; x <= 8; x++) for (int y = -8; y <= 8; y++) for (int z = -8; z <= 8; z++) {
      const ivec3 p = c + ivec3(x, y, z);
      if (!glueMask(w, p)) continue;
      for (int f = 0; f < 6; f += 2) if (glued(w, p, f)) glueFaces.push_back({p, f});   // (each pair once: from its west, lower or north block)
      for (int f = 1; f < 6; f += 2) { const ivec3 q = p + faceDir(f); if (!glueMask(w, q) || std::abs(q.x - c.x) > 8 || std::abs(q.y - c.y) > 8 || std::abs(q.z - c.z) > 8) if (glued(w, p, f)) glueFaces.push_back({p, f}); }
    }
  }

  void drawGlue(GameSession& s, const FrameCtx& f) {
    if (glueFaces.empty() || !s.player || s.player->held().id != "glue") return;
    std::vector<FxVert> v;
    for (auto& [p, face] : glueFaces) {
      const ivec3 d = faceDir(face);
      const vec3 o = f.cam.rel(glm::dvec3(p)) + vec3(0.5f) + vec3(d) * 0.503f;
      const vec3 a = d.x ? vec3(0, 0.42f, 0) : vec3(0.42f, 0, 0), bb = d.z ? vec3(0, 0.42f, 0) : (d.x ? vec3(0, 0, 0.42f) : vec3(0, 0, 0.42f));
      const vec3 C[4] = {o - a - bb, o + a - bb, o + a + bb, o - a + bb};
      for (int i = 0; i < 4; i++) { v.push_back({C[i], vec2(0)}); v.push_back({C[(i + 1) & 3], vec2(0)}); }
    }
    sh.use(); f.apply(sh);
    sh.set("uViewProj", f.cam.viewProj); sh.set("uMode", 0); sh.set("uColor", vec4(1.f, 0.72f, 0.2f, 0.55f));
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(FxVert)), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, (GLsizei)v.size());
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
  }
};
}

void registerMovingLayer() { registerRenderLayer([](GameSession& s) { return std::make_shared<MovingLayer>(s); }); }

} // namespace dq
