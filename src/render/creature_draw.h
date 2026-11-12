// render/creature_draw.h — the creature shader (assets/shaders/creature.glsl: voxel.glsl + alpha) and the
// client-only wildlife (render/creature_fauna.cpp). Internal to the creature layer.
#pragma once
#include "assets/voxel.h"
#include "render/frame.h"

namespace dq {

class GameSession;

class CreatureDraw {
public:
  bool begin(const FrameCtx& f);
  void draw(const VoxelMeshGL& m, const mat4& model, VoxelLight light, vec4 tint = vec4(0), float alpha = 1);
  void end();
private:
  std::unique_ptr<Shader> sh;
  bool tried = false;
};
CreatureDraw& creatureDraw();
VoxelLight lightAt(const class World& w, vec3 p);

// game/fauna.js: butterflies by day, fireflies by night, songbirds that scatter, fish in lakes and rivers
class Fauna {
public:
  Fauna();
  ~Fauna();
  void update(GameSession& s, float dt);
  void draw(GameSession& s, const FrameCtx& f);
  int count() const;
private:
  struct Critter; struct Meshes;
  std::vector<Critter>* list;
  std::unique_ptr<Meshes> meshes;
  float t = 0;
  void spawn(GameSession& s, vec3 p);
};

} // namespace dq
