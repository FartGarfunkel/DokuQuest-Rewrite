// common/settings.h — game + launcher settings (userDir()/settings.json), shared by both programs.
// Field names follow the original game's DEFAULT_SETTINGS (game/game.js) plus the C++ additions.
#pragma once
#include "common/fs.h"

namespace dq {

enum class RendererKind { Legacy = 0, Modern = 1, Vulkan = 2 };   // GL 3.3 core / GL 4.6 core / Vulkan 1.3
const char* rendererName(RendererKind k);

struct Settings {
  // ---- original options ----
  int renderDist = 8;            // chunks
  float sens = 1, padSens = 1, fov = 72, scale = 1;   // scale = resolution scale
  bool fancyLeaves = true;
  std::string plantDetail = "near";   // off | near | all
  bool wavingPlants = true, autoJump = false;
  float volume = 0.6f, mobVol = 1, music = 0.5f, ambience = 0.7f;
  bool captions = false;         // captions for the sounds around you (ui/captions.cpp; Options → Accessibility)
  int colorFilter = 0;           // colour-blind filter: 0 off, 1 protanopia, 2 deuteranopia, 3 tritanopia (render/colour_filter_layer.cpp)
  int musicFreq = 1;             // how often music plays: 0 rarely, 1 now and then, 2 often, 3 nearly always
  bool invertY = false, showFps = false, fpBody = true, viewBob = true;
  float brightness = 0.35f;      // Minecraft's brightness curve: 0 moody (dark stays dark) .. 1 bright (common.glsl mcBrightness)
  // the picture's colour (render/colour_filter_layer.cpp): a style, or "custom" once a slider is moved
  std::string colourStyle = "natural";   // vibrant | natural | classic | custom
  float saturation = 0.85f;      // 0 grey .. 1 as drawn .. 1.5
  float contrast = 0.94f;        // 0.5 flat .. 1 as drawn .. 1.5
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
  int microVoxels = 0;           // 0 flat faces, 1 shaded micro-voxels, 2 relief micro-voxels (parallax; still being worked on, so off by default)
  int microVoxelDist = 48;       // blocks: micro-voxel relief fades out past this
  int waterQuality = 2;          // 0 flat, 1 waves, 2 waves + reflections
  int shadows = 1;               // 0 off, 1 soft sun shadows (modern only)
  // weather (render/weather_layer.cpp; after Particle Rain): how many weather particles at most (0 off, 600 low, 1500
  // normal, 3000 high), how far round you they fall (blocks), splashes/ripples/streaks, ground fog sheets, height fog
  int weatherParticles = 1500, weatherDist = 16;
  bool weatherSurfaceFx = true, groundFog = true, heightFog = true;
  bool vsync = true, fullscreen = false;
  int windowW = 1280, windowH = 720;
  int workerThreads = 0;         // 0 = auto
  std::string resourcePack = "DokuQuest Base";   // the game's own art. Older "auto", "none" and "builtin" mean this too.
  std::string shaderPack = "";   // off
  std::string gpu = "auto";      // graphics card: "auto" (the dedicated one) or "vendor:device" in hex, e.g. "1002:73bf" (render/gpu_select.h)
  std::string playerName = "Wanderer";
  std::string skin = "";         // file in skins/ or "creator:<name>"
  bool autoUpdate = true;
  // Window focus and saving follow BTA 8.0's options, written for this engine.
  bool pauseOnLostFocus = true;   // open the pause menu 250 ms after the window loses focus
  bool muteOnLostFocus = false;   // silence the mix; the volume sliders stay where they are
  bool rawMouse = true;           // this game already uses raw look; off warps the pointer instead
  int autosaveSec = 60;           // 0 off, else a step of 5 seconds up to 115. 60 is the rate worlds already use.
  json keyBinds = json::object(), padBinds = json::object(), padMaps = json::object();
  json extra = json::object();   // unknown/mod keys are kept round-trip

  static Settings load();
  void save() const;
  void applyPreset(const std::string& p);
  void applyColourStyle(const std::string& style);   // sets saturation and contrast for vibrant / natural / classic
  json toJson() const;
  void fromJson(const json& j);
};

} // namespace dq
