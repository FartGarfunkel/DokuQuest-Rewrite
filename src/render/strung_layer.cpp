// render/strung_layer.cpp — chains and ropes strung between blocks (sim/strung.h): a chain as interlocking 3D links, each
// turned a quarter from the last, in iron; a rope as a twisted three-strand cord, frayed at its ends; a rope bridge's slats
// across it, swaying a little. Small voxel models (assets/voxel.h) placed along the sagging line, lit like the world.
#include "client/session.h"
#include "render/item_layers.h"
#include "render/frame.h"
#include "assets/voxel.h"
#include "sim/strung.h"
#include "world/world.h"
#include <cmath>

namespace dq {

namespace {

struct Models { VoxelMeshGL link, rope, fray, slat; bool built = false; };
Models& models() {
  static Models M;
  if (M.built) return M;
  M.built = true;
  auto up = [](VoxelMeshGL& gl, const VoxelGrid& g) { VoxelMeshData d; buildVoxelMesh(g, d); gl.upload(d); };
  {   // a link: an oval ring 6 x 10, 2 thick
    VoxelGrid g({6, 10, 2});
    for (int y = 0; y < 10; y++) for (int x = 0; x < 6; x++) {
      const bool edge = x == 0 || x == 5 || y == 0 || y == 9, corner = (x == 0 || x == 5) && (y == 0 || y == 9);
      if (!edge || corner) continue;
      const int k = 150 + (y > 6 ? 30 : 0) - (x == 0 ? 25 : 0);
      for (int z = 0; z < 2; z++) g.set(x, y, z, voxRGBA(k, k + 2, k + 8));
    }
    up(M.link, g);
  }
  {   // a rope's length: three strands twisting round each other
    VoxelGrid g({3, 8, 3});
    for (int y = 0; y < 8; y++) for (int z = 0; z < 3; z++) for (int x = 0; x < 3; x++) {
      if ((x == 0 || x == 2) && (z == 0 || z == 2)) continue;
      const int s = (x + z * 2 + y) % 3;
      const int k = s == 0 ? 168 : s == 1 ? 140 : 118;
      g.set(x, y, z, voxRGBA(k, k * 4 / 5, k * 3 / 5));
    }
    up(M.rope, g);
  }
  {   // a frayed end: loose fibres spreading out
    VoxelGrid g({5, 4, 5});
    for (int y = 0; y < 4; y++) for (int z = 0; z < 5; z++) for (int x = 0; x < 5; x++) {
      const bool core = x >= 1 && x <= 3 && z >= 1 && z <= 3 && y < 2;
      const bool fibre = y >= 1 && ((x + z * 3 + y) % 4 == 0);
      if (core || fibre) g.set(x, y, z, voxRGBA(176, 146, 104));
    }
    up(M.fray, g);
  }
  {   // a slat: a short plank board across the rope
    VoxelGrid g({34, 2, 8});
    for (int z = 0; z < 8; z++) for (int y = 0; y < 2; y++) for (int x = 0; x < 34; x++) {
      const int grain = ((x * 7 + z * 3) % 11 == 0) ? -18 : 0, k = (z == 0 || z == 7 ? 128 : 152) + grain + (y ? 10 : 0);
      g.set(x, y, z, voxRGBA(k, k * 3 / 4, k / 2));
    }
    up(M.slat, g);
  }
  return M;
}

// a frame along `dir` (its y), the rest square to it
mat4 along(vec3 at, vec3 dir, float roll) {
  const vec3 y = glm::normalize(dir);
  vec3 x = std::fabs(y.y) > 0.95f ? vec3(1, 0, 0) : glm::normalize(glm::cross(y, vec3(0, 1, 0)));
  vec3 z = glm::cross(x, y);
  if (roll) { const vec3 x2 = x * std::cos(roll) + z * std::sin(roll); z = glm::cross(x2, y); x = x2; }
  mat4 m(1); m[0] = vec4(x, 0); m[1] = vec4(y, 0); m[2] = vec4(z, 0); m[3] = vec4(at, 1);
  return m;
}

class StrungLayer : public RenderLayer {
public:
  const char* name() const override { return "strung"; }
  int order() const override { return 122; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.level || !s.player) return;
    const auto list = strungList(*s.level);
    if (list.empty()) return;
    World& w = s.world();
    Models& M = models();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    const float V = 1.f / 32.f;
    for (const Strung& st : list) {
      if (st.realm != s.player->realm) continue;
      const vec3 a = strungEnd(*s.level, st, false), b = strungEnd(*s.level, st, true);
      const vec3 ra = f.cam.rel(glm::dvec3(a)), rb = f.cam.rel(glm::dvec3(b));
      const vec3 mid = (ra + rb) * 0.5f;
      const float len = glm::distance(a, b);
      if (glm::dot(mid, mid) > 96.f * 96.f || !f.cam.frustum.sphere(mid, len * 0.5f + 1.f)) continue;
      if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
      const vec3 side = std::hypot(b.x - a.x, b.z - a.z) > 0.01f ? glm::normalize(glm::cross(b - a, vec3(0, 1, 0))) : vec3(1, 0, 0);
      const float sway = st.slats ? std::sin((float)f.time * 1.3f + st.id) * 0.05f : 0.f;
      auto P = [&](float t) { return strungPoint(*s.level, st, t) + side * (sway * 4 * t * (1 - t)); };
      const float step = st.chain ? 0.25f : 8 * V;
      const int n = std::max(2, (int)std::ceil(len / step));
      for (int i = 0; i < n; i++) {
        const float t0 = (float)i / n, t1 = (float)(i + 1) / n;
        const vec3 p0 = P(t0), p1 = P(t1), c = (p0 + p1) * 0.5f;
        const u8 l = w.getLight(ifloor(c.x), ifloor(c.y), ifloor(c.z));
        const VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
        const vec3 rc = f.cam.rel(glm::dvec3(c));
        if (st.chain) {
          const mat4 m = along(rc, p1 - p0, (i & 1) ? PI * 0.5f : 0.f) * glm::scale(mat4(1), vec3(V)) * glm::translate(mat4(1), vec3(-3, -5, -1));
          VR.draw(M.link, m, L);
        } else {
          const float k = glm::distance(p0, p1) / (8 * V);
          const mat4 m = along(rc, p1 - p0, i * 0.4f) * glm::scale(mat4(1), vec3(V, V * k, V)) * glm::translate(mat4(1), vec3(-1.5f, -4, -1.5f));
          VR.draw(M.rope, m, L);
        }
      }
      if (!st.chain) for (int e = 0; e < 2; e++) {   // the frayed ends at the blocks
        const vec3 p = e ? P(1) : P(0), q = e ? P(0.97f) : P(0.03f);
        const u8 l = w.getLight(ifloor(p.x), ifloor(p.y), ifloor(p.z));
        VR.draw(M.fray, along(f.cam.rel(glm::dvec3(p)), p - q, 0) * glm::scale(mat4(1), vec3(V)) * glm::translate(mat4(1), vec3(-2.5f, -2, -2.5f)), VoxelLight{(l >> 4) / 15.f, (l & 15) / 15.f});
      }
      if (st.slats) {
        const int ns = std::max(1, (int)std::floor(len / 0.3f));
        for (int i = 0; i < ns; i++) {
          const float t = (i + 0.5f) / ns;
          const vec3 p = P(t), q = P(std::min(1.f, t + 0.02f));
          const u8 l = w.getLight(ifloor(p.x), ifloor(p.y + 0.2f), ifloor(p.z));
          // the board across: its long side (grid x) along `side`, lying on the rope
          mat4 m(1); const vec3 fwd = glm::normalize(q - p), up = glm::normalize(glm::cross(side, fwd));
          m[0] = vec4(side, 0); m[1] = vec4(up, 0); m[2] = vec4(fwd, 0); m[3] = vec4(f.cam.rel(glm::dvec3(p)), 1);
          VR.draw(M.slat, m * glm::scale(mat4(1), vec3(V)) * glm::translate(mat4(1), vec3(-17, -2, -4)), VoxelLight{(l >> 4) / 15.f, (l & 15) / 15.f});
        }
      }
    }
    if (begun) VR.end();
  }
};
}

void registerStrungLayer() { static bool done = false; if (done) return; done = true; registerRenderLayer([](GameSession&) { return std::make_shared<StrungLayer>(); }); }

} // namespace dq
