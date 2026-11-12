// render/world_fx.h — the built-in shader levels' passes inside the world (Settings > Shaders), next to the picture-only ones
// in render/post_layer.cpp GradeLayer:
//   High:  sun shadows (a shadow map of the land round you from the sun, the terrain shader reading it), and water you see
//          into (the solid scene copied before the water: the bottom bent by the ripples, deeper water more of its colour)
//   Ultra: a bigger, finer shadow map, reflections in the water (traced across that copy), shading in the corners (from
//          its depth, laid over the solid scene before the water and the hand go in)
// Off in split screen, through portals, with an Iris/OptiFine pack (it lights the world itself) and on Vulkan.
#pragma once
#include "render/gl.h"
#include "render/frame.h"

namespace dq {

class GameSession;
class WorldRenderer;

void worldFxBegin(GameSession& s, const FrameCtx& f);         // before anything is drawn: the shadow map
void worldFxAfterOpaque(GameSession& s, const FrameCtx& f);   // the solid scene done: the corners' shading, then the copies
void worldFxEnd();                                            // the frame is drawn
void worldFxBind(Shader& sh, const FrameCtx& f, int pass);    // the terrain shader's uniforms and textures (units 8, 9, 10)

} // namespace dq
