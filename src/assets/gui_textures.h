// assets/gui_textures.h — the active resource pack's GUI pictures (buttons, hotbar, hearts, container
// backgrounds...) as GL textures + uv rects for the UI. packs.js used the pack's button pictures
// (1.20.2+ sprites or the older widgets sheet); this generalises it to any GUI picture.
// Main (GL) thread only; textures are created on first use and dropped when the pack changes.
#pragma once
#include "common/common.h"

namespace dq {

struct GuiSprite {
  unsigned tex = 0;            // GL texture (ImTextureID = (void*)(uintptr_t)tex)
  vec2 uv0{0}, uv1{1};
  int w = 0, h = 0;            // size in the pack's pixels
  float scale = 1;             // pack pixels per vanilla pixel (2 for a 32x pack's 512-wide sheets)
  bool valid() const { return tex != 0; }
  void* id() const { return (void*)(uintptr_t)tex; }
};

// Minecraft's sprite names (1.20.2+): "widget/button", "widget/button_highlighted", "widget/button_disabled",
// "hud/hotbar", "hud/hotbar_selection", "hud/crosshair", "hud/heart/container", "hud/heart/full",
// "hud/heart/half", "hud/armor_empty|half|full", "hud/air", "hud/air_bursting",
// "hud/experience_bar_background|progress", "container/inventory", "container/crafting_table",
// "container/furnace", "container/generic_54", "container/dispenser", "container/brewing_stand",
// "container/enchanting_table", "container/anvil", "container/smoker", "container/blast_furnace".
// Looks in textures/gui/sprites/<name>.png, then cuts it from the older sheets. Invalid if the pack lacks it.
GuiSprite guiSprite(const std::string& name);
// any picture under the pack's textures/ (e.g. "gui/container/inventory.png"), whole or a rect given in
// vanilla pixels of a sheet `sheetW` wide (scaled to the pack's resolution)
GuiSprite guiTexture(const std::string& rel, ivec4 rect = ivec4(0), int sheetW = 256);
void resetGuiTextures();       // the pack changed (applyResourcePack calls it)

} // namespace dq
