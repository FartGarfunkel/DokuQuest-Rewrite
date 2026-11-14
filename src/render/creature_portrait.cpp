// render/creature_portrait.cpp — see creature_portrait.h
#include "render/creature_portrait.h"
#include "render/creature_models.h"
#include "sim/creature.h"
#include "sim/creature_rig.h"
#include "assets/voxel.h"
#include <memory>
#include <unordered_map>
#include <cmath>

namespace dq {

CreaturePortrait::~CreaturePortrait() {
  if (fbo) glDeleteFramebuffers(1, &fbo);
  if (tex) glDeleteTextures(1, &tex);
  if (depth) glDeleteRenderbuffers(1, &depth);
}

GLuint CreaturePortrait::render(const std::string& type, int w, int h, float yaw, float time, bool silhouette) {
  if (!creatureType(type)) return 0;
  // one creature of each kind kept to pose (never in a world: only its rig is used)
  static std::unordered_map<std::string, std::unique_ptr<Creature>> models;
  auto& cp = models[type];
  if (!cp) cp = std::make_unique<Creature>(type);
  Creature& m = *cp;
  const RigModel* RM = rigModel(m.modelKey());
  const CreatureModelGL* CM = creatureModel(m.modelKey());
  if (!RM || !CM) return 0;
  w = std::max(16, w); h = std::max(16, h);
  if (w != tw || h != th || !fbo) {
    if (!fbo) glGenFramebuffers(1, &fbo);
    if (!tex) glGenTextures(1, &tex);
    if (!depth) glGenRenderbuffers(1, &depth);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    tw = w; th = h;
  }
  GLint prevFbo = 0, vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_VIEWPORT, vp);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo); return 0; }
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); glDisable(GL_BLEND);
  // posed standing, its idle going on (age), facing the camera turned by yaw
  m.pos = vec3(0); m.yaw = yaw; m.age = time; m.onGround = true; m.walkAmt = 0;
  rigPose(m, *RM, creatureBase(m, true));
  // the whole of it in view: its parts' boxes once posed
  vec3 lo(1e9f), hi(-1e9f);
  for (size_t i = 0; i < RM->parts.size() && i < m.mats.size(); i++) {
    if (m.mats[i][3][3] == 0 || CM->parts[i]->hidden) continue;
    const RigPart& p = RM->parts[i];
    for (int c = 0; c < 8; c++) {
      const vec3 q((c & 1) ? p.max.x : p.min.x, (c & 2) ? p.max.y : p.min.y, (c & 4) ? p.max.z : p.min.z);
      const vec3 wq = vec3(m.mats[i] * vec4(q, 1));
      lo = glm::min(lo, wq); hi = glm::max(hi, wq);
    }
  }
  if (lo.x > hi.x) { lo = vec3(-0.5f, 0, -0.5f); hi = vec3(0.5f, 1, 0.5f); }
  const vec3 ctr = (lo + hi) * 0.5f;
  const float rad = std::max(0.3f, glm::length(hi - lo) * 0.5f);
  const float fov = 0.6f, dist = rad / std::sin(fov * 0.5f) * 1.05f;
  const mat4 P = glm::perspective(fov, (float)w / h, 0.05f, dist * 4 + 10);
  mat4 V = glm::translate(mat4(1), vec3(0, 0, -dist));
  V = glm::rotate(V, 0.28f, vec3(1, 0, 0));   // (a little from above)
  V = glm::translate(V, -ctr);
  VoxelRenderer& VR = voxelRenderer();
  VR.beginNeutral(P * V, vec3(0.35f, 0.9f, 0.75f));
  const vec4 tint = silhouette ? vec4(0.05f, 0.04f, 0.04f, 0.97f) : vec4(0);
  for (size_t i = 0; i < RM->parts.size(); i++) {
    const RigPart& p = RM->parts[i];
    const CreaturePartMesh& pm = *CM->parts[i];
    if (pm.hidden || !rigPartPosed(m, (int)i) || !rigPartShown(m, p) || p.alpha < 1) continue;
    VR.draw(pm.gl, m.mats[i] * pm.local, VoxelLight{1, p.glow ? 1.f : 0.f}, tint);
  }
  VR.end();
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  return tex;
}

} // namespace dq
