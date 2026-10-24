// common/settings.h — game + launcher settings (userDir()/settings.json), shared by both programs.
// Field names follow the original game's DEFAULT_SETTINGS (game/game.js) plus the C++ additions.
#pragma once
#include "common/fs.h"

namespace dq {

enum class RendererKind { Legacy = 0, Modern = 1, Vulkan = 2 };   // GL 3.3 core / GL 4.6 core / (future)
const char* rendererName(RendererKind k);

struct Settings {
  // ---- original options ----
  int renderDist = 8;            // chunks
  float sens = 1, padSens = 1, fov = 72, scale = 1;   // scale = resolution scale
  bool fancyLeaves = true;
  std::string plantDetail = "near";   // off | near | all
  bool wavingPlants = true, autoJump = false;
  float volume = 0.6f, mobVol = 1, music = 0.5f, ambience = 0.7f;
  bool invertY = false, showFps = false, fpBody = true, viewBob = true;
  float brightness = 0.35f;
  std::string split = "auto";
  int particles = 2;             // 0 off, 1 few, 2 all
  std::string guiScale = "auto";
  bool toggleSneak = false, toggleSprint = false, screenShake = true, hurtTilt = true;
  int fpsLimit = 0;
  std::string padLayout = "xbox";
  bool heldSway = true, clouds = true, portalViews = true;
  int farTerrain = 2048;         // distant terrain (LOD) radius in blocks, 0 = off
  // ---- C++ engine additions ----
  RendererKind renderer = RendererKind::Modern;
  std::string preset = "auto";   // potato | low | medium | high | ultra | custom | auto
  int microVoxels = 2;           // 0 flat faces, 1 shaded micro-voxels, 2 relief micro-voxels (parallax)
  int microVoxelDist = 48;       // blocks: micro-voxel relief fades out past this
  int waterQuality = 2;          // 0 flat, 1 waves, 2 waves + reflections
  int shadows = 1;               // 0 off, 1 soft sun shadows (modern only)
  bool vsync = true, fullscreen = false;
  int windowW = 1280, windowH = 720;
  int workerThreads = 0;         // 0 = auto
  std::string resourcePack = "auto";   // "auto" picks GeruDoku if present in resourcepacks/
  std::string shaderPack = "";
  std::string playerName = "Wanderer";
  std::string skin = "";         // file in skins/ or "creator:<name>"
  bool autoUpdate = true;
  json keyBinds = json::object(), padBinds = json::object(), padMaps = json::object();
  json extra = json::object();   // unknown/mod keys are kept round-trip

  static Settings load();
  void save() const;
  void applyPreset(const std::string& p);
  json toJson() const;
  void fromJson(const json& j);
};

} // namespace dq
