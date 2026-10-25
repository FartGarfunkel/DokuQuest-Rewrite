// ui/icon_glint.cpp — the enchantment shimmer over an item icon (styles.css .slot .glint: a violet sheen with a
// bright band sliding across, screen-blended, masked by the icon's shape). Drawn as an additive grid of quads
// textured with the icon itself, so it only lands where the icon has pixels.
#include "ui/theme.h"
#include "assets/icons.h"
#include "render/gl.h"
#include <imgui.h>
#include <cmath>

namespace dq {
namespace ui {

namespace {
void additiveOn(const ImDrawList*, const ImDrawCmd*) { glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE); }
}

void drawGlint(ImDrawList* dl, const std::string& key, ImVec2 p, float s, float alpha) {
  IconRef r = itemIcons().get(key);
  if (!r.valid() || alpha <= 0) return;
  float t = (float)ImGui::GetTime();
  float cpos = -0.3f + 1.6f * (t / 2.6f - std::floor(t / 2.6f));   // the bright band's place along the diagonal
  const int N = 8;
  dl->AddCallback(additiveOn, nullptr);
  dl->PushTextureID((ImTextureID)(intptr_t)r.tex);
  dl->PrimReserve(N * N * 6, (N + 1) * (N + 1));
  ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
  for (int j = 0; j <= N; j++) for (int i = 0; i <= N; i++) {
    float fx = (float)i / N, fy = (float)j / N;
    float d = (fx + fy * 0.6f) / 1.6f * 1.2f - 0.1f;
    float k = std::max(0.f, 1 - std::fabs(d - cpos) / 0.16f); k = k * k * (3 - 2 * k);
    float a = (0.18f + 0.67f * k) * alpha;
    int cr = (int)(150 + 70 * k), cg = (int)(80 + 90 * k);
    dl->PrimWriteVtx(ImVec2(p.x + fx * s, p.y + fy * s), ImVec2(r.uv0.x + (r.uv1.x - r.uv0.x) * fx, r.uv0.y + (r.uv1.y - r.uv0.y) * fy), IM_COL32(cr, cg, 255, (int)(255 * a)));
  }
  for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
    ImDrawIdx a = base + (ImDrawIdx)(j * (N + 1) + i), b = a + 1, c = a + (ImDrawIdx)(N + 1), e = c + 1;
    dl->PrimWriteIdx(a); dl->PrimWriteIdx(b); dl->PrimWriteIdx(e);
    dl->PrimWriteIdx(a); dl->PrimWriteIdx(e); dl->PrimWriteIdx(c);
  }
  dl->PopTextureID();
  dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

} // namespace ui
} // namespace dq
