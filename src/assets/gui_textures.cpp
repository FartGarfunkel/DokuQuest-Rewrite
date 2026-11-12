// assets/gui_textures.cpp — pack GUI pictures for the UI.
#include "assets/gui_textures.h"
#include "assets/pack_fs.h"
#include "assets/tileset.h"
#include "render/gl.h"
#include <map>

namespace dq {

namespace {
struct Pic { GLuint tex = 0; int w = 0, h = 0; };
std::map<std::string, Pic> g_pics;       // texture-relative path -> texture ("" tex = missing)
struct Cut { const char* sheet; int x, y, w, h, sheetW; };
const std::map<std::string, Cut> LEGACY = {
  {"widget/button", {"gui/widgets.png", 0, 66, 200, 20, 256}},
  {"widget/button_highlighted", {"gui/widgets.png", 0, 86, 200, 20, 256}},
  {"widget/button_disabled", {"gui/widgets.png", 0, 46, 200, 20, 256}},
  {"hud/hotbar", {"gui/widgets.png", 0, 0, 182, 22, 256}},
  {"hud/hotbar_selection", {"gui/widgets.png", 0, 22, 24, 24, 256}},
  {"hud/crosshair", {"gui/icons.png", 0, 0, 15, 15, 256}},
  {"hud/heart/container", {"gui/icons.png", 16, 0, 9, 9, 256}},
  {"hud/heart/full", {"gui/icons.png", 52, 0, 9, 9, 256}},
  {"hud/heart/half", {"gui/icons.png", 61, 0, 9, 9, 256}},
  {"hud/armor_empty", {"gui/icons.png", 16, 9, 9, 9, 256}},
  {"hud/armor_half", {"gui/icons.png", 25, 9, 9, 9, 256}},
  {"hud/armor_full", {"gui/icons.png", 34, 9, 9, 9, 256}},
  {"hud/air", {"gui/icons.png", 16, 18, 9, 9, 256}},
  {"hud/air_bursting", {"gui/icons.png", 25, 18, 9, 9, 256}},
  {"hud/experience_bar_background", {"gui/icons.png", 0, 64, 182, 5, 256}},
  {"hud/experience_bar_progress", {"gui/icons.png", 0, 69, 182, 5, 256}},
  {"container/inventory", {"gui/container/inventory.png", 0, 0, 176, 166, 256}},
  {"container/crafting_table", {"gui/container/crafting_table.png", 0, 0, 176, 166, 256}},
  {"container/furnace", {"gui/container/furnace.png", 0, 0, 176, 166, 256}},
  {"container/smoker", {"gui/container/smoker.png", 0, 0, 176, 166, 256}},
  {"container/blast_furnace", {"gui/container/blast_furnace.png", 0, 0, 176, 166, 256}},
  {"container/generic_54", {"gui/container/generic_54.png", 0, 0, 176, 222, 256}},
  {"container/dispenser", {"gui/container/dispenser.png", 0, 0, 176, 166, 256}},
  {"container/brewing_stand", {"gui/container/brewing_stand.png", 0, 0, 176, 166, 256}},
  {"container/enchanting_table", {"gui/container/enchanting_table.png", 0, 0, 176, 166, 256}},
  {"container/anvil", {"gui/container/anvil.png", 0, 0, 176, 166, 256}},
};

const Pic& pic(const std::string& rel) {
  auto it = g_pics.find(rel);
  if (it != g_pics.end()) return it->second;
  Pic p;
  PackFS* P = activePack();
  if (P && P->hasTextures()) if (auto b = P->read(P->texRoot + rel)) {
    std::vector<u8> px;
    if (loadPngMem(b->data(), b->size(), p.w, p.h, px)) p.tex = makeTexture2D(p.w, p.h, px.data(), true, false, false);
  }
  return g_pics[rel] = p;
}
}

void resetGuiTextures() {
  for (auto& [k, p] : g_pics) if (p.tex) glDeleteTextures(1, &p.tex);
  g_pics.clear();
}

GuiSprite guiTexture(const std::string& rel, ivec4 rect, int sheetW) {
  const Pic& p = pic(rel);
  GuiSprite s;
  if (!p.tex) return s;
  s.tex = p.tex;
  float k = (float)p.w / sheetW;
  s.scale = k;
  if (rect.z <= 0 || rect.w <= 0) { s.w = p.w; s.h = p.h; return s; }
  float x = rect.x * k, y = rect.y * k, w = rect.z * k, h = rect.w * k;
  s.uv0 = vec2(x / p.w, y / p.h); s.uv1 = vec2((x + w) / p.w, (y + h) / p.h);
  s.w = (int)std::lround(w); s.h = (int)std::lround(h);
  return s;
}

GuiSprite guiSprite(const std::string& name) {
  GuiSprite s = guiTexture("gui/sprites/" + name + ".png");
  if (s.valid()) { s.scale = 1; return s; }
  auto it = LEGACY.find(name);
  if (it == LEGACY.end()) return {};
  const Cut& c = it->second;
  return guiTexture(c.sheet, ivec4(c.x, c.y, c.w, c.h), c.sheetW);
}

} // namespace dq
