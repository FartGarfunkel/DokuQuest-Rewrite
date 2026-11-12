// ui/char_preview.h — a voxel character drawn into its own texture for the menus (the creator's turning model,
// the skin editor's 3D view): the same meshes and animation as in the world, lit evenly.
#pragma once
#include "render/char_anim.h"
#include "render/char_layer.h"
#include "render/gl.h"

namespace dq {

struct CharPreviewOpts {
  float yaw = PI + 0.45f, elev = 0.22f, dist = 64;   // turn, tilt, distance (model pixels) — skineditor.js's camera
  float time = 0;
  bool walk = false;                                 // walk on the spot
  bool still = false;                                // the rest pose (painting)
  bool gear = true;                                  // the 3D look's shapes
  bool overlay = true;                               // the outer layer
  int isolate = -1;                                  // only this skin part (0..5) bright, the rest faint
  std::vector<std::string> armor;                    // worn armour item keys (4, "" = none)
  std::string cape;                                  // a cape tile key ("" = the character's own)
  // a whole player as the world draws them (the inventory doll): what they wear and hold, and their pose
  const CharWear* wear = nullptr;
  const CharPose* pose = nullptr;
  bool headOnly = false;                             // just the head (the pause menu's "On this screen" list)
  float target = -1;                                 // the height looked at (model pixels; < 0 = the middle of the figure)
};

class CharPreview {
public:
  ~CharPreview();
  // draw the character into the texture (w x h pixels); returns the texture (0 if FBOs failed)
  GLuint render(const CharacterSpec& spec, int w, int h, const CharPreviewOpts& o);
  mat4 viewProj{1};      // the last render: model pixels (feet at y 0, unscaled rest pose) -> clip
  GLuint texture() const { return tex; }
private:
  GLuint fbo = 0, tex = 0, depth = 0;
  int tw = 0, th = 0;
};

// ImGui image of a preview texture (flipped the right way up)
void previewImage(GLuint tex, float w, float h);
// a controller stick's value (ImGuiKey_GamepadRStickRight ...), 0..1
float padAxis(int imguiKey);

} // namespace dq
