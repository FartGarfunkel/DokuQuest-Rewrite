// render/colour_filter_layer.cpp — the colour-blind filters (assets/shaders/colour_filter.glsl): last of all the world's
// drawing (after the hand), the picture is copied out and drawn back through the filter. Settings::colorFilter.
// Photo mode's shaders go through the same pass (client/photo_mode.h: none, black & white, sepia, vivid, faded film,
// warm, cool, retro, dreamy, vignette), at its intensity.
#include "client/session.h"
#include "client/app.h"
#include "render/gl.h"
#include "client/photo_mode.h"

namespace dq {

class ColourFilterLayer : public RenderLayer {
public:
  const char* name() const override { return "colour-filter"; }
  int order() const override { return 5000; }
  void drawOverlay(GameSession& s, const FrameCtx& f) override {
    int kind = s.app.settings.colorFilter;
    if (kind < 1 || kind > 3) kind = 0;
    const PhotoMode& ph = photoMode();
    const int photo = ph.active && !s.split() ? ph.shader : 0;
    const Settings& st = s.app.settings;
    const float sat = std::clamp(st.saturation, 0.f, 1.5f), con = std::clamp(st.contrast, 0.5f, 1.5f);
    const bool grade = std::fabs(sat - 1.f) > 0.005f || std::fabs(con - 1.f) > 0.005f;   // Vibrant: no pass at all
    if (!kind && !photo && !grade) return;
    if (!tried) { tried = true; sh = std::make_unique<Shader>(); if (!sh->load("colour_filter.glsl", "colour_filter.glsl")) sh.reset(); glGenVertexArrays(1, &vao); }
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
    sh->use(); sh->set("uTex", 0); sh->set("uKind", kind); sh->set("uPhoto", photo); sh->set("uAmt", ph.intensity);
    sh->set("uGrade", grade ? vec2(sat, con) : vec2(1.f));
    glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
  }
private:
  bool tried = false; std::unique_ptr<Shader> sh; GLuint vao = 0, tex = 0; int tw = 0, th = 0;
};

void registerColourFilterLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<ColourFilterLayer>(); }); }

} // namespace dq
