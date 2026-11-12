// render/pack_geom.h — voxel models drawn this frame, so a shader pack can redraw them.
// Creatures, characters, chests and dropped items are voxel meshes in render space (the same
// space as the terrain). The pack pass replays that list with gbuffers_entities.
// The first-person hand is a second list. Its projection is not the world's, so it never
// shares the world queue or the world depth image. The pack draws it on the next frame.
#pragma once
#include "common/common.h"
#include "render/gl.h"
#include <vector>

namespace dq {

struct PackVoxelDraw {
  const Mesh* mesh = nullptr;
  mat4 model{1.f};
  float sky = 1.f;
  float block = 0.f;
  vec4 tint{0.f};
  bool mirrored = false;
};

inline std::vector<PackVoxelDraw>& packVoxelQueue() {
  static std::vector<PackVoxelDraw> q;
  return q;
}
inline bool& packVoxelCaptureOn() {
  static bool on = false;
  return on;
}
inline void packVoxelSetCapture(bool on) { packVoxelCaptureOn() = on; }
inline void packVoxelClear() { packVoxelQueue().clear(); }

inline void packVoxelNote(const Mesh* mesh, const mat4& model, float sky, float block, vec4 tint) {
  if (!packVoxelCaptureOn() || !mesh) return;
  auto& q = packVoxelQueue();
  if (q.size() >= 20000) return;
  PackVoxelDraw d;
  d.mesh = mesh;
  d.model = model;
  d.sky = sky;
  d.block = block;
  d.tint = tint;
  d.mirrored = glm::determinant(mat3(model)) < 0.f;
  q.push_back(d);
}

// The hand list is what the first-person pass just recorded. The pack reads the previous
// frame's list, because that pass runs after the pack. suppress is set when gbuffers_hand
// linked, so the game arm is not drawn on top of the pack's tonemapped arm.
struct PackHandFrame {
  mat4 proj{1.f};
  std::vector<PackVoxelDraw> draws;
};
inline PackHandFrame& packHandCur() { static PackHandFrame f; return f; }
inline PackHandFrame& packHandPrev() { static PackHandFrame f; return f; }
inline bool& packHandCaptureOn() { static bool on = false; return on; }
inline bool& packHandSuppress() { static bool on = false; return on; }
inline void packHandSetSuppress(bool on) { packHandSuppress() = on; }
inline void packHandBegin(const mat4& proj) {
  packHandCaptureOn() = true;
  packHandCur().proj = proj;
  packHandCur().draws.clear();
}
inline void packHandEnd() {
  packHandCaptureOn() = false;
  packHandPrev() = std::move(packHandCur());
  packHandCur() = {};
}
// A pack that sets clouds=off draws the sky itself (Complementary's deferred raymarch).
// The game's planes would be copied into that picture and hide the pack's clouds.
inline bool& packSkipGameClouds() { static bool on = false; return on; }
inline void packSetSkipClouds(bool on) { packSkipGameClouds() = on; }

inline void packHandNote(const Mesh* mesh, const mat4& model, float sky, float block, vec4 tint) {
  if (!packHandCaptureOn() || !mesh) return;
  auto& q = packHandCur().draws;
  if (q.size() >= 256) return;
  PackVoxelDraw d;
  d.mesh = mesh;
  d.model = model;
  d.sky = sky;
  d.block = block;
  d.tint = tint;
  d.mirrored = glm::determinant(mat3(model)) < 0.f;
  q.push_back(d);
}

} // namespace dq
