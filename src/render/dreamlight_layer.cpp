// render/dreamlight_layer.cpp — Horizon-style dreamlike light (assets/shaders/dreamlight.glsl): after the world and the
// hand are drawn, the picture is copied out and drawn back through the shader. Options → Graphics → Lighting
// (Settings::extra "dreamLight": 0 classic, 1 dreamlike, 2 strong; dreamlike unless set).
#include "client/session.h"
#include "client/app.h"
#include "render/gl.h"
#include "render/sky.h"

namespace dq {

class DreamlightLayer : public RenderLayer {
public:
  const char* name() const override { return "dreamlight"; }
  int order() const override { return 4900; }   // after the hand, before the colour-blind filter (5000)
  void drawOverlay(GameSession& s, const FrameCtx& f) override {
    const int mode = s.app.settings.extra.value("dreamLight", 1);
    if (mode < 1) return;
    if (!tried) { tried = true; sh = std::make_unique<Shader>(); if (!sh->load("dreamlight.glsl", "dreamlight.glsl")) sh.reset(); glGenVertexArrays(1, &vao); }
    if (!sh) return;
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return;
    if (!tex || tw != vp[2] || th != vp[3]) {
      if (tex) glDeleteTextures(1, &tex);
      glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vp[2], vp[3], 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      tw = vp[2]; th = vp[3];
    }
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    const float sy = f.sunDir.y;
    const float day = f.dim == DIM_OVERWORLD ? smoothstepf(-0.1f, 0.1f, sy) : 0.f;
    const float low = 1 - smoothstepf(0.f, 0.35f, std::fabs(sy));
    sh->use(); sh->set("uTex", 0); sh->set("uRes", vec2((float)vp[2], (float)vp[3]));
    sh->set("uStrength", mode >= 2 ? 1.6f : 1.f); sh->set("uDaylight", day); sh->set("uSunLow", low);
    sh->set("uRain", f.sky ? f.sky->raining : 0.f); sh->set("uLift", f.fogColor);
    glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); glBindVertexArray(0);
    glEnable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST);
  }
private:
  bool tried = false; std::unique_ptr<Shader> sh; GLuint vao = 0, tex = 0; int tw = 0, th = 0;
};

void registerDreamlightLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<DreamlightLayer>(); }); }

} // namespace dq
