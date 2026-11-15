// render/tactile_kit.cpp — see tactile_kit.h
#include "render/tactile_kit.h"
#include "render/content_draw.h"
#include "render/tile_box.h"
#include "assets/item_models.h"
#include "assets/pack_models.h"
#include "core/item_glint.h"
#include "client/app.h"
#include "world/world.h"
#include <map>
#include <tuple>
#include <glm/gtc/matrix_transform.hpp>

namespace dq {
std::string stackModelKey(const ItemStack& s);   // render/pile_layer.cpp

TPose tactileBlockPose(const FrameCtx& f, ivec3 at, int facing) {
  static const float D[4] = {180.f, -90.f, 0.f, 90.f};   // (Tactile's rotationFor: south, east, north, west)
  TPose p; p.m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(at)));
  p.t(0.5f, 0.5f, 0.5f).ry(D[facing & 3]).t(-0.5f, -0.5f, -0.5f);
  return p;
}

void tactileItem(const ItemStack& s, const mat4& pose, VoxelLight L, vec4 tint) {
  if (s.empty()) return;
  const VoxelModel& im = itemModel(stackModelKey(s));
  if (!im.ready()) return;
  mat4 m = pose;
  // Minecraft's FIXED display: a flat item turned to face back the way it's drawn (item/generated "fixed": y 180, scale 1);
  // a block item half a block (block/block "fixed": scale 0.5)
  if (im.kind == VoxelModel::Block) m = glm::scale(m, vec3(0.5f));
  else m = glm::rotate(m, glm::radians(180.f), vec3(0, 1, 0));
  voxelRenderer().draw(im.gl, m * im.matrix(), L, tint, itemGlints(s) ? 1.f : 0.f);
}

void tactilePartDraw(App& app, ContentDraw& cd, const PackModel& M, const mat4& pose, vec2 light) {
  for (const PackModelBox& b : M.boxes) {
    const vec3 a = b.from, z = b.to;
    auto rot = [&](vec3 p) {
      if (b.rotAxis < 0 || b.rotAngle == 0) return p;
      const float r = glm::radians(b.rotAngle), cs = std::cos(r), sn = std::sin(r);
      vec3 d = p - b.rotOrigin;
      if (b.rotAxis == 0) d = vec3(d.x, d.y * cs - d.z * sn, d.y * sn + d.z * cs);
      else if (b.rotAxis == 1) d = vec3(d.x * cs + d.z * sn, d.y, -d.x * sn + d.z * cs);
      else d = vec3(d.x * cs - d.y * sn, d.x * sn + d.y * cs, d.z);
      return b.rotOrigin + d;
    };
    auto W = [&](float x, float y, float zz) { return rot(vec3(x, y, zz)) / 16.f; };
    // corners seen from outside: bottom-left, bottom-right, top-left (as the mesher's emitPackModel)
    const vec3 F[6][3] = {
      {W(z.x, a.y, a.z), W(a.x, a.y, a.z), W(z.x, z.y, a.z)},   // north
      {W(a.x, a.y, z.z), W(z.x, a.y, z.z), W(a.x, z.y, z.z)},   // south
      {W(a.x, a.y, a.z), W(a.x, a.y, z.z), W(a.x, z.y, a.z)},   // west
      {W(z.x, a.y, z.z), W(z.x, a.y, a.z), W(z.x, z.y, z.z)},   // east
      {W(a.x, z.y, z.z), W(z.x, z.y, z.z), W(a.x, z.y, a.z)},   // up
      {W(a.x, a.y, a.z), W(z.x, a.y, a.z), W(a.x, a.y, z.z)}};  // down
    for (int f = 0; f < 6; f++) {
      const PackModelFace& fc = b.face[f];
      if (fc.tile < 0) continue;
      const GLuint tex = tileTexture(app, fc.tile);
      if (!tex) continue;
      const vec3 bl = F[f][0], X = F[f][1] - bl, Y = F[f][2] - bl;
      if (glm::length(X) < 1e-5f || glm::length(Y) < 1e-5f) continue;
      mat4 q(1); q[0] = vec4(X, 0); q[1] = vec4(Y, 0); q[2] = vec4(glm::normalize(glm::cross(X, Y)) * 0.001f, 0); q[3] = vec4(bl + (X + Y) * 0.5f, 1);
      vec4 uv = fc.uv / 16.f;   // (picture: v down; the quad's top is +y)
      cd.picture(tex, pose * q, uv, vec4(1), light, true);
    }
  }
}

VoxelLight tactileLight(World& w, ivec3 at) {
  u8 best = w.getLight(at.x, at.y, at.z);
  static const int D[6][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
  int s = best >> 4, b = best & 15;
  for (auto& d : D) { const u8 l = w.getLight(at.x + d[0], at.y + d[1], at.z + d[2]); s = std::max(s, l >> 4); b = std::max(b, l & 15); }
  return VoxelLight{s / 15.f, b / 15.f};
}

float tactileEase(ivec3 at, int part, bool open, double dt) {
  static std::map<std::tuple<int, int, int, int>, float> v;
  float& a = v[{at.x, at.y, at.z, part}];
  const float goal = open ? 1.f : 0.f, step = (float)dt * 3.f;
  a = a < goal ? std::min(goal, a + step) : std::max(goal, a - step);
  return a;
}

} // namespace dq
