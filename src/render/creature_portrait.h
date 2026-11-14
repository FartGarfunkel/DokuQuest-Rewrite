// render/creature_portrait.h — a creature drawn into its own texture for the book (the journal's Compendium): its own
// voxel model, posed as the world poses it, turning slowly, lit evenly. Unlit (a silhouette) for one only glimpsed.
#pragma once
#include "render/gl.h"
#include <string>

namespace dq {

class CreaturePortrait {
public:
  ~CreaturePortrait();
  // the creature `type` into a w x h texture, turned yaw, `time` seconds into its idle; silhouette: dark, no picture.
  // Returns the texture (0: unknown creature or no framebuffer)
  GLuint render(const std::string& type, int w, int h, float yaw, float time, bool silhouette = false);
private:
  GLuint fbo = 0, tex = 0, depth = 0;
  int tw = 0, th = 0;
};

} // namespace dq
