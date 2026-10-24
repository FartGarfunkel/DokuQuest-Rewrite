#include "common/settings.h"

namespace dq {

const char* rendererName(RendererKind k) {
  switch (k) { case RendererKind::Legacy: return "legacy"; case RendererKind::Modern: return "modern"; case RendererKind::Vulkan: return "vulkan"; }
  return "modern";
}

#define DQ_SETTINGS_FIELDS(X) \
  X(renderDist) X(sens) X(padSens) X(fov) X(scale) X(fancyLeaves) X(plantDetail) X(wavingPlants) X(autoJump) \
  X(volume) X(mobVol) X(music) X(ambience) X(invertY) X(showFps) X(fpBody) X(viewBob) X(brightness) X(split) \
  X(particles) X(guiScale) X(toggleSneak) X(toggleSprint) X(screenShake) X(hurtTilt) X(fpsLimit) X(padLayout) \
  X(heldSway) X(clouds) X(portalViews) X(farTerrain) X(preset) X(microVoxels) X(microVoxelDist) X(waterQuality) \
  X(shadows) X(vsync) X(fullscreen) X(windowW) X(windowH) X(workerThreads) X(resourcePack) X(shaderPack) \
  X(playerName) X(skin) X(autoUpdate) X(keyBinds) X(padBinds) X(padMaps)

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
}
Settings Settings::load() {
  Settings s;
  if (auto j = readJson(userDir() / "settings.json")) s.fromJson(*j);
  return s;
}
void Settings::save() const { writeJson(userDir() / "settings.json", toJson()); }

void Settings::applyPreset(const std::string& p) {
  preset = p;
  if (p == "potato") { renderer = RendererKind::Legacy; renderDist = 4; farTerrain = 512; microVoxels = 0; waterQuality = 0; shadows = 0; particles = 1; fancyLeaves = false; plantDetail = "near"; scale = 0.75f; clouds = false; }
  else if (p == "low") { renderDist = 6; farTerrain = 1024; microVoxels = 1; waterQuality = 1; shadows = 0; particles = 1; fancyLeaves = false; scale = 1; }
  else if (p == "medium") { renderDist = 8; farTerrain = 2048; microVoxels = 2; microVoxelDist = 32; waterQuality = 1; shadows = 0; particles = 2; fancyLeaves = true; scale = 1; }
  else if (p == "high") { renderDist = 12; farTerrain = 4096; microVoxels = 2; microVoxelDist = 48; waterQuality = 2; shadows = 1; particles = 2; fancyLeaves = true; scale = 1; }
  else if (p == "ultra") { renderDist = 16; farTerrain = 8192; microVoxels = 2; microVoxelDist = 96; waterQuality = 2; shadows = 1; particles = 2; fancyLeaves = true; scale = 1; }
}

} // namespace dq
