// ui/char_preview.cpp — a voxel character rendered offscreen for the creator and the skin editor.
#include "ui/char_preview.h"
#include "client/app.h"
#include "core/registry.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace dq {

CharPreview::~CharPreview() {
  if (!glDeleteFramebuffers) return;
  if (fbo) glDeleteFramebuffers(1, &fbo);
  if (tex) glDeleteTextures(1, &tex);
  if (depth) glDeleteRenderbuffers(1, &depth);
}

float padAxis(int k) { const ImGuiKeyData* d = ImGui::GetKeyData((ImGuiKey)k); return d ? d->AnalogValue : 0.f; }

void previewImage(GLuint tex, float w, float h) {
  if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
  else ImGui::Dummy(ImVec2(w, h));
}

GLuint CharPreview::render(const CharacterSpec& spec, int w, int h, const CharPreviewOpts& o) {
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

  const CharModel& M = charModel(spec);
  const TileSet& ts = app().tiles;
  // the camera (skineditor.js mvp): looking at the middle of the figure from dist model pixels away
  float sy = M.stature.x, sx = M.stature.y;
  mat4 P = glm::perspective(0.62f, (float)w / h, 0.05f, 40.f);
  mat4 V = glm::translate(mat4(1), vec3(0, 0, -o.dist * CHAR_PX));
  V = glm::rotate(V, o.elev, vec3(1, 0, 0));
  V = glm::rotate(V, o.yaw, vec3(0, 1, 0));
  V = glm::translate(V, vec3(0, -16 * CHAR_PX * sy, 0));
  mat4 VP = P * V;
  viewProj = VP * glm::scale(mat4(1), vec3(sx, sy, sx)) * glm::scale(mat4(1), vec3(CHAR_PX));
  // the pose: standing about (or walking on the spot), arms easy
  CharPose ps;
  ps.time = o.time; ps.still = o.still;
  if (o.walk) { ps.walk = o.time * 4.3f * 1.6f * 1.4f; ps.walkAmt = 1; }
  ps.capeA = o.walk ? 0.6f : 0.12f; ps.capeSway = std::sin(o.time * 2.3f) * 0.03f;
  std::string cape = o.cape.empty() ? spec.cape : o.cape;
  const GearLook* L = M.look ? &*M.look : nullptr;
  bool cloak = L && L->cloak && cape.empty();
  CharRig R;
  charRig(M, ps, mat4(1), R, cloak, false);
  VoxelRenderer& VR = voxelRenderer();
  VR.beginNeutral(VP, vec3(0.35f, 0.9f, 0.75f));
  VoxelLight light{1, 0};
  static const int SKINPART[CP_COUNT] = {0, 1, 2, 2, 3, 3, 4, 4, 5, 5};
  std::string AR[4];
  for (int i = 0; i < 4 && i < (int)o.armor.size(); i++) AR[i] = o.armor[i];
  for (int i = 0; i < CP_COUNT; i++) {
    vec4 tint = o.isolate >= 0 && SKINPART[i] != o.isolate ? vec4(0.16f, 0.15f, 0.14f, 0.75f) : vec4(0);
    mat4 m = R.part[i] * R.scale[i];
    if (M.base[i].ready()) VR.draw(M.base[i].gl, m * M.base[i].local(), light, tint);
    bool armored = (i == CP_HEAD && !AR[0].empty()) || (i != CP_HEAD && i < CP_LEGR_U && !AR[1].empty()) || (i >= CP_LEGR_U && !AR[2].empty());
    if (o.overlay && !armored && M.ovl[i].ready() && (o.isolate < 0 || SKINPART[i] == o.isolate)) VR.draw(M.ovl[i].gl, m * M.ovl[i].local(), light, tint);
  }
  if (o.gear && L && o.isolate < 0) {
    for (auto& [part, mesh] : M.gear(!AR[0].empty(), !AR[1].empty(), &ts)) {
      auto fm = R.gear.find(part);
      if (fm != R.gear.end() && mesh.ready()) VR.draw(mesh.gl, fm->second * mesh.local(), light);
    }
  }
  static const std::map<std::string, int> PART = {{"head", CP_HEAD}, {"body", CP_BODY}, {"armR_u", CP_ARMR_U}, {"armR_d", CP_ARMR_D}, {"armL_u", CP_ARML_U},
                                                  {"armL_d", CP_ARML_D}, {"legR_u", CP_LEGR_U}, {"legR_d", CP_LEGR_D}, {"legL_u", CP_LEGL_U}, {"legL_d", CP_LEGL_D}};
  for (int slot = 0; slot < 4; slot++) {
    if (AR[slot].empty() || !reg().item(AR[slot])) continue;
    const ArmorSet& A = armorMeshes(AR[slot], "", slot, M.slim, L != nullptr, ts);
    for (auto& [pn, list] : A.byPart) { int pi = PART.at(pn); for (const CharMesh* cm : list) VR.draw(cm->gl, R.part[pi] * R.scale[pi] * cm->local(), light); }
  }
  if (!cape.empty() && o.isolate < 0) if (const CharMesh* cm = capeMesh(cape, 0, ts)) {
    mat4 m = glm::translate(R.body, vec3(0, 12, 2.1f));
    m = glm::rotate(m, -ps.capeA, vec3(1, 0, 0)); m = glm::rotate(m, ps.capeSway, vec3(0, 0, 1));
    VR.draw(cm->gl, m * cm->local(), light);
  }
  VR.end();
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  return tex;
}

} // namespace dq
