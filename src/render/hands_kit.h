// render/hands_kit.h — drawing the hands-on stations over our blocks (docs/HANDS_ON.md): a pose
// in block space (0..1 over the block, turned to the block's facing about its middle as hands-on turns it), an
// item drawn as an item frame shows it (a flat item a block wide turned to face back; a block item half a
// block), and a hands-on model part (a door, a lid, the wheel) drawn face by face from its pictures at any pose.
#pragma once
#include "render/frame.h"
#include "assets/voxel.h"
#include "core/itemstack.h"

namespace dq {
class App; class ContentDraw; class World;
struct PackModel;

struct TPose {
  mat4 m{1};   // block space (0..1) -> render space
  TPose& t(float x, float y, float z) { m = glm::translate(m, vec3(x, y, z)); return *this; }
  TPose& rx(float deg) { m = glm::rotate(m, glm::radians(deg), vec3(1, 0, 0)); return *this; }
  TPose& ry(float deg) { m = glm::rotate(m, glm::radians(deg), vec3(0, 1, 0)); return *this; }
  TPose& rz(float deg) { m = glm::rotate(m, glm::radians(deg), vec3(0, 0, 1)); return *this; }
  TPose& s(float x, float y, float z) { m = glm::scale(m, vec3(x, y, z)); return *this; }
  TPose& s(float k) { return s(k, k, k); }
};
// the block at `at`, turned to the hands-on facing (sim/hands.h handsFacing): the renderers' first three steps
TPose handsBlockPose(const FrameCtx& f, ivec3 at, int facing);
// an item drawn as an item frame shows it, at the pose (VoxelRenderer begun by the caller)
void handsItem(const ItemStack& s, const mat4& pose, VoxelLight L, vec4 tint = vec4(0));
// a model's faces at the pose (block space 0..1 = the model's 0..16), each its own picture (ContentDraw begun by the caller)
void handsPartDraw(App& app, ContentDraw& cd, const PackModel& M, const mat4& pose, vec2 light);
// writing laid on a surface as the hands-on sheet writes it: centred at (cx, y, cz) in the pose's block space
// (sixteenths for cx and cz, blocks for y), read from above the surface, wrapped to maxW sixteenths, in the UI font; scale
// is the hands-on (for its 9-pixel font). ContentDraw begun by the caller
void handsText(ContentDraw& cd, const TPose& pose, float cx, float y, float cz, const std::string& text, float scale, float maxW, vec4 color, vec2 light);
// the light a hands-on renderer sees at a block (the brightest of it and the air round it)
VoxelLight handsLight(World& w, ivec3 at);
// a door or lid's angle easing toward open (1) or shut (0), the hands-on 0.15 a tick (3 a second); per block, kept here
float handsEase(ivec3 at, int part, bool open, double dt);

} // namespace dq
