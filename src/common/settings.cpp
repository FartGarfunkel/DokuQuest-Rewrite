#include "common/settings.h"

namespace dq {

const char* rendererName(RendererKind k) {
  switch (k) { case RendererKind::Legacy: return "legacy"; case RendererKind::Modern: return "modern"; case RendererKind::Vulkan: return "vulkan"; }
  return "modern";
}

#define DQ_SETTINGS_FIELDS(X) \
  X(renderDist) X(sens) X(padSens) X(fov) X(scale) X(fancyLeaves) X(plantDetail) X(wavingPlants) X(autoJump) \
  X(volume) X(mobVol) X(music) X(musicFreq) X(colorFilter) X(captions) X(ambience) X(invertY) X(showFps) X(fpBody) X(viewBob) X(brightness) X(colourStyle) X(saturation) X(contrast) X(split) \
  X(particles) X(guiScale) X(toggleSneak) X(toggleSprint) X(screenShake) X(hurtTilt) X(fpsLimit) X(padLayout) \
  X(heldSway) X(clouds) X(portalViews) X(farTerrain) X(preset) X(microVoxels) X(microVoxelDist) X(waterQuality) \
  X(shadows) X(weatherParticles) X(weatherDist) X(weatherSurfaceFx) X(groundFog) X(heightFog) X(vsync) X(fullscreen) X(windowW) X(windowH) X(workerThreads) X(resourcePack) X(shaderPack) X(gpu) \
  X(playerName) X(skin) X(autoUpdate) X(pauseOnLostFocus) X(muteOnLostFocus) X(rawMouse) X(autosaveSec) \
  X(keyBinds) X(padBinds) X(padMaps)

json Settings::toJson() const {
  json j = extra;
#define X(f) j[#f] = f;
  DQ_SETTINGS_FIELDS(X)
#undef X
  j["renderer"] = rendererName(renderer);
  return j;
}
void Settings::fromJson(const json& j) {
  if (!j.is_object()) return;
#define X(f) if (j.contains(#f)) { try { j.at(#f).get_to(f); } catch (...) {} }
  DQ_SETTINGS_FIELDS(X)
#undef X
  if (j.contains("renderer") && j["renderer"].is_string()) {
    std::string r = j["renderer"];
    renderer = r == "legacy" ? RendererKind::Legacy : r == "vulkan" ? RendererKind::Vulkan : RendererKind::Modern;
  }
  static const char* known[] = {
#define X(f) #f,
    DQ_SETTINGS_FIELDS(X)
#undef X
    "renderer"};
  extra = json::object();
  for (auto& [k, v] : j.items()) if (std::find_if(std::begin(known), std::end(known), [&](const char* s) { return k == s; }) == std::end(known)) extra[k] = v;
  renderDist = clampv(renderDist, 2, 32);
  farTerrain = clampv(farTerrain, 0, 16384);
  autosaveSec = clampv(autosaveSec, 0, 115);
  autosaveSec = (autosaveSec / 5) * 5;   // Off, 5, 10, ... 115 seconds
  brightness = clampv(brightness, 0.f, 1.f);
  saturation = clampv(saturation, 0.f, 1.5f);
  contrast = clampv(contrast, 0.5f, 1.5f);
  weatherParticles = clampv(weatherParticles, 0, 3000);
  weatherDist = clampv(weatherDist, 12, 32);
}
Settings Settings::load() {
  Settings s;
  if (auto j = readJson(userDir() / "settings.json")) s.fromJson(*j);
  return s;
}
void Settings::save() const { writeJson(userDir() / "settings.json", toJson()); }

// The colour styles (Options → Video → Colour Style). Vibrant is the picture as drawn; Classic mutes it toward the
// flatter, duller look of Minecraft Alpha/Beta (less colour, softer contrast, so blacks are not as deep on an OLED).
void Settings::applyColourStyle(const std::string& style) {
  colourStyle = style;
  if (style == "vibrant") { saturation = 1.0f; contrast = 1.0f; }
  else if (style == "natural") { saturation = 0.85f; contrast = 0.94f; }
  else if (style == "classic") { saturation = 0.68f; contrast = 0.86f; }
}

void Settings::applyPreset(const std::string& p) {
  preset = p;
  // the weather (Particles, Distance, Splashes, Ground Fog, Height Fog) follows the preset too
  if (p == "potato") { weatherParticles = 600; weatherDist = 12; weatherSurfaceFx = false; groundFog = false; heightFog = true; }
  else if (p == "low") { weatherParticles = 600; weatherDist = 14; weatherSurfaceFx = true; groundFog = true; heightFog = true; }
  else if (p == "medium") { weatherParticles = 1500; weatherDist = 16; weatherSurfaceFx = true; groundFog = true; heightFog = true; }
  else if (p == "high") { weatherParticles = 3000; weatherDist = 20; weatherSurfaceFx = true; groundFog = true; heightFog = true; }
  else if (p == "ultra") { weatherParticles = 3000; weatherDist = 24; weatherSurfaceFx = true; groundFog = true; heightFog = true; }
  if (p == "potato") { renderer = RendererKind::Legacy; renderDist = 4; farTerrain = 512; microVoxels = 0; waterQuality = 0; shadows = 0; particles = 1; fancyLeaves = false; plantDetail = "near"; scale = 0.75f; clouds = false; }
  else if (p == "low") { renderDist = 6; farTerrain = 1024; microVoxels = 0; waterQuality = 1; shadows = 0; particles = 1; fancyLeaves = false; scale = 1; }
  else if (p == "medium") { renderDist = 8; farTerrain = 2048; microVoxels = 0; microVoxelDist = 32; waterQuality = 1; shadows = 0; particles = 2; fancyLeaves = true; scale = 1; }
  else if (p == "high") { renderDist = 12; farTerrain = 4096; microVoxels = 0; microVoxelDist = 48; waterQuality = 2; shadows = 1; particles = 2; fancyLeaves = true; scale = 1; }
  else if (p == "ultra") { renderDist = 16; farTerrain = 8192; microVoxels = 0; microVoxelDist = 96; waterQuality = 2; shadows = 1; particles = 2; fancyLeaves = true; scale = 1; }
}

} // namespace dq
