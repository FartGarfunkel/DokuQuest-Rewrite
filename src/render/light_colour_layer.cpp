// render/light_colour_layer.cpp — coloured block light (BTA): every light-giving block has a colour (torches and
// campfires warm, soul fire blue, redstone red, sea lanterns and prismarine cyan, amethyst and portals violet, the
// coloured lamps their dye...; data/blocks.json "lightColor": [r, g, b] sets any block's). Round the camera a volume of
// 64 x 48 x 64 cells holds the light's colour in each: from every light in it, a flood out through everything that
// isn't opaque (as the light itself spreads, 15 steps), each cell taking the colours that reach it weighted by how
// bright they arrive. The shaders (common.glsl lightingAt) tint the block light by it; how bright stays the light
// engine's. The blocks are copied on the main thread and the flood runs on a worker; the result goes up as a 3D
// texture (unit 12). With no coloured light about, the volume is off and block light is the usual warm white.
#include "client/session.h"
#include "client/app.h"
#include "render/frame.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "world/world.h"
#include "world/light_color.h"
#include <atomic>
#include <mutex>

namespace dq {

namespace {
constexpr int VX = 64, VY = 48, VZ = 64;

// a light block's colour (normalised so its brightest channel is 1)
vec3 lightColourOf(BlockId id) {
  // Same colours the light engine emits (world/light_color.cpp). The cache stays on this thread:
  // the chunk worker calls blockLightColour directly and must not share this vector.
  static std::vector<vec3> cache; static std::vector<u8> known;
  if (cache.size() < MAX_BLOCKS) { cache.assign(MAX_BLOCKS, vec3(0)); known.assign(MAX_BLOCKS, 0); }
  if ((unsigned)id >= MAX_BLOCKS) return blockLightColour(id);
  if (known[id]) return cache[id];
  known[id] = 1;
  return cache[id] = blockLightColour(id);
}
bool isWarm(vec3 c) { return glm::distance(c, vec3(1.0f, 0.85f, 0.62f) / 1.0f) < 0.08f; }

struct Job {
  ivec3 lo{0};
  std::vector<BlockId> ids;   // VX*VY*VZ, plus a margin of 15 all round so lights just outside reach in
  ivec3 lo2{0}, n2{0};
};
}

struct LightVolState { std::mutex mu; std::vector<u8> out; bool any = false; ivec3 outLo{0}; std::atomic<bool> busy{false}, ready{false}; };
void floodLight(const Job& J, LightVolState& S);

class LightColourLayer : public RenderLayer {
public:
  const char* name() const override { return "light-colour"; }
  int order() const override { return 990; }
  ~LightColourLayer() override { if (tex) glDeleteTextures(1, &tex); if (dummy) glDeleteTextures(1, &dummy); }

  void update(GameSession& s, double dt) override {
    FrameCtx& f = s.frame;
    if (!dummy) { glGenTextures(1, &dummy); glBindTexture(GL_TEXTURE_3D, dummy); const u8 w[4] = {255, 255, 255, 255}; glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, w); }
    f.lightVol = on && tex ? tex : dummy; f.lightVolOn = on && tex;
    f.lightVolMin = glm::dvec3(have);
    if (!s.player || !s.level || (s.app.settings.extra.contains("colouredLight") && s.app.settings.extra["colouredLight"] == false)) { on = false; f.lightVolOn = false; return; }
    // a finished flood: up it goes
    LightVolState& S = *st;
    if (S.ready.load()) {
      std::lock_guard<std::mutex> lk(S.mu);
      if (!tex) glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_3D, tex);
      glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      for (GLenum w : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTexParameteri(GL_TEXTURE_3D, w, GL_CLAMP_TO_EDGE);
      if (S.any) glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, VX, VY, VZ, 0, GL_RGBA, GL_UNSIGNED_BYTE, S.out.data());
      on = S.any; have = S.outLo; S.ready = false; S.busy = false;
      f.lightVol = on ? tex : dummy; f.lightVolOn = on; f.lightVolMin = glm::dvec3(have);
    }
    // a new flood when the camera has moved on or a while has passed (lights placed or broken)
    t -= dt;
    const ivec3 cam = ivec3(glm::floor(f.cam.pos));
    const ivec3 centre = have + ivec3(VX / 2, VY / 2, VZ / 2);
    const bool moved = std::abs(cam.x - centre.x) > 8 || std::abs(cam.z - centre.z) > 8 || std::abs(cam.y - centre.y) > 8 || !started;
    if (S.busy || (!moved && t > 0)) return;
    t = 1.0;
    started = true;
    auto job = std::make_shared<Job>();
    job->lo = cam - ivec3(VX / 2, VY / 2, VZ / 2);
    job->lo2 = job->lo - ivec3(15); job->n2 = ivec3(VX, VY, VZ) + ivec3(30);
    World& w = s.world();
    // copy the blocks (and see whether any light here is not the usual warm one; if none, no flood at all)
    job->ids.resize((size_t)job->n2.x * job->n2.y * job->n2.z);
    bool coloured = false;
    const Registry& R = reg();
    size_t i = 0;
    for (int z = 0; z < job->n2.z; z++) for (int y = 0; y < job->n2.y; y++) for (int x = 0; x < job->n2.x; x++, i++) {
      const int gy = job->lo2.y + y;
      const BlockId b = gy < 0 || gy >= WORLD_H ? AIR : w.getBlock(job->lo2.x + x, gy, job->lo2.z + z);
      job->ids[i] = b;
      if (b && R.tLight[b] > 0 && !coloured && !isWarm(lightColourOf(b))) coloured = true;
    }
    if (!coloured) { on = false; f.lightVolOn = false; f.lightVol = dummy; have = job->lo; return; }
    for (size_t b = 0; b < R.blocks.size() && b < MAX_BLOCKS; b++) if (R.tLight[b]) lightColourOf((BlockId)b);   // (fill the cache here, not on the worker)
    S.busy = true;
    auto keep = st;
    s.app.pool->submit([keep, job]() { floodLight(*job, *keep); }, -1);
  }

