// render/height_fog.h — fog that lies low: thick at the valley floor (the Overworld's sea level), thinning with height,
// so valleys fill with it and the hills stand out of it. assets/shaders/common.glsl applyFog integrates it along each
// view ray in closed form (an exponential layer); this works out its numbers for the frame (SkyRenderer::computeFrame).
#pragma once
#include "common/common.h"

namespace dq {

// x: fog density at the camera (per block, already thinned for the camera's height), y: the layer's falloff height
// (blocks), z: 1 on / 0 off, w: how much thicker it may get along a ray going down (ln of cap / x: the floor's cap
// holds below the floor, so a pit isn't filled solid). camY: the camera's global y; mist and rain 0..1
// (WeatherSystem); open: how open the sky is where you are (0 in a cave or a closed room: no fog in there).
vec4 heightFogParams(float camY, int dim, float mist, float rain, bool on, float open = 1);

} // namespace dq
