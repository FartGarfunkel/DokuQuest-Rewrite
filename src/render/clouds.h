// render/clouds.h — clouds, Cube World style (render/clouds.js + the weather's clouds from
// game/skies.js): voxel cumulus heaps of rounded puffs with smaller puffs boiling up on top, in two
// layers, shaded like a soft volume (bright crowns, grey-blue bellies, a hazy halo near you), drifting
// with the wind; thick, dark and towering where a storm is, wisps running ahead of a front, lit
// from inside by lightning. One cloud layer for the stacked world, between the Overworld's sky and
// the Skylands, so from the Skylands the same clouds drift by below. Drawn instanced (one draw per
// cloud shape).
#pragma once
#include "render/frame.h"

namespace dq {

class Level;
struct SkyEnv;

constexpr int CLOUD_BASE_Y = 512 + 212;   // global y of the cloud layer (Overworld band-local 212, above the highest peaks, under the cloud bank)

class CloudRenderer {
public:
  ~CloudRenderer();
  void update(Level& lv, const FrameCtx& f, const SkyEnv& env);   // which clouds are where this frame
  void drawOpaque(const FrameCtx& f, const SkyEnv& env);
  void drawWisps(const FrameCtx& f, const SkyEnv& env);
  int drawn = 0;
  float reach = 680;
private:
  struct Cloud { vec3 pos; float d, s, dark, tower; int t, r; bool wisp; };
  struct Inst { float x, y, z, s; float sy, rot, fk, pad; float tr, tg, tb, ta; };
  void build();
  void drawSet(const FrameCtx& f, const SkyEnv& env, bool wisps);
  void drawGroups(const std::vector<std::pair<int, int>>& groups, bool wisps);
  void setUniforms(const FrameCtx& f, const SkyEnv& env);
  bool built = false, active = false;
  Shader shader;
  struct Tpl { GLuint vao = 0, vbo = 0; GLsizei count = 0; };
  Tpl tpl[10], wisp[4];
  GLuint inst = 0;
  std::vector<Cloud> list;
  std::vector<Inst> insts, shellInsts;
  std::vector<std::pair<int, int>> shellGroups;
  float thunder = 0, flash = 0;
};

} // namespace dq
