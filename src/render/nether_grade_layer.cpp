// render/nether_grade_layer.cpp — Better than Adventure 8.0's Nether colour correction (its post.fsh grade for hot
// worlds): the finished picture of the Nether is read back and drawn again a little darker and warmer, red x1.6 and
// green x1.15 at the default strength. Options → Video → Post-Processing → Nether Color Correction (0..100 %, BTA's
// default 50 %). The Old World, cooler, gets less of it and a little less colour, as in BTA.
#include "client/session.h"
#include "client/app.h"
#include "render/gl.h"
#include "gen/nether_bta.h"
#include "sim/level.h"

namespace dq {
double netherExactTemperature(i32 seed, int x, int z);   // gen/nether_exact.cpp

class NetherGradeLayer : public RenderLayer {
public:
  const char* name() const override { return "nether-grade"; }
  int order() const override { return 4900; }   // before the colour-blind filter
  // BTA's heat haze wants the world's depth; the held item clears it before the overlays, so it is kept here first
  void drawTranslucent(GameSession& s, const FrameCtx& f) override {
    haveDepth = false;
    if (f.dim != DIM_NETHER || f.cam.realm != REALM_MAIN || !s.level || f.underwater || depthBroken) return;
    if (std::clamp(s.app.settings.extra.value("heatHaze", 0.5f), 0.f, 1.f) <= 0.001f) return;
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return;
    while (glGetError() != GL_NO_ERROR) {}
    if (!dtex || dw != vp[2] || dh != vp[3]) {
      if (dtex) glDeleteTextures(1, &dtex);
      glGenTextures(1, &dtex); glBindTexture(GL_TEXTURE_2D, dtex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, vp[2], vp[3], 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      dw = vp[2]; dh = vp[3];
    }
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, dtex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    if (glGetError() != GL_NO_ERROR) { depthBroken = true; return; }   // a multisampled or odd depth buffer: no haze
    haveDepth = true;
  }
  void drawOverlay(GameSession& s, const FrameCtx& f) override {
    // in and out of the Nether the grade fades over 0.7 s with the portal's fog crossfade (render/sky.cpp), never a jump
    const bool in = f.dim == DIM_NETHER && f.cam.realm == REALM_MAIN && s.level && !f.underwater;
    const float dt = lastT > 0 ? (float)std::clamp(f.time - lastT, 0.0, 0.25) : 0.f; lastT = f.time;
    presence = in ? std::min(1.f, presence + dt / 0.7f) : std::max(0.f, presence - dt / 0.7f);
    if (!s.level || presence <= 0.001f || f.cam.realm != REALM_MAIN) return;
    const float slider = std::clamp(s.app.settings.extra.value("colorCorrectionNether", 0.5f), 0.f, 1.f) * presence * presence * (3 - 2 * presence);
    const float haze = haveDepth ? std::clamp(s.app.settings.extra.value("heatHaze", 0.5f), 0.f, 1.f) : 0.f;
    if (slider <= 0.001f && haze <= 0.001f) return;
    // BTA scales the grade by the temperature where you stand (scale 2t below t = 0.5)
    // the temperature where you stand, blended across a land's edge (BTA reads the block temperature, which is smooth)
    if (in) {
      u8 a = NB_CRAG, b = NB_CRAG; float m = 1;
      netherLandAt(s.level->info.seed, s.level->info.genVersion, (int)std::floor(f.cam.pos.x), (int)std::floor(f.cam.pos.y), (int)std::floor(f.cam.pos.z), a, b, m);
      auto tOf = [](u8 l) { return l == NB_OLDWORLD || l == NB_OLD_DESERT ? 0.26f : 1.f; };
      const float want = s.level->info.genVersion >= 7   // BTA's own temperature (exact), else the land's
        ? (float)netherExactTemperature(s.level->info.seed, (int)std::floor(f.cam.pos.x), (int)std::floor(f.cam.pos.z))
        : tOf(a) * m + tOf(b) * (1 - m);
      tempE = tempE < 0 ? want : tempE + (want - tempE) * std::min(1.f, dt * 3.f);   // BTA: 5 % a tick
    }
    const float temp = tempE < 0 ? 1.f : tempE;
    const float scale = temp < 0.5f ? 2 * temp : 1.f;
    const float target = scale;
    eased += (target - eased) * std::min(1.f, dt);   // BTA eases its grade 5 % a tick (about 63 % a second)
    if (!tried) { tried = true; sh = std::make_unique<Shader>(); if (!sh->load("nether_grade.glsl", "nether_grade.glsl")) sh.reset(); glGenVertexArrays(1, &vao); }
    if (!sh) return;
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return;
    if (!tex || tw != vp[2] || th != vp[3]) {
      if (tex) glDeleteTextures(1, &tex);
      glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vp[2], vp[3], 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      tw = vp[2]; th = vp[3];
    }
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    // the grade at this strength: each change times the slider, each colour mod 1 - (1 - mod) x slider
    const float k = eased * slider;
    const float sat = temp < 0.5f ? 0.125f * k - 0.5f * (1 - 2 * temp) * slider : 0.125f * k;
    sh->use(); sh->set("uTex", 0);
    sh->set("uBrightness", -0.05f * slider); sh->set("uContrast", -0.05f * slider); sh->set("uExposure", -0.125f * slider);
    sh->set("uSaturation", sat);
    sh->set("uRgbMod", vec3(1 + 1.2f * k, 1 + 0.3f * k, 1));
    // the heat haze: BTA's amount = scale * slider * 3, ramping in with distance (13 to 52 blocks at 12 chunks)
    sh->set("uHaze", haze > 0.001f ? scale * haze * 3.f : 0.f); sh->set("uTime", (float)std::fmod(f.time, 1000.0));
    sh->set("uDepth", 1); sh->set("uNearFar", vec2(f.cam.nearZ, f.cam.farZ));
    sh->set("uFar", std::max(48.f, s.app.settings.renderDist * 16.f));
    if (haze > 0.001f) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, dtex); glActiveTexture(GL_TEXTURE0); }
    glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); glBindVertexArray(0);
    glEnable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST);
  }
private:
  bool tried = false; std::unique_ptr<Shader> sh; GLuint vao = 0, tex = 0; int tw = 0, th = 0;
  float eased = 1;
  GLuint dtex = 0; int dw = 0, dh = 0; bool haveDepth = false, depthBroken = false;
  double lastT = -1; float presence = 0, tempE = -1;
};

void registerNetherGradeLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<NetherGradeLayer>(); }); }

} // namespace dq