private:
  GLuint tex = 0, dummy = 0;
  bool on = false, started = false;
  std::shared_ptr<LightVolState> st = std::make_shared<LightVolState>();
  ivec3 have{0};
  double t = 0;
};

void floodLight(const Job& J, LightVolState& S) {
    const Registry& R = reg();
    const int NX = J.n2.x, NY = J.n2.y, NZ = J.n2.z;
    auto at = [&](int x, int y, int z) { return ((size_t)z * NY + y) * NX + x; };
    std::vector<vec4> acc((size_t)VX * VY * VZ, vec4(0));   // rgb * weight, weight
    std::vector<u8> seen((size_t)NX * NY * NZ, 0);
    std::vector<ivec3> q, nq;
    const ivec3 off = J.lo - J.lo2;   // where the volume sits inside the copy
    std::vector<size_t> touched;
    for (int z = 0; z < NZ; z++) for (int y = 0; y < NY; y++) for (int x = 0; x < NX; x++) {
      const BlockId b = J.ids[at(x, y, z)];
      if (!b || !R.tLight[b]) continue;
      const vec3 col = lightColourOf(b);
      const int L0 = R.tLight[b];
      // a flood out to 15 steps through what isn't opaque, as the light spreads
      q.clear(); q.push_back(ivec3(x, y, z)); touched.clear();
      seen[at(x, y, z)] = 1; touched.push_back(at(x, y, z));
      for (int lvl = L0; lvl > 0 && !q.empty(); lvl--) {
        nq.clear();
        const float wgt = (float)(lvl * lvl);
        for (const ivec3& p : q) {
          const ivec3 v = p - off;
          if (v.x >= 0 && v.y >= 0 && v.z >= 0 && v.x < VX && v.y < VY && v.z < VZ) { vec4& a = acc[((size_t)v.z * VY + v.y) * VX + v.x]; a += vec4(col * wgt, wgt); }
          static const ivec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
          for (const ivec3& d : N) {
            const ivec3 n = p + d;
            if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= NX || n.y >= NY || n.z >= NZ) continue;
            const size_t k = at(n.x, n.y, n.z);
            if (seen[k]) continue;
            seen[k] = 1; touched.push_back(k);
            const BlockId nb = J.ids[k];
            if (nb && R.tOpaque[nb]) continue;   // (it lights the face, but the light goes no further)
            nq.push_back(n);
          }
        }
        std::swap(q, nq);
      }
      for (size_t k : touched) seen[k] = 0;
    }
    std::vector<u8> o((size_t)VX * VY * VZ * 4, 255);
    bool anyCol = false;
    for (size_t k = 0; k < acc.size(); k++) {
      const vec4& a = acc[k];
      vec3 c = a.w > 0 ? vec3(a) / a.w : vec3(1.0f, 0.85f, 0.62f);
      const float m = std::max({c.x, c.y, c.z, 1e-3f}); c /= m;
      if (!isWarm(c)) anyCol = true;
      o[k * 4] = (u8)(c.x * 255); o[k * 4 + 1] = (u8)(c.y * 255); o[k * 4 + 2] = (u8)(c.z * 255); o[k * 4 + 3] = 255;
    }
    std::lock_guard<std::mutex> lk(S.mu);
    S.out.swap(o); S.any = anyCol; S.outLo = J.lo;
    S.ready = true;
}

vec3 lightColourOfBlock(BlockId id) { return lightColourOf(id); }

void registerLightColourLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<LightColourLayer>(); });
}

} // namespace dq
